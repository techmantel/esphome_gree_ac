// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esppac.h"
#include <array>

#if defined(USE_ESP8266)
class WiFiServer;
namespace esp8266webserver {
template<typename ServerType>
class ESP8266WebServerTemplate;
}
using ESP8266WebServer = esp8266webserver::ESP8266WebServerTemplate<WiFiServer>;
#elif defined(USE_ESP32)
class WebServer;
#endif

namespace esphome {
namespace sinclair_ac {
namespace CNT {

enum class ACState {
    Initializing, /* no data for quite a long time */
    Ready,        /* AC talking to us */
};

enum class ACUpdate {
    NoUpdate,    /* no parameters changed - normally process data, static flag set */
    UpdateStart, /* start update with 0xAF and cleared static flag */
    UpdateClear, /* update without 0xAF and cleared static flag */
};

namespace protocol {
    /* SYNC */
    static const uint8_t SYNC                = 0x7E;
    /* packet types */
    static const uint8_t CMD_IN_UNIT_REPORT  = 0x31;
    static const uint8_t CMD_OUT_PARAMS_SET  = 0x01;
    static const uint8_t CMD_OUT_SYNC_TIME   = 0x03;
    static const uint8_t CMD_OUT_MAC_REPORT  = 0x04; /* 7e 7e 0d 04 04 00 00 00 AA BB CC DD EE FF 00 -> AA BB CC DD EE FF = MAC address */
    static const uint8_t CMD_OUT_UNKNOWN_1   = 0x02; /* 7e 7e 10 02 00 00 00 00 00 00 01 00 28 1e 19 23 23 00 b8 */
    static const uint8_t CMD_IN_UNKNOWN_1    = 0x44; /* 7e 7e 1a 44 01 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 */
    static const uint8_t CMD_IN_UNKNOWN_2    = 0x33; /* 7e 7e 2f 33 00 00 40 00 09 20 19 0a 00 10 00 14 17 5b 08 08 00 00 00 00 00 00 00 00 01 00 00 0d 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 */

    /* byte indexes are AFTER we remove first 4 bytes from the packet (sync, length, type) as well as a checksum */
    /* unit report packet data fields, for binary values there is no need to define bit offset/position */
    static const uint8_t REPORT_PWR_BYTE       = 4;
    static const uint8_t REPORT_PWR_MASK       = 0b10000000;

    static const uint8_t REPORT_SHORT_DATA_LEN = 33;
    static const uint8_t REPORT_SHORT_PWR_BYTE = 15;
    static const uint8_t REPORT_SHORT_PWR_MASK = 0b00000010;
    static const uint8_t REPORT_SHORT_MODE_BYTE = 2;
    static const uint8_t REPORT_SHORT_MODE_COOL = 1;
    static const uint8_t REPORT_SHORT_MODE_DRY  = 2;
    static const uint8_t REPORT_SHORT_MODE_FAN  = 3;
    static const uint8_t REPORT_SHORT_MODE_AUTO_HEAT = 4;
    static const uint8_t REPORT_SHORT_MODE_HEAT = 8;
    static const uint8_t REPORT_SHORT_FAN_SPD1_BYTE = 3;
    static const uint8_t REPORT_SHORT_TEMP_SET_LO_BYTE = 6;
    static const uint8_t REPORT_SHORT_TEMP_SET_HI_BYTE = 7;
    static const uint8_t REPORT_SHORT_TEMP_ACT_BYTE = 16;
    static const uint8_t REPORT_SHORT_TEMP_ACT_OFF = 4;
    static const uint8_t REPORT_SHORT_HSWING_BYTE = 4;
    static const uint8_t REPORT_SHORT_VSWING_BYTE = 5;
    static const uint8_t REPORT_SHORT_PWR_BASE = 0x14;
    static const uint8_t REPORT_SHORT_TURBO_MASK = 0x20;
    static const uint8_t REPORT_SHORT_HEAT_MASK = 0x08;

    static const uint8_t REPORT_MODE_BYTE      = 6;
    static const uint8_t REPORT_MODE_MASK      = 0b01110000;
    static const uint8_t REPORT_MODE_POS       = 4;
    static const uint8_t REPORT_MODE_AUTO          = 0;
    static const uint8_t REPORT_MODE_COOL          = 1;
    static const uint8_t REPORT_MODE_DRY           = 2;
    static const uint8_t REPORT_MODE_FAN           = 3;
    static const uint8_t REPORT_MODE_HEAT          = 4;

    static const uint8_t REPORT_FAN_SPD1_BYTE  = 7;
    static const uint8_t REPORT_FAN_SPD1_MASK  = 0b00000111;
    static const uint8_t REPORT_FAN_SPD1_POS   = 0;
    static const uint8_t REPORT_FAN_SPD2_BYTE  = 4;
    static const uint8_t REPORT_FAN_SPD2_MASK  = 0b00000011;
    static const uint8_t REPORT_FAN_SPD2_POS   = 0;
    static const uint8_t REPORT_FAN_QUIET_BYTE = 16;
    static const uint8_t REPORT_FAN_QUIET_MASK = 0b00001000;
    static const uint8_t REPORT_FAN_TURBO_BYTE = 6;
    static const uint8_t REPORT_FAN_TURBO_MASK = 0b00000001;

    static const uint8_t REPORT_TEMP_SET_LO_BYTE   = 10;
    static const uint8_t REPORT_TEMP_SET_HI_BYTE   = 11;
    static const uint8_t REPORT_TEMP_SET_HI_MASK   = 0b00000001;
    static const uint16_t REPORT_TEMP_SET_RAW_BASE = 0x00A0;
    static const uint8_t REPORT_TEMP_SET_C_BASE    = 16; /* celsius represented by RAW_BASE */
    static const uint8_t REPORT_TEMP_SET_RAW_STEP  = 10; /* raw units per 1C */

    static const uint8_t REPORT_TEMP_ACT_BYTE  = 19;
    static const uint8_t REPORT_TEMP_ACT_MASK  = 0b11111111;
    static const uint8_t REPORT_TEMP_ACT_POS   = 0;
    static const uint8_t REPORT_TEMP_ACT_OFF   = 16;  /* temperature offset from value in packet */
    static const float   REPORT_TEMP_ACT_DIV   = 2.0; /* temperature divider from value in packet */

    static const uint8_t REPORT_HSWING_BYTE    = 8;
    static const uint8_t REPORT_HSWING_MASK    = 0b00000111;
    static const uint8_t REPORT_HSWING_POS     = 0;
    static const uint8_t REPORT_HSWING_OFF         = 0;
    static const uint8_t REPORT_HSWING_FULL        = 1;
    static const uint8_t REPORT_HSWING_CLEFT       = 2;
    static const uint8_t REPORT_HSWING_CMIDL       = 3;
    static const uint8_t REPORT_HSWING_CMID        = 4;
    static const uint8_t REPORT_HSWING_CMIDR       = 5;
    static const uint8_t REPORT_HSWING_CRIGHT      = 6;

    static const uint8_t REPORT_VSWING_BYTE    = 9;
    static const uint8_t REPORT_VSWING_MASK    = 0b11110000;
    static const uint8_t REPORT_VSWING_POS     = 4;
    static const uint8_t REPORT_VSWING_OFF         = 0;
    static const uint8_t REPORT_VSWING_FULL        = 1;
    static const uint8_t REPORT_VSWING_CUP         = 2;
    static const uint8_t REPORT_VSWING_CMIDU       = 3;
    static const uint8_t REPORT_VSWING_CMID        = 4;
    static const uint8_t REPORT_VSWING_CMIDD       = 5;
    static const uint8_t REPORT_VSWING_CDOWN       = 6;
    static const uint8_t REPORT_VSWING_DOWN        = 7;
    static const uint8_t REPORT_VSWING_MIDD        = 8;
    static const uint8_t REPORT_VSWING_MID         = 9;
    static const uint8_t REPORT_VSWING_MIDU        = 10;
    static const uint8_t REPORT_VSWING_UP          = 11;

    static const uint8_t REPORT_DISP_ON_BYTE   = 6;
    static const uint8_t REPORT_DISP_ON_MASK   = 0b00000010;
    static const uint8_t REPORT_DISP_MODE_BYTE = 9;
    static const uint8_t REPORT_DISP_MODE_MASK = 0b00110000;
    static const uint8_t REPORT_DISP_MODE_POS  = 4;
    static const uint8_t REPORT_DISP_MODE_AUTO     = 0;
    static const uint8_t REPORT_DISP_MODE_SET      = 1;
    static const uint8_t REPORT_DISP_MODE_ACT      = 2;
    static const uint8_t REPORT_DISP_MODE_OUT      = 3;

    static const uint8_t REPORT_DISP_F_BYTE    = 7;
    static const uint8_t REPORT_DISP_F_MASK    = 0b10000000;

    static const uint8_t REPORT_PLASMA1_BYTE   = 6;
    static const uint8_t REPORT_PLASMA1_MASK   = 0b00000100;
    static const uint8_t REPORT_PLASMA2_BYTE   = 0;
    static const uint8_t REPORT_PLASMA2_MASK   = 0b00000100;

    static const uint8_t REPORT_SLEEP_BYTE     = 4;
    static const uint8_t REPORT_SLEEP_MASK     = 0b00001000;

    static const uint8_t REPORT_XFAN_BYTE      = 6;
    static const uint8_t REPORT_XFAN_MASK      = 0b00001000;

    static const uint8_t REPORT_SAVE_BYTE      = 11;
    static const uint8_t REPORT_SAVE_MASK      = 0b01000000;

    /* SET packet shares all the byte definition with REPORT */
    static const uint8_t SET_PACKET_LEN        = 45;
    static const uint8_t SET_SHORT_PACKET_LEN  = REPORT_SHORT_DATA_LEN;
    static const uint8_t SET_SHORT_TRANSITION_BYTE = 1;
    static const uint8_t SET_SHORT_TRANSITION_VAL  = 0x01;
    static const uint8_t SET_SHORT_VSWING_CONST_MASK = 0x01;
    
    static const uint8_t SET_CONST_02_BYTE     = 39;
    static const uint8_t SET_CONST_02_VAL      = 0x02;

    static const uint8_t SET_AF_BYTE           = 3;
    static const uint8_t SET_AF_VAL            = 0xAF;

    static const uint8_t SET_NOCHANGE_BYTE     = 11;
    static const uint8_t SET_NOCHANGE_MASK     = 0b00001000;

    static const uint8_t SET_CONST_BIT_BYTE    = 7;
    static const uint8_t SET_CONST_BIT_MASK    = 0b00000010;

    /* time constraints */
    static const unsigned long TIME_REFRESH_PERIOD_MS   =  300;
    static const unsigned long TIME_IDLE_POLL_PERIOD_MS = 5000;
    static const unsigned long TIME_TIMEOUT_INACTIVE_MS = 15000;
}

/* Define packets from AC that would be processed by software */
const std::vector<uint8_t> allowedPackets = {protocol::CMD_IN_UNIT_REPORT};

class SinclairACCNT : public SinclairAC {
    public:
        SinclairACCNT();

        void control(const climate::ClimateCall &call) override;

        void on_horizontal_swing_change(const std::string &swing) override;
        void on_vertical_swing_change(const std::string &swing) override;

        void on_display_change(const std::string &display) override;
        void on_display_unit_change(const std::string &display_unit) override;

        void on_plasma_change(bool plasma) override;
        void on_sleep_change(bool sleep) override;
        void on_xfan_change(bool xfan) override;
        void on_save_change(bool save) override;

        void setup() override;
        void loop() override;

        void set_debug_ui_enabled(bool enabled) { this->debug_ui_enabled_ = enabled; }
        void set_debug_ui_port(uint16_t port) { this->debug_ui_port_ = port; }

    protected:
        struct DebugPacket {
            uint32_t timestamp_ms;
            bool outgoing;
            uint8_t len;
            std::array<uint8_t, DATA_MAX> bytes;
        };

        static const uint8_t DEBUG_PACKET_HISTORY_SIZE = 32;
        static const uint8_t DEBUG_PACKET_RESPONSE_SIZE = 12;

        uint32_t last_debug_raw_sent_ = 0;
        bool debug_ui_enabled_ = false;
        uint16_t debug_ui_port_ = 8080;
        bool debug_ui_ready_ = false;
        std::array<DebugPacket, DEBUG_PACKET_HISTORY_SIZE> debug_packets_;
        uint8_t debug_packet_head_ = 0;
        uint8_t debug_packet_count_ = 0;

    #if defined(USE_ESP8266)
        ESP8266WebServer *debug_server_ = nullptr;
    #elif defined(USE_ESP32)
        WebServer *debug_server_ = nullptr;
    #endif

        ACState state_ = ACState::Initializing; /* Stores if the AC is responsive or not */
        ACUpdate update_ = ACUpdate::NoUpdate;  /* Stores if we need tu send update to AC or no */

        climate::ClimateMode mode_internal_ = climate::CLIMATE_MODE_OFF;
        bool power_internal_ = false;
        float target_temperature_reported_ = -1.0f;
        std::string fan_mode_reported_;

        std::string display_mode_internal_;
        bool display_power_internal_ = false;

        bool processUnitReport();

        void send_packet();
        void send_short_control_packet_(uint32_t now);

        bool verify_packet();
        void handle_packet();

        void init_debug_server_();
        void handle_debug_server_();
        void record_debug_packet_(const std::vector<uint8_t> &packet, bool outgoing);

        void handle_debug_root_();
        void handle_debug_status_();
        void handle_debug_packets_();
        void handle_debug_raw_send_();
        void handle_debug_control_();

        std::string json_status_();
        std::string json_packets_(uint8_t max_packets);
        std::string json_escape_(const std::string &value);
        std::string packet_to_hex_(const uint8_t *data, size_t len);
        bool parse_hex_packet_(const std::string &hex_input, std::vector<uint8_t> *packet);
        bool apply_debug_control_();

        bool is_short_report_();
        bool determine_power();
        climate::ClimateMode determine_mode();
        const char* determine_fan_mode();

        std::string determine_vertical_swing();
        std::string determine_horizontal_swing();

        std::string determine_display();
        std::string determine_display_unit();

        bool determine_plasma();
        bool determine_sleep();
        bool determine_xfan();
        bool determine_save();
};

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
