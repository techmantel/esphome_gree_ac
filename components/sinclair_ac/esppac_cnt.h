#pragma once

#include "esppac.h"
#include <cmath>

namespace esphome::sinclair_ac::CNT {

// Offsets apply after the 2-byte sync, length, command, and checksum have
// been removed from a frame.
namespace protocol {
constexpr uint8_t SYNC = 0x7E;
constexpr uint8_t CMD_IN_UNIT_REPORT = 0x31;
constexpr uint8_t CMD_OUT_PARAMS_SET = 0x01;

constexpr uint8_t REPORT_SHORT_DATA_LEN = 33;
constexpr uint8_t REPORT_SHORT_MODE_BYTE = 2;
constexpr uint8_t REPORT_SHORT_MODE_COOL = 1;
constexpr uint8_t REPORT_SHORT_MODE_DRY = 2;
constexpr uint8_t REPORT_SHORT_MODE_FAN = 3;
constexpr uint8_t REPORT_SHORT_MODE_AUTO_HEAT = 4;
constexpr uint8_t REPORT_SHORT_FAN_SPD1_BYTE = 3;
constexpr uint8_t REPORT_SHORT_HSWING_BYTE = 4;
constexpr uint8_t REPORT_SHORT_VSWING_BYTE = 5;
constexpr uint8_t REPORT_SHORT_VSWING_MASK = 0x0F;
constexpr uint8_t REPORT_SHORT_VSWING_LAST = 0;
constexpr uint8_t REPORT_SHORT_VSWING_AUTO = 1;
constexpr uint8_t REPORT_SHORT_VSWING_MID = 4;
constexpr uint8_t REPORT_SHORT_TEMP_SET_LO_BYTE = 6;
constexpr uint8_t REPORT_SHORT_TEMP_SET_HI_BYTE = 7;
constexpr uint8_t REPORT_SHORT_TEMP_ACT_BYTE = 16;
constexpr uint8_t REPORT_SHORT_TEMP_ACT_OFF = 4;
constexpr uint8_t REPORT_SHORT_PWR_BYTE = 15;
constexpr uint8_t REPORT_SHORT_PWR_MASK = 0x02;
constexpr uint8_t REPORT_SHORT_PWR_BASE = 0x14;
constexpr uint8_t REPORT_SHORT_TURBO_MASK = 0x20;
constexpr uint8_t SET_SHORT_TRANSITION_BYTE = 1;
constexpr uint8_t SET_SHORT_TRANSITION_VAL = 0x01;

constexpr uint8_t REPORT_PWR_BYTE = 4;
constexpr uint8_t REPORT_PWR_MASK = 0x80;
constexpr uint8_t REPORT_MODE_BYTE = 6;
constexpr uint8_t REPORT_MODE_MASK = 0x70;
constexpr uint8_t REPORT_MODE_POS = 4;
constexpr uint8_t REPORT_MODE_AUTO = 0;
constexpr uint8_t REPORT_MODE_COOL = 1;
constexpr uint8_t REPORT_MODE_DRY = 2;
constexpr uint8_t REPORT_MODE_FAN = 3;
constexpr uint8_t REPORT_FAN_SPD1_BYTE = 7;
constexpr uint8_t REPORT_FAN_SPD1_MASK = 0x07;
constexpr uint8_t REPORT_FAN_TURBO_BYTE = 6;
constexpr uint8_t REPORT_FAN_TURBO_MASK = 0x01;
constexpr uint8_t REPORT_TEMP_SET_LO_BYTE = 10;
constexpr uint8_t REPORT_TEMP_SET_HI_BYTE = 11;
constexpr uint8_t REPORT_TEMP_SET_HI_MASK = 0x01;
constexpr uint16_t REPORT_TEMP_SET_RAW_BASE = 0x00A0;
constexpr uint8_t REPORT_TEMP_SET_C_BASE = 16;
constexpr uint8_t REPORT_TEMP_SET_RAW_STEP = 10;
constexpr uint8_t REPORT_TEMP_ACT_BYTE = 19;
constexpr uint8_t REPORT_TEMP_ACT_OFF = 16;
constexpr float REPORT_TEMP_ACT_DIV = 2.0f;
constexpr uint8_t REPORT_VSWING_BYTE = 9;
constexpr uint8_t REPORT_VSWING_MASK = 0xF0;
constexpr uint8_t REPORT_VSWING_POS = 4;

constexpr uint8_t SET_PACKET_LEN = 45;
constexpr uint8_t SET_CONST_02_BYTE = 39;
constexpr uint8_t SET_CONST_02_VAL = 0x02;
constexpr uint8_t SET_AF_BYTE = 3;
constexpr uint8_t SET_AF_VAL = 0xAF;
constexpr uint8_t SET_NOCHANGE_BYTE = 11;
constexpr uint8_t SET_NOCHANGE_MASK = 0x08;
constexpr uint8_t SET_CONST_BIT_BYTE = 7;
constexpr uint8_t SET_CONST_BIT_MASK = 0x02;

constexpr uint8_t FAN_AUTO = 1;
constexpr uint8_t FAN_LOW = 2;
constexpr uint8_t FAN_MEDIUM = 4;
constexpr uint8_t FAN_HIGH = 6;
}  // namespace protocol

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
