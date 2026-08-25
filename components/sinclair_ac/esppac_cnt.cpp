#include "esppac_cnt.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"

namespace esphome::sinclair_ac::CNT {

void SinclairACCNT::setup() {
  SinclairAC::setup();
  this->last_packet_received_ = millis();
  this->target_temperature = 24.0f;
  this->reported_target_ = 24.0f;
  this->fan_mode = climate::CLIMATE_FAN_AUTO;
}

void SinclairACCNT::loop() {
  SinclairAC::loop();
  if (this->serialProcess_.state != STATE_COMPLETE) {
    this->send_pending();
    return;
  }

  this->serialProcess_.state = STATE_RESTART;
  this->wait_response_ = false;
  if (this->verify_packet()) {
    this->ready_ = true;
    this->last_packet_received_ = millis();
    // Preserve a pending HA request until its command packet is sent. The
    // debug implementation used the same sequencing to avoid a report
    // overwriting the requested state before transmission.
    if (this->update_ == Update::None) this->handle_packet();
  }
  this->send_pending();
}

void SinclairACCNT::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    const auto requested = *call.get_mode();
    if (requested == climate::CLIMATE_MODE_OFF || requested == climate::CLIMATE_MODE_AUTO ||
        requested == climate::CLIMATE_MODE_COOL || requested == climate::CLIMATE_MODE_DRY ||
        requested == climate::CLIMATE_MODE_FAN_ONLY) {
      this->mode = requested;
      this->update_ = Update::Pending;
    }
  }
  if (call.get_target_temperature().has_value()) {
    this->target_temperature = std::clamp(*call.get_target_temperature(),
                                          static_cast<float>(MIN_TEMPERATURE),
                                          static_cast<float>(MAX_TEMPERATURE));
    this->update_ = Update::Pending;
  }
  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_AUTO:
      case climate::CLIMATE_FAN_LOW:
      case climate::CLIMATE_FAN_MEDIUM:
      case climate::CLIMATE_FAN_HIGH:
        this->fan_mode = *call.get_fan_mode();
        break;
      default:
        break;
    }
    if (*call.get_fan_mode() == climate::CLIMATE_FAN_AUTO || *call.get_fan_mode() == climate::CLIMATE_FAN_LOW ||
        *call.get_fan_mode() == climate::CLIMATE_FAN_MEDIUM || *call.get_fan_mode() == climate::CLIMATE_FAN_HIGH)
      this->update_ = Update::Pending;
  }
  if (call.get_swing_mode().has_value()) {
    if (*call.get_swing_mode() == climate::CLIMATE_SWING_OFF ||
        *call.get_swing_mode() == climate::CLIMATE_SWING_VERTICAL) {
      this->swing_mode = *call.get_swing_mode();
      this->update_ = Update::Pending;
    }
  }
}

bool SinclairACCNT::verify_packet() const {
  const auto &packet = this->serialProcess_.data;
  if (packet.size() < 5 || packet[3] != protocol::CMD_IN_UNIT_REPORT) return false;
  uint8_t checksum = 0;
  for (size_t i = 2; i + 1 < packet.size(); i++) checksum += packet[i];
  return checksum == packet.back();
}

void SinclairACCNT::handle_packet() {
  this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4);
  this->serialProcess_.data.pop_back();
  if (this->serialProcess_.data.size() != protocol::REPORT_SHORT_DATA_LEN &&
      this->serialProcess_.data.size() != protocol::SET_PACKET_LEN) return;
  this->process_report();
}

bool SinclairACCNT::is_short_report() const {
  return this->serialProcess_.data.size() == protocol::REPORT_SHORT_DATA_LEN;
}

void SinclairACCNT::process_report() {
  const auto &data = this->serialProcess_.data;
  const bool short_report = this->is_short_report();
  this->reported_power_ = short_report ? (data[protocol::REPORT_SHORT_PWR_BYTE] & protocol::REPORT_SHORT_PWR_MASK) != 0
                                       : (data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;
  const uint8_t mode = short_report ? data[protocol::REPORT_SHORT_MODE_BYTE]
                                    : ((data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >>
                                       protocol::REPORT_MODE_POS);
  switch (mode) {
    case 0: this->reported_mode_ = climate::CLIMATE_MODE_AUTO; break;
    case protocol::REPORT_SHORT_MODE_AUTO_HEAT: this->reported_mode_ = climate::CLIMATE_MODE_AUTO; break;
    case protocol::REPORT_MODE_COOL: this->reported_mode_ = climate::CLIMATE_MODE_COOL; break;
    case protocol::REPORT_MODE_DRY: this->reported_mode_ = climate::CLIMATE_MODE_DRY; break;
    case protocol::REPORT_MODE_FAN: this->reported_mode_ = climate::CLIMATE_MODE_FAN_ONLY; break;
    default: return;
  }
  this->mode = this->reported_power_ ? this->reported_mode_ : climate::CLIMATE_MODE_OFF;
  const uint8_t fan = short_report ? (data[protocol::REPORT_SHORT_FAN_SPD1_BYTE] & protocol::REPORT_FAN_SPD1_MASK)
                                   : (data[protocol::REPORT_FAN_SPD1_BYTE] & protocol::REPORT_FAN_SPD1_MASK);
  const bool turbo = short_report ? ((data[protocol::REPORT_SHORT_PWR_BYTE] & protocol::REPORT_SHORT_TURBO_MASK) != 0)
                                  : ((data[protocol::REPORT_FAN_TURBO_BYTE] & protocol::REPORT_FAN_TURBO_MASK) != 0);
  if (turbo || fan == 0 || fan == protocol::FAN_HIGH) this->fan_mode = climate::CLIMATE_FAN_HIGH;
  else if (fan == protocol::FAN_AUTO) this->fan_mode = climate::CLIMATE_FAN_AUTO;
  else if (fan == protocol::FAN_LOW) this->fan_mode = climate::CLIMATE_FAN_LOW;
  else if (fan == protocol::FAN_MEDIUM) this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  else this->fan_mode = climate::CLIMATE_FAN_AUTO;

  const uint16_t raw_target = short_report
                                  ? (data[protocol::REPORT_SHORT_TEMP_SET_LO_BYTE] |
                                     ((data[protocol::REPORT_SHORT_TEMP_SET_HI_BYTE] & protocol::REPORT_TEMP_SET_HI_MASK) << 8))
                                  : (data[protocol::REPORT_TEMP_SET_LO_BYTE] |
                                     ((data[protocol::REPORT_TEMP_SET_HI_BYTE] & protocol::REPORT_TEMP_SET_HI_MASK) << 8));
  this->reported_target_ = protocol::REPORT_TEMP_SET_C_BASE +
                           static_cast<float>(raw_target - protocol::REPORT_TEMP_SET_RAW_BASE) /
                               protocol::REPORT_TEMP_SET_RAW_STEP;
  this->update_target_temperature(this->reported_target_);
  const float current = short_report ? static_cast<float>(data[protocol::REPORT_SHORT_TEMP_ACT_BYTE] +
                                                            protocol::REPORT_SHORT_TEMP_ACT_OFF)
                                     : (static_cast<float>(data[protocol::REPORT_TEMP_ACT_BYTE]) -
                                        protocol::REPORT_TEMP_ACT_OFF) /
                                           protocol::REPORT_TEMP_ACT_DIV;
  this->update_current_temperature(current);
  const uint8_t swing = short_report ? (data[protocol::REPORT_SHORT_VSWING_BYTE] & protocol::REPORT_SHORT_VSWING_MASK)
                                     : ((data[protocol::REPORT_VSWING_BYTE] & protocol::REPORT_VSWING_MASK) >>
                                        protocol::REPORT_VSWING_POS);
  ESP_LOGD("sinclair_ac", "Vertical swing report: %s frame, raw=%u", short_report ? "short" : "full", swing);
  // The Lomo's report values observed on this unit are inverse to the command
  // encoding: 0 reports active full sweep, while non-zero is not sweeping.
  this->swing_mode = swing == 0 ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
  this->publish_state();
}

void SinclairACCNT::send_pending() {
  const uint32_t now = millis();
  if (now - this->last_packet_sent_ < 300) return;
  if (this->update_ == Update::Pending) {
    this->send_short_packet();
    return;
  }
  if (this->wait_response_ || now - this->last_packet_sent_ < 5000) return;
  this->send_full_packet();
}

void SinclairACCNT::send_full_packet() {
  std::vector<uint8_t> packet(protocol::SET_PACKET_LEN, 0);
  const bool has_update = this->update_ == Update::Pending;
  const bool power = this->mode != climate::CLIMATE_MODE_OFF;
  const auto requested_mode = power ? this->mode : this->reported_mode_;
  uint8_t mode = protocol::REPORT_MODE_COOL;
  if (requested_mode == climate::CLIMATE_MODE_AUTO) mode = protocol::REPORT_MODE_AUTO;
  else if (requested_mode == climate::CLIMATE_MODE_DRY) mode = protocol::REPORT_MODE_DRY;
  else if (requested_mode == climate::CLIMATE_MODE_FAN_ONLY) mode = protocol::REPORT_MODE_FAN;
  packet[protocol::REPORT_MODE_BYTE] = static_cast<uint8_t>(mode << protocol::REPORT_MODE_POS);
  packet[protocol::SET_CONST_02_BYTE] = protocol::SET_CONST_02_VAL;
  packet[protocol::SET_CONST_BIT_BYTE] |= protocol::SET_CONST_BIT_MASK;
  if (has_update) packet[protocol::SET_AF_BYTE] = protocol::SET_AF_VAL;
  else packet[protocol::SET_NOCHANGE_BYTE] |= protocol::SET_NOCHANGE_MASK;
  if (power) packet[protocol::REPORT_PWR_BYTE] |= protocol::REPORT_PWR_MASK;
  const auto fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  if (fan == climate::CLIMATE_FAN_LOW) packet[protocol::REPORT_FAN_SPD1_BYTE] |= protocol::FAN_LOW;
  else if (fan == climate::CLIMATE_FAN_MEDIUM) packet[protocol::REPORT_FAN_SPD1_BYTE] |= protocol::FAN_MEDIUM;
  else if (fan == climate::CLIMATE_FAN_HIGH) packet[protocol::REPORT_FAN_SPD1_BYTE] |= protocol::FAN_HIGH;
  else packet[protocol::REPORT_FAN_SPD1_BYTE] |= protocol::FAN_AUTO;
  const auto raw_target = static_cast<uint16_t>(protocol::REPORT_TEMP_SET_RAW_BASE +
      lround((this->target_temperature - protocol::REPORT_TEMP_SET_C_BASE) * protocol::REPORT_TEMP_SET_RAW_STEP));
  packet[protocol::REPORT_TEMP_SET_LO_BYTE] = raw_target & 0xFF;
  packet[protocol::REPORT_TEMP_SET_HI_BYTE] = (raw_target >> 8) & protocol::REPORT_TEMP_SET_HI_MASK;
  packet[protocol::REPORT_VSWING_BYTE] =
      this->swing_mode == climate::CLIMATE_SWING_VERTICAL ? 0x10 : 0x40;
  packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
  packet.insert(packet.begin(), protocol::SET_PACKET_LEN + 2);
  uint8_t checksum = 0;
  for (const auto byte : packet) checksum += byte;
  packet.push_back(checksum);
  packet.insert(packet.begin(), protocol::SYNC);
  packet.insert(packet.begin(), protocol::SYNC);
  this->write_array(packet);
  this->last_packet_sent_ = millis();
  this->wait_response_ = true;
  this->update_ = Update::None;
}

void SinclairACCNT::send_short_packet() {
  std::vector<uint8_t> packet(protocol::REPORT_SHORT_DATA_LEN, 0);
  const bool power = this->mode != climate::CLIMATE_MODE_OFF;
  uint8_t mode = protocol::REPORT_SHORT_MODE_COOL;
  const auto requested_mode = power ? this->mode : this->reported_mode_;
  if (requested_mode == climate::CLIMATE_MODE_DRY) mode = protocol::REPORT_SHORT_MODE_DRY;
  else if (requested_mode == climate::CLIMATE_MODE_FAN_ONLY) mode = protocol::REPORT_SHORT_MODE_FAN;
  packet[protocol::SET_SHORT_TRANSITION_BYTE] = protocol::SET_SHORT_TRANSITION_VAL;
  packet[protocol::REPORT_SHORT_MODE_BYTE] = mode;
  packet[protocol::REPORT_SHORT_FAN_SPD1_BYTE] = protocol::FAN_AUTO;
  const auto fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  if (fan == climate::CLIMATE_FAN_LOW) packet[protocol::REPORT_SHORT_FAN_SPD1_BYTE] = protocol::FAN_LOW;
  else if (fan == climate::CLIMATE_FAN_MEDIUM) packet[protocol::REPORT_SHORT_FAN_SPD1_BYTE] = protocol::FAN_MEDIUM;
  else if (fan == climate::CLIMATE_FAN_HIGH) packet[protocol::REPORT_SHORT_FAN_SPD1_BYTE] = protocol::FAN_HIGH;
  packet[protocol::REPORT_SHORT_VSWING_BYTE] = this->swing_mode == climate::CLIMATE_SWING_VERTICAL
                                                      ? protocol::REPORT_SHORT_VSWING_AUTO
                                                      : protocol::REPORT_SHORT_VSWING_MID;
  const auto raw_target = static_cast<uint16_t>(protocol::REPORT_TEMP_SET_RAW_BASE +
      lround((this->target_temperature - protocol::REPORT_TEMP_SET_C_BASE) * protocol::REPORT_TEMP_SET_RAW_STEP));
  packet[protocol::REPORT_SHORT_TEMP_SET_LO_BYTE] = raw_target & 0xFF;
  packet[protocol::REPORT_SHORT_TEMP_SET_HI_BYTE] = (raw_target >> 8) & protocol::REPORT_TEMP_SET_HI_MASK;
  packet[protocol::REPORT_SHORT_PWR_BYTE] = protocol::REPORT_SHORT_PWR_BASE |
                                             (power ? protocol::REPORT_SHORT_PWR_MASK : 0);
  packet[protocol::REPORT_SHORT_TEMP_ACT_BYTE] = std::isnan(this->current_temperature)
      ? 0x11 : static_cast<uint8_t>(std::clamp(lround(this->current_temperature - 4), 0L, 255L));

  packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
  packet.insert(packet.begin(), protocol::REPORT_SHORT_DATA_LEN + 2);
  uint8_t checksum = 0;
  for (const auto byte : packet) checksum += byte;
  packet.push_back(checksum);
  packet.insert(packet.begin(), protocol::SYNC);
  packet.insert(packet.begin(), protocol::SYNC);
  this->write_array(packet);
  this->last_packet_sent_ = millis();
  this->wait_response_ = true;
  this->update_ = Update::None;
}

}  // namespace esphome::sinclair_ac::CNT
