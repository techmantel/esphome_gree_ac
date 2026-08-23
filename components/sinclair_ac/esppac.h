#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include <vector>

namespace esphome::sinclair_ac {

enum SerialProcessState : uint8_t { STATE_WAIT_SYNC, STATE_RECIEVE, STATE_COMPLETE, STATE_RESTART };
struct SerialProcess {
  std::vector<uint8_t> data;
  uint8_t frame_size{0};
  SerialProcessState state{STATE_WAIT_SYNC};
};

static constexpr uint8_t MIN_TEMPERATURE = 16;
static constexpr uint8_t MAX_TEMPERATURE = 30;
static constexpr float TEMPERATURE_STEP = 1.0f;
static constexpr uint8_t TEMPERATURE_THRESHOLD = 100;

class SinclairAC : public Component, public uart::UARTDevice, public climate::Climate {
 public:
  void setup() override;
  void loop() override;

 protected:
  climate::ClimateTraits traits() override;
  void read_data();
  void update_current_temperature(float temperature);
  void update_target_temperature(float temperature);

  SerialProcess serialProcess_;
  uint32_t init_time_{0};
  uint32_t last_packet_sent_{0};
  uint32_t last_packet_received_{0};
  bool wait_response_{false};
};

}  // namespace esphome::sinclair_ac
