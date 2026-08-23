#include "esppac_cnt.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"

namespace esphome::sinclair_ac::CNT {

namespace {
constexpr uint8_t SYNC = 0x7E;
constexpr uint8_t REPORT = 0x31;
constexpr uint8_t SET = 0x01;
constexpr uint8_t SHORT_LEN = 33;
constexpr uint8_t FULL_LEN = 45;
constexpr uint8_t MODE_BYTE = 2;
constexpr uint8_t TRANSITION_BYTE = 1;
constexpr uint8_t FAN_BYTE = 3;
constexpr uint8_t SWING_BYTE = 5;
constexpr uint8_t TEMP_SET_LO = 6;
constexpr uint8_t TEMP_SET_HI = 7;
constexpr uint8_t POWER_BYTE = 15;
constexpr uint8_t TEMP_CURRENT = 16;
constexpr uint8_t POWER_MASK = 0x02;
constexpr uint8_t TURBO_MASK = 0x20;
constexpr uint16_t TEMP_RAW_BASE = 0xA0;
constexpr uint8_t TEMP_C_BASE = 16;
constexpr uint8_t TEMP_RAW_STEP = 10;
// The short command frame uses Gree's louvre-position values. Home
// Assistant's binary Off state is represented by the fixed middle position.
constexpr uint8_t SWING_FULL = 1;
constexpr uint8_t SWING_FIXED_MIDDLE = 4;
constexpr uint8_t MODE_COOL = 1;
constexpr uint8_t MODE_DRY = 2;
constexpr uint8_t MODE_FAN = 3;
constexpr uint8_t MODE_AUTO = 4;
constexpr uint8_t FULL_MODE_BYTE = 6;
constexpr uint8_t FULL_POWER_BYTE = 4;
constexpr uint8_t FULL_TARGET_LO = 10;
constexpr uint8_t FULL_TARGET_HI = 11;
constexpr uint8_t FULL_FAN_BYTE = 7;
constexpr uint8_t FULL_SWING_BYTE = 9;
constexpr uint8_t FULL_CURRENT_BYTE = 19;
constexpr uint8_t FULL_CONST_BYTE = 39;
constexpr uint8_t FULL_AF_BYTE = 3;
constexpr uint8_t FULL_NOCHANGE_BYTE = 11;
constexpr uint8_t FULL_CONST_BIT_BYTE = 7;
constexpr uint8_t FAN_AUTO = 1;
constexpr uint8_t FAN_LOW = 2;
constexpr uint8_t FAN_MEDIUM = 4;
constexpr uint8_t FAN_HIGH = 6;
}  // namespace

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
  if (packet.size() < 5 || packet[3] != REPORT) return false;
  uint8_t checksum = 0;
  for (size_t i = 2; i + 1 < packet.size(); i++) checksum += packet[i];
  return checksum == packet.back();
}

void SinclairACCNT::handle_packet() {
  this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4);
  this->serialProcess_.data.pop_back();
  if (this->serialProcess_.data.size() != SHORT_LEN && this->serialProcess_.data.size() != FULL_LEN) return;
  this->process_report();
}

bool SinclairACCNT::is_short_report() const { return this->serialProcess_.data.size() == SHORT_LEN; }

void SinclairACCNT::process_report() {
  const auto &data = this->serialProcess_.data;
  const bool short_report = this->is_short_report();
  this->reported_power_ = short_report ? (data[POWER_BYTE] & POWER_MASK) != 0
                                       : (data[FULL_POWER_BYTE] & 0x80) != 0;
  const uint8_t mode = short_report ? data[MODE_BYTE] : ((data[FULL_MODE_BYTE] >> 4) & 0x07);
  switch (mode) {
    case 0: this->reported_mode_ = climate::CLIMATE_MODE_AUTO; break;
    case MODE_AUTO: this->reported_mode_ = climate::CLIMATE_MODE_AUTO; break;
    case MODE_COOL: this->reported_mode_ = climate::CLIMATE_MODE_COOL; break;
    case MODE_DRY: this->reported_mode_ = climate::CLIMATE_MODE_DRY; break;
    case MODE_FAN: this->reported_mode_ = climate::CLIMATE_MODE_FAN_ONLY; break;
    default: return;
  }
  this->mode = this->reported_power_ ? this->reported_mode_ : climate::CLIMATE_MODE_OFF;
  const uint8_t fan = short_report ? (data[FAN_BYTE] & 0x07) : (data[FULL_FAN_BYTE] & 0x07);
  const bool turbo = short_report ? ((data[POWER_BYTE] & TURBO_MASK) != 0) : ((data[FULL_MODE_BYTE] & 0x01) != 0);
  if (turbo || fan == 0 || fan == FAN_HIGH) this->fan_mode = climate::CLIMATE_FAN_HIGH;
  else if (fan == FAN_AUTO) this->fan_mode = climate::CLIMATE_FAN_AUTO;
  else if (fan == FAN_LOW) this->fan_mode = climate::CLIMATE_FAN_LOW;
  else if (fan == FAN_MEDIUM) this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
  else this->fan_mode = climate::CLIMATE_FAN_AUTO;

  const uint16_t raw_target = short_report ? (data[TEMP_SET_LO] | ((data[TEMP_SET_HI] & 1) << 8))
                                           : (data[FULL_TARGET_LO] | ((data[FULL_TARGET_HI] & 1) << 8));
  this->reported_target_ = TEMP_C_BASE + static_cast<float>(raw_target - TEMP_RAW_BASE) / TEMP_RAW_STEP;
  this->update_target_temperature(this->reported_target_);
  const float current = short_report ? static_cast<float>(data[TEMP_CURRENT] + 4)
                                     : (static_cast<float>(data[FULL_CURRENT_BYTE]) - 16.0f) / 2.0f;
  this->update_current_temperature(current);
  const uint8_t swing = short_report ? (data[SWING_BYTE] & 0x0F) : ((data[FULL_SWING_BYTE] >> 4) & 0x0F);
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
  std::vector<uint8_t> packet(FULL_LEN, 0);
  const bool has_update = this->update_ == Update::Pending;
  const bool power = this->mode != climate::CLIMATE_MODE_OFF;
  const auto requested_mode = power ? this->mode : this->reported_mode_;
  uint8_t mode = 1;
  if (requested_mode == climate::CLIMATE_MODE_AUTO) mode = 0;
  else if (requested_mode == climate::CLIMATE_MODE_DRY) mode = 2;
  else if (requested_mode == climate::CLIMATE_MODE_FAN_ONLY) mode = 3;
  packet[FULL_MODE_BYTE] = static_cast<uint8_t>(mode << 4);
  packet[FULL_CONST_BYTE] = 0x02;
  packet[FULL_CONST_BIT_BYTE] |= 0x02;
  if (has_update) packet[FULL_AF_BYTE] = 0xAF;
  else packet[FULL_NOCHANGE_BYTE] |= 0x08;
  if (power) packet[FULL_POWER_BYTE] |= 0x80;
  const auto fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  if (fan == climate::CLIMATE_FAN_LOW) packet[FULL_FAN_BYTE] |= FAN_LOW;
  else if (fan == climate::CLIMATE_FAN_MEDIUM) packet[FULL_FAN_BYTE] |= FAN_MEDIUM;
  else if (fan == climate::CLIMATE_FAN_HIGH) packet[FULL_FAN_BYTE] |= FAN_HIGH;
  else packet[FULL_FAN_BYTE] |= FAN_AUTO;
  const auto raw_target = static_cast<uint16_t>(TEMP_RAW_BASE + lround((this->target_temperature - TEMP_C_BASE) * TEMP_RAW_STEP));
  packet[FULL_TARGET_LO] = raw_target & 0xFF;
  packet[FULL_TARGET_HI] = (raw_target >> 8) & 1;
  packet[FULL_SWING_BYTE] = this->swing_mode == climate::CLIMATE_SWING_VERTICAL ? 0x10 : 0x40;
  packet.insert(packet.begin(), SET);
  packet.insert(packet.begin(), FULL_LEN + 2);
  uint8_t checksum = 0;
  for (const auto byte : packet) checksum += byte;
  packet.push_back(checksum);
  packet.insert(packet.begin(), SYNC);
  packet.insert(packet.begin(), SYNC);
  this->write_array(packet);
  this->last_packet_sent_ = millis();
  this->wait_response_ = true;
  this->update_ = Update::None;
}

void SinclairACCNT::send_short_packet() {
  std::vector<uint8_t> packet(SHORT_LEN, 0);
  const bool power = this->mode != climate::CLIMATE_MODE_OFF;
  uint8_t mode = MODE_COOL;
  const auto requested_mode = power ? this->mode : this->reported_mode_;
  if (requested_mode == climate::CLIMATE_MODE_DRY) mode = MODE_DRY;
  else if (requested_mode == climate::CLIMATE_MODE_FAN_ONLY) mode = MODE_FAN;
  packet[TRANSITION_BYTE] = 0x01;
  packet[MODE_BYTE] = mode;
  packet[FAN_BYTE] = FAN_AUTO;
  const auto fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
  if (fan == climate::CLIMATE_FAN_LOW) packet[FAN_BYTE] = FAN_LOW;
  else if (fan == climate::CLIMATE_FAN_MEDIUM) packet[FAN_BYTE] = FAN_MEDIUM;
  else if (fan == climate::CLIMATE_FAN_HIGH) packet[FAN_BYTE] = FAN_HIGH;
  packet[SWING_BYTE] = this->swing_mode == climate::CLIMATE_SWING_VERTICAL ? SWING_FULL : SWING_FIXED_MIDDLE;
  const auto raw_target = static_cast<uint16_t>(TEMP_RAW_BASE +
      lround((this->target_temperature - TEMP_C_BASE) * TEMP_RAW_STEP));
  packet[TEMP_SET_LO] = raw_target & 0xFF;
  packet[TEMP_SET_HI] = (raw_target >> 8) & 1;
  packet[POWER_BYTE] = 0x14 | (power ? POWER_MASK : 0);
  packet[TEMP_CURRENT] = std::isnan(this->current_temperature)
      ? 0x11 : static_cast<uint8_t>(std::clamp(lround(this->current_temperature - 4), 0L, 255L));

  packet.insert(packet.begin(), SET);
  packet.insert(packet.begin(), SHORT_LEN + 2);
  uint8_t checksum = 0;
  for (const auto byte : packet) checksum += byte;
  packet.push_back(checksum);
  packet.insert(packet.begin(), SYNC);
  packet.insert(packet.begin(), SYNC);
  this->write_array(packet);
  this->last_packet_sent_ = millis();
  this->wait_response_ = true;
  this->update_ = Update::None;
}

}  // namespace esphome::sinclair_ac::CNT
