#include "esppac.h"

#include "esphome/core/log.h"

namespace esphome::sinclair_ac {

static const char *const TAG = "sinclair_ac";

climate::ClimateTraits SinclairAC::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(MIN_TEMPERATURE);
  traits.set_visual_max_temperature(MAX_TEMPERATURE);
  traits.set_visual_temperature_step(TEMPERATURE_STEP);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO,
                              climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_DRY,
                              climate::CLIMATE_MODE_FAN_ONLY});
  // Home Assistant can issue the standard fan-mode calls for these four
  // speeds. Turbo remains a custom fan mode because ESPHome has no native
  // equivalent for it.
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                  climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH});
  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL});
  return traits;
}

void SinclairAC::setup() {
  this->init_time_ = millis();
  this->last_packet_sent_ = millis();
  ESP_LOGI(TAG, "Gree Lomo AC component starting");
}

void SinclairAC::loop() { this->read_data(); }

void SinclairAC::read_data() {
  while (this->available()) {
    if (this->serialProcess_.state == STATE_COMPLETE) break;
    uint8_t byte;
    this->read_byte(&byte);
    if (this->serialProcess_.state == STATE_RESTART) {
      this->serialProcess_.data.clear();
      this->serialProcess_.state = STATE_WAIT_SYNC;
    }
    this->serialProcess_.data.push_back(byte);
    if (this->serialProcess_.data.size() >= 200) {
      this->serialProcess_.data.clear();
      this->serialProcess_.state = STATE_WAIT_SYNC;
      continue;
    }
    switch (this->serialProcess_.state) {
      case STATE_WAIT_SYNC:
        if (byte != 0x7E && this->serialProcess_.data.size() > 2 &&
            this->serialProcess_.data[this->serialProcess_.data.size() - 2] == 0x7E &&
            this->serialProcess_.data[this->serialProcess_.data.size() - 3] == 0x7E) {
          this->serialProcess_.data.clear();
          this->serialProcess_.data.push_back(0x7E);
          this->serialProcess_.data.push_back(0x7E);
          this->serialProcess_.data.push_back(byte);
          this->serialProcess_.frame_size = byte;
          this->serialProcess_.state = STATE_RECIEVE;
        }
        break;
      case STATE_RECIEVE:
        if (--this->serialProcess_.frame_size == 0) this->serialProcess_.state = STATE_COMPLETE;
        break;
      default:
        break;
    }
  }
}

void SinclairAC::update_current_temperature(float temperature) {
  if (temperature <= TEMPERATURE_THRESHOLD) this->current_temperature = temperature;
}

void SinclairAC::update_target_temperature(float temperature) {
  if (temperature <= TEMPERATURE_THRESHOLD) this->target_temperature = temperature;
}

}  // namespace esphome::sinclair_ac
