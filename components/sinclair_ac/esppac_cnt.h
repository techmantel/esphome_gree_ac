// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esppac.h"

namespace esphome {
namespace sinclair_ac {
namespace CNT {

enum class ACState {
    Initializing, /* no data for quite a long time */
    Ready,        /* AC talking to us */
};

enum class ACUpdate {
    NoUpdate,    /* no parameters changed - only poll the unit for reports */
    UpdateStart, /* parameters changed - send a SET packet with the new settings */
};

namespace protocol {
    /* SYNC */
    static const uint8_t SYNC                = 0x7E;
    /* packet types */
    static const uint8_t CMD_IN_UNIT_REPORT  = 0x31;
    static const uint8_t CMD_OUT_PARAMS_SET  = 0x01;

    /* byte indexes are AFTER we remove first 4 bytes from the packet (sync, length, type) as well as a checksum */
    /* unit report packet data fields, for binary values there is no need to define bit offset/position */
    static const uint8_t REPORT_PWR_BYTE       = 15;
    static const uint8_t REPORT_PWR_MASK       = 0b00000010;

    static const uint8_t REPORT_MODE_BYTE      = 2;
    static const uint8_t REPORT_MODE_MASK      = 0b00001111;
    static const uint8_t REPORT_MODE_POS       = 0;
    static const uint8_t REPORT_MODE_COOL          = 1;
    static const uint8_t REPORT_MODE_DRY           = 2;
    static const uint8_t REPORT_MODE_FAN           = 3;

    static const uint8_t REPORT_FAN_SPD1_BYTE  = 3;
    static const uint8_t REPORT_FAN_SPD1_MASK  = 0b00000111;
    static const uint8_t REPORT_FAN_SPD1_POS   = 0;

    /* target temperature is a 9 bit value in tenths of a degree, e.g. 0x0F0 = 24.0C */
    static const uint8_t REPORT_TEMP_SET_BYTE    = 6;
    static const uint8_t REPORT_TEMP_SET_HI_BYTE = 7;
    static const uint8_t REPORT_TEMP_SET_HI_MASK = 0b00000001;
    static const float   REPORT_TEMP_SET_DIV     = 10.0; /* temperature divider from value in packet */

    /* byte 16 is always 0x11 and does not follow the room temperature, the unit does not seem to report it at all */

    /* bytes 4 and 5 hold horizontal and vertical swing, 0 = keep the current setting (swing is not supported yet) */

    /* SET packet shares all the byte definition with REPORT */
    static const uint8_t SET_PACKET_LEN        = 33;

    static const uint8_t SET_CONST_14_BYTE     = 15;
    static const uint8_t SET_CONST_14_VAL      = 0x14; /* Some always 0x14 base value of the power byte... */

    static const uint8_t SET_CONST_BIT_BYTE    = 1;
    static const uint8_t SET_CONST_BIT_MASK    = 0b00000001;

    /* Full frame (with sync, length, type and checksum) the unit answers with a report without applying any settings.
       This is the 45 byte Sinclair SET packet with only the "no change" flag and the constant bits set. */
    const std::vector<uint8_t> POLL_PACKET = {
        0x7E, 0x7E, 0x2F, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
        0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x3C,
    };

    /* time constraints */
    static const unsigned long TIME_REFRESH_PERIOD_MS   =   300;
    static const unsigned long TIME_POLL_PERIOD_MS      =  5000;
    static const unsigned long TIME_TIMEOUT_INACTIVE_MS = 15000;
}

/* Define packets from AC that would be processed by software */
const std::vector<uint8_t> allowedPackets = {protocol::CMD_IN_UNIT_REPORT};

class SinclairACCNT : public SinclairAC {
    public:
        void control(const climate::ClimateCall &call) override;

        void setup() override;
        void loop() override;

    protected:
        ACState state_ = ACState::Initializing; /* Stores if the AC is responsive or not */
        ACUpdate update_ = ACUpdate::NoUpdate;  /* Stores if we need tu send update to AC or no */

        climate::ClimateMode mode_internal_ = climate::CLIMATE_MODE_COOL;
        bool power_internal_ = false;

        bool processUnitReport();

        void send_packet();

        bool verify_packet();
        void handle_packet();

        climate::ClimateMode determine_mode();
        const char* determine_fan_mode();
};

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
