// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#pragma once

#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {

namespace sinclair_ac {

static const char *const VERSION = "0.0.1";

static const uint8_t READ_TIMEOUT = 20;  // The maximum time to wait before considering a packet complete

static const uint8_t MIN_TEMPERATURE = 16;   // Minimum temperature as reported by EWPE SMART APP
static const uint8_t MAX_TEMPERATURE = 30;   // Maximum temperature as supported by EWPE SMART APP
static const float TEMPERATURE_STEP = 1.0;   // Steps the temperature can be set in
static const float TEMPERATURE_TOLERANCE = 2;  // The tolerance to allow when checking the climate state
static const uint8_t TEMPERATURE_THRESHOLD = 100;  // Maximum temperature the AC can report (formally 119.5 for sinclair protocol, but 100 is impossible, soo...)

namespace fan_modes{
    const char* const FAN_AUTO  = "0 - Auto";
    const char* const FAN_LOW   = "2 - Low";
    const char* const FAN_MED   = "4 - Medium";
    const char* const FAN_HIGH  = "6 - High";
}

typedef enum {
        STATE_WAIT_SYNC,
        STATE_RECIEVE,
        STATE_COMPLETE,
        STATE_RESTART
} SerialProcessState_t;

static const uint8_t DATA_MAX = 200;

typedef struct {
        std::vector<uint8_t> data;
        uint8_t data_cnt;
        uint8_t frame_size;
        SerialProcessState_t state;
} SerialProcess_t;

class SinclairAC : public Component, public uart::UARTDevice, public climate::Climate {
    public:
        void set_current_temperature_sensor(sensor::Sensor *current_temperature_sensor);

        void setup() override;
        void loop() override;

    protected:
        sensor::Sensor *current_temperature_sensor_ = nullptr; /* If user wants to replace reported temperature by an external sensor readout */

        SerialProcess_t serialProcess_;

        uint32_t init_time_;   // Stores the current time
        // uint32_t last_read_;   // Stores the time at which the last read was done
        uint32_t last_packet_sent_ = 0;  // Stores the time at which the last packet was sent
        uint32_t last_packet_received_ = 0;  // Stores the time at which the last packet was received
        bool wait_response_ = false;

        climate::ClimateTraits traits() override;

        void read_data();

        void update_current_temperature(float temperature);
        void update_target_temperature(float temperature);

        climate::ClimateAction determine_action();

        void log_packet(std::vector<uint8_t> data, bool outgoing = false);
};

}  // namespace sinclair_ac
}  // namespace esphome
