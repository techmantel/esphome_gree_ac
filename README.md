# ESPHOME component to support Gree/Sinclair AC units
This repository adds support for ESP32-based WiFi modules to interface with Gree/Sinclair AC units.
This generally replaces stock WiFi module, sometimes giving a little more advanced features for swing control than stock application or remote.

**USE AT YOUR OWN RISK!**

Work is still in progress!

Tested with Sinclair AC (MV-H09BIF), mostly works, sometimes need to send parameter change twice - need to investigate.

Communication protocol is based on my own reverse-engineering.

ESPHome interface/binding based on:
* https://github.com/DomiStyle/esphome-panasonic-ac

**USAGE**
* Use at your own risk!
* See: https://github.com/piotrva/esphome_gree_ac/tree/main/examples
* Create configuration file: `ac-sinclair-main.yaml`
* Configure youe ESP `board`, `uart`, optionally `status_led`, check `wifi` settings (secrets)
* Create configuration(s) for your device(s): `ac-living-room.yaml`, `ac-bedroom.yaml`
* Configure deviceid and devicename, use proper `api` and `ota` keys
* Upload initial configuration to your ESP board using USB connection
* Disconnect completely power from your AC system, follow all safety procedures, desolder original WiFI unit
* Prepare a DIY adapter to connect ESP board to the AC unit, see table and representative schematic below
* Reconnect power to your AC system.
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

**GREE LOMO 36-A (this branch)**

This branch is adapted to the Gree Lomo 36-A fan convector (cooling only, no outdoor unit) and is no longer compatible with the Sinclair units above.
Supported: power, Cool / Dry / Fan-only modes, target temperature (16-30C), fan Auto / Low / Medium / High and room temperature.
Not supported (yet): swing, X-fan, iFeel, turbo, display and the other Sinclair extras.

The unit speaks the same 38 byte frame in both directions: `7E 7E 23 <cmd> <33 data bytes> <checksum>`, where the checksum is the sum of all bytes after the two sync bytes.
Settings are sent as command `01`, the unit reports with command `31`. Data byte indexes (counted after `7E 7E 23 <cmd>`):

| Byte | Meaning                                                                  |
| ---- | ------------------------------------------------------------------------ |
| 1    | `01` in frames we send                                                   |
| 2    | mode: `01` cool, `02` dry, `03` fan only                                 |
| 3    | fan: `01` auto, `02` low, `04` medium, `06` high                         |
| 4, 5 | horizontal / vertical swing, `00` keeps the current setting              |
| 6, 7 | target temperature in tenths of a degree, 9 bits (bit 0 of byte 7 = bit 8) |
| 15   | flags, base value `14`, bit `02` = power on                              |
| 16   | room temperature - 4 (only read, never written)                          |

When nothing changes the unit is polled every 5 seconds with a fixed 50 byte Sinclair "no change" frame, which it answers with a report.
Use `level: VERBOSE` in the `logger` to see every frame.

**NOTES**
* It was reported [#1](https://github.com/piotrva/esphome_gree_ac/issues/1) that with some changes the code works with Lennox li024ci AC

**TODO**
* Support Timers - maybe unnecessray as timers can be managed by Home Assistant
* Support Time sync - maybe unnecessray as timers can be managed by Home Assistant
