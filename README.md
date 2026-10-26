# ESPHOME component for the Gree Lomo 36-A fan convector
This repository adds support for ESP-based WiFi modules to interface with the Gree Lomo 36-A fan convector (cooling only, no outdoor unit).
It replaces the stock WiFi module and makes the unit controllable from Home Assistant.

**USE AT YOUR OWN RISK!**

This is a fork of [piotrva/esphome_gree_ac](https://github.com/piotrva/esphome_gree_ac) (Gree/Sinclair AC units), adapted to the protocol of the Lomo 36-A.
It is no longer compatible with the Sinclair units the original was written for, use the original repository for those.

Communication protocol is based on reverse-engineering.

ESPHome interface/binding based on:
* https://github.com/piotrva/esphome_gree_ac
* https://github.com/DomiStyle/esphome-panasonic-ac

**FEATURES**
* Supported: power, Cool / Dry / Fan-only modes, target temperature (16-30C), fan Auto / Low / Medium / High
* Not supported (yet): swing, X-fan, iFeel, turbo, display
* The unit does not report the room temperature, see below how to use another sensor for it

**USAGE**
* Use at your own risk!
* See: https://github.com/techmantel/esphome_gree_ac/tree/gree-lomo-claude/examples
* Create configuration file: `ac-gree-main.yaml`
* Configure your ESP `board` and `uart` pins (the example is for an ESP-01), check `wifi` settings (secrets)
* Create configuration(s) for your device(s): `ac-living-room.yaml`, `ac-bedroom.yaml`
* Configure deviceid and devicename, use proper `api` and `ota` keys
* Upload initial configuration to your ESP board using USB connection
* Disconnect completely power from your unit, follow all safety procedures, remove the original WiFi module
* Prepare a DIY adapter to connect ESP board to the unit, see table and representative schematic below
* Reconnect power to your unit.
* Enjoy!

Generally stock WiFi module outputs UART with 3.3V signal levels and the AC unit outputs UART with 5V signal levels therefore a simple voltage divider on UART from AC unit towards ESP is usually suitable, considering very slow baudrate.
On some stock WiFi PCBs AC unit connector pins are marked on silkscreen.

| AC unit pin | Function | ESP connection        |
| ----------- | -------- | --------------------- |
| 1           | +5V      | VIN / 5V              |
| 2           | RX       | UART TX               |
| 3           | TX       | UART RX (via divider) |
| 4           | GND      | GND                   |

![Connection schematic](./images/schematic.png)

**ROOM TEMPERATURE**

The unit does not report the room temperature (byte 16 is always `11`, whatever the real temperature is).
To show it in Home Assistant, point `current_temperature_sensor` at any temperature sensor, e.g. one imported from Home Assistant:

```yaml
sensor:
  - platform: homeassistant
    id: room_temperature
    entity_id: sensor.living_room_temperature

climate:
  - platform: sinclair_ac
    name: ${devicename}
    current_temperature_sensor: room_temperature
```

**PROTOCOL**

UART 4800 baud, 8 data bits, even parity, 1 stop bit.
The unit speaks the same 38 byte frame in both directions: `7E 7E 23 <cmd> <33 data bytes> <checksum>`, where the checksum is the sum of all bytes after the two sync bytes.
Settings are sent as command `01`, the unit reports with command `31`. Data byte indexes (counted after `7E 7E 23 <cmd>`):

| Byte | Meaning                                                                    |
| ---- | -------------------------------------------------------------------------- |
| 1    | `01` in frames we send                                                     |
| 2    | mode: `01` cool, `02` dry, `03` fan only                                   |
| 3    | fan: `01` auto, `02` low, `04` medium, `06` high                           |
| 4, 5 | horizontal / vertical swing, `00` keeps the current setting                |
| 6, 7 | target temperature in tenths of a degree, 9 bits (bit 0 of byte 7 = bit 8) |
| 15   | flags, base value `14`, bit `02` = power on                                |
| 16   | unknown, always `11` (not the room temperature)                            |

When nothing changes the unit is polled every 5 seconds with a fixed 50 byte Sinclair "no change" frame, which it answers with a report.
Use `level: VERBOSE` in the `logger` to see every frame.

**TODO**
* Vertical swing
* X-fan
