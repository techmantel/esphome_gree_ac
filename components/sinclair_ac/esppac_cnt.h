#pragma once

#include "esppac.h"
#include <cmath>

namespace esphome::sinclair_ac::CNT {

class SinclairACCNT : public SinclairAC {
 public:
  void setup() override;
  void loop() override;
  void control(const climate::ClimateCall &call) override;

 private:
  enum class Update { None, Pending };
  Update update_{Update::None};
  bool ready_{false};
  climate::ClimateMode reported_mode_{climate::CLIMATE_MODE_COOL};
  bool reported_power_{false};
  float reported_target_{NAN};
  climate::ClimateSwingMode reported_swing_{climate::CLIMATE_SWING_OFF};

  bool verify_packet() const;
  void handle_packet();
  void process_report();
  void send_pending();
  void send_full_packet();
  void send_short_packet();
  bool is_short_report() const;
};

}  // namespace esphome::sinclair_ac::CNT
