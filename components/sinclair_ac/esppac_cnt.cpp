// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esppac_cnt.h"

namespace esphome {
namespace sinclair_ac {
namespace CNT {

static const char *const TAG = "sinclair_ac.serial";

void SinclairACCNT::setup()
{
    SinclairAC::setup();

    ESP_LOGD(TAG, "Using serial protocol for Sinclair AC");
}

void SinclairACCNT::loop()
{
    /* this reads data from UART */
    SinclairAC::loop();

    /* we have a frame from AC */
    if (this->serialProcess_.state == STATE_COMPLETE)
    {
        /* do not forget to order for restart of the recieve state machine */
        this->serialProcess_.state = STATE_RESTART;
        /* mark that we have recieved a response */
        this->wait_response_ = false;
        /* log for ESPHome debug */
        log_packet(this->serialProcess_.data);

        if (!verify_packet())  /* Verify length, header, counter and checksum */
        {
            return;
        }

        this->last_packet_received_ = millis();  /* Set the time at which we received our last packet */

        /* A valid recieved packet of accepted type marks module as being ready */
        if (this->state_ != ACState::Ready)
        {
            this->state_ = ACState::Ready;  
            Component::status_clear_error();
            this->last_packet_sent_ = millis();
        }

        if (this->update_ == ACUpdate::NoUpdate)
        {
            handle_packet(); /* this will update state of components in HA as well as internal settings */
        }
    }

    /* we will send a packet to the AC as a reponse to indicate changes */
    send_packet();

    /* if there are no packets for a while - mark module as not ready */
    if (millis() - this->last_packet_received_ >= protocol::TIME_TIMEOUT_INACTIVE_MS)
    {
        if (this->state_ != ACState::Initializing)
        {
            this->state_ = ACState::Initializing;
            Component::status_set_error();
        }
    }
}

/*
 * ESPHome control request
 */

void SinclairACCNT::control(const climate::ClimateCall &call)
{
    if (this->state_ != ACState::Ready)
        return;

    if (call.get_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->mode = *call.get_mode();
    }

    if (call.get_target_temperature().has_value())
    {
        ESP_LOGV(TAG, "Requested target teperature change");
        this->update_ = ACUpdate::UpdateStart;
        this->target_temperature = *call.get_target_temperature();
        if (this->target_temperature < MIN_TEMPERATURE)
        {
            this->target_temperature = MIN_TEMPERATURE;
        }
        else if (this->target_temperature > MAX_TEMPERATURE)
        {
            this->target_temperature = MAX_TEMPERATURE;
        }
    }

    if (call.has_custom_fan_mode())
    {
        ESP_LOGV(TAG, "Requested fan mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->set_custom_fan_mode_(call.get_custom_fan_mode());
    }
}

/*
 * Send a raw packet, as is
 */

void SinclairACCNT::send_packet()
{
    std::vector<uint8_t> packet(protocol::SET_PACKET_LEN, 0);  /* Initialize packet contents */

    if (this->wait_response_ == true && (millis() - this->last_packet_sent_) < protocol::TIME_REFRESH_PERIOD_MS)
    {
        /* do net send packet too often or when we are waiting for report to come */
        return;
    }

    if (this->update_ == ACUpdate::NoUpdate)
    {
        /* nothing to change - just poll the unit for a report from time to time */
        if ((millis() - this->last_packet_sent_) < protocol::TIME_POLL_PERIOD_MS)
        {
            return;
        }

        this->last_packet_sent_ = millis();
        this->wait_response_ = true;
        write_array(protocol::POLL_PACKET);
        log_packet(protocol::POLL_PACKET, true);
        return;
    }

    packet[protocol::SET_CONST_14_BYTE] = protocol::SET_CONST_14_VAL; /* Some always 0x14 byte... */
    packet[protocol::SET_CONST_BIT_BYTE] = protocol::SET_CONST_BIT_MASK; /* Some always true bit */

    /* MODE and POWER --------------------------------------------------------------------------- */
    uint8_t mode = protocol::REPORT_MODE_COOL;
    bool power = false;
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_COOL:
            mode = protocol::REPORT_MODE_COOL;
            power = true;
            break;
        case climate::CLIMATE_MODE_DRY:
            mode = protocol::REPORT_MODE_DRY;
            power = true;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            mode = protocol::REPORT_MODE_FAN;
            power = true;
            break;
        default:
        case climate::CLIMATE_MODE_OFF:
            /* In case of MODE_OFF we will not alter the last mode setting recieved from AC, see determine_mode() */
            switch (this->mode_internal_)
            {
                case climate::CLIMATE_MODE_DRY:
                    mode = protocol::REPORT_MODE_DRY;
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    mode = protocol::REPORT_MODE_FAN;
                    break;
                default:
                case climate::CLIMATE_MODE_COOL:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
            }
            power = false;
            break;
    }

    packet[protocol::REPORT_MODE_BYTE] |= (mode << protocol::REPORT_MODE_POS);
    if (power)
    {
        packet[protocol::REPORT_PWR_BYTE] |= protocol::REPORT_PWR_MASK;
    }

    /* TARGET TEMPERATURE --------------------------------------------------------------------------- */
    uint16_t target_temperature = (uint16_t)lround(this->target_temperature * protocol::REPORT_TEMP_SET_DIV);
    packet[protocol::REPORT_TEMP_SET_BYTE] = (uint8_t)(target_temperature & 0xFF);
    if (target_temperature > 0xFF)
    {
        packet[protocol::REPORT_TEMP_SET_HI_BYTE] |= protocol::REPORT_TEMP_SET_HI_MASK;
    }

    /* FAN SPEED --------------------------------------------------------------------------- */
    /* below will default to AUTO */
    uint8_t fanSpeed1 = 1;
    if (this->has_custom_fan_mode())
    {
        const char* custom_fan_mode = this->get_custom_fan_mode().c_str();

        if (strcmp(custom_fan_mode, fan_modes::FAN_AUTO) == 0)
        {
            fanSpeed1 = 1;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_LOW) == 0)
        {
            fanSpeed1 = 2;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_MED) == 0)
        {
            fanSpeed1 = 4;
        }
        else if (strcmp(custom_fan_mode, fan_modes::FAN_HIGH) == 0)
        {
            fanSpeed1 = 6;
        }
        else
        {
            fanSpeed1 = 1;
        }
    }

    packet[protocol::REPORT_FAN_SPD1_BYTE] |= (fanSpeed1 << protocol::REPORT_FAN_SPD1_POS);

    /* Do the command, length */
    packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
    packet.insert(packet.begin(), protocol::SET_PACKET_LEN + 2); /* Add 2 bytes as we added a command and will add checksum */

    /* Do checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 0 ; i < packet.size() ; i++)
    {
        checksum += packet[i];
    }
    packet.push_back(checksum);

    /* Do SYNC bytes */
    packet.insert(packet.begin(), protocol::SYNC);
    packet.insert(packet.begin(), protocol::SYNC);

    this->last_packet_sent_ = millis();  /* Save the time when we sent the last packet */
    this->wait_response_ = true;
    write_array(packet);                 /* Sent the packet by UART */
    log_packet(packet, true);            /* Log uart for debug purposes */

    /* settings are applied with a single packet */
    this->update_ = ACUpdate::NoUpdate;
}

/*
 * Packet handling
 */

bool SinclairACCNT::verify_packet()
{
    /* At least 2 sync bytes + length + type + checksum */
    if (this->serialProcess_.data.size() < 5)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (length)");
        return false;
    }

    /* The header (aka sync bytes) was checked by SinclairAC::read_data() */

    /* The frame len was assumed by SinclairAC::read_data() */

    /* Check if this packet type sould be processed */
    bool commandAllowed = false;
    for (uint8_t packet : allowedPackets)
    {
        if (this->serialProcess_.data[3] == packet)
        {
            commandAllowed = true;
            break;
        }
    }
    if (!commandAllowed)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (command [%02X] not allowed)", this->serialProcess_.data[3]);
        return false;
    }

    /* Report must hold the full data block: 2 sync bytes + length + type + data + checksum */
    if (this->serialProcess_.data.size() != protocol::SET_PACKET_LEN + 5)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (length %u)", (unsigned) this->serialProcess_.data.size());
        return false;
    }

    /* Check checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 2 ; i < this->serialProcess_.data.size() - 1 ; i++)
    {
        checksum += this->serialProcess_.data[i];
    }
    if (checksum != this->serialProcess_.data[this->serialProcess_.data.size()-1])
    {
        ESP_LOGD(TAG, "Dropping invalid packet (checksum)");
        return false;
    }

    return true;
}

void SinclairACCNT::handle_packet()
{
    if (this->serialProcess_.data[3] == protocol::CMD_IN_UNIT_REPORT)
    {
        /* here we will remove unnecessary elements - header and checksum */
        this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4); /* remove header */
        this->serialProcess_.data.pop_back();  /* remove checksum */
        /* now process the data */
        this->processUnitReport();
        this->publish_state();
    }
    else 
    {
        ESP_LOGD(TAG, "Received unknown packet");
    }
}

/*
 * This decodes frame recieved from AC Unit
 */
bool SinclairACCNT::processUnitReport()
{
    bool hasChanged = false;

    climate::ClimateMode newMode = determine_mode();
    if (this->mode != newMode) hasChanged = true;
    this->mode = newMode;

    const char* newFanMode = determine_fan_mode();
    if (this->has_custom_fan_mode())
    {
        if (strcmp(this->get_custom_fan_mode().c_str(), newFanMode) != 0) hasChanged = true;
    }
    else
    {
        hasChanged = true;
    }
    this->set_custom_fan_mode_(newFanMode);
    
    uint16_t newTargetTemperatureRaw = this->serialProcess_.data[protocol::REPORT_TEMP_SET_BYTE]
        | ((this->serialProcess_.data[protocol::REPORT_TEMP_SET_HI_BYTE] & protocol::REPORT_TEMP_SET_HI_MASK) << 8);
    float newTargetTemperature = (float)newTargetTemperatureRaw / protocol::REPORT_TEMP_SET_DIV;
    if (this->target_temperature != newTargetTemperature) hasChanged = true;
    this->update_target_temperature(newTargetTemperature);

    /* the unit does not report the room temperature, it comes from the external sensor (if configured) */

    return hasChanged;
}

climate::ClimateMode SinclairACCNT::determine_mode()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;

    /* as mode presented by climate component incorporates both power and mode we will store this separately for Sinclair
       in _internal_ fields */
    /* check unit power flag */
    this->power_internal_ = (this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;

    /* check unit mode */
    switch (mode)
    {
        case protocol::REPORT_MODE_COOL:
            this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            break;
        case protocol::REPORT_MODE_DRY:
            this->mode_internal_ = climate::CLIMATE_MODE_DRY;
            break;
        case protocol::REPORT_MODE_FAN:
            this->mode_internal_ = climate::CLIMATE_MODE_FAN_ONLY;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown climate mode");
            this->mode_internal_ = climate::CLIMATE_MODE_OFF;
            break;
    }

    /* if unit is powered on - return the mode, otherwise return CLIMATE_MODE_OFF */
    if (this->power_internal_)
    {
        return this->mode_internal_;
    }
    else
    {
        return climate::CLIMATE_MODE_OFF;
    }
}

const char* SinclairACCNT::determine_fan_mode()
{
    uint8_t fanSpeed1 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD1_BYTE]  & protocol::REPORT_FAN_SPD1_MASK) >> protocol::REPORT_FAN_SPD1_POS;
    if      (fanSpeed1 == 1)
    {
        return fan_modes::FAN_AUTO;
    }
    else if (fanSpeed1 == 2)
    {
        return fan_modes::FAN_LOW;
    }
    else if (fanSpeed1 == 4)
    {
        return fan_modes::FAN_MED;
    }
    else if (fanSpeed1 == 6)
    {
        return fan_modes::FAN_HIGH;
    }
    else
    {
        ESP_LOGW(TAG, "Received unknown fan mode");
        return fan_modes::FAN_AUTO;
    }
}

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
