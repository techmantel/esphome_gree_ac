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

**NOTES**
* It was reported [#1](https://github.com/piotrva/esphome_gree_ac/issues/1) that with some changes the code works with Lennox li024ci AC

## Built-in Debug Web UI (ESP-01)

You can enable a lightweight on-device debug UI from the `sinclair_ac` climate config:

```yaml
climate:
	- platform: sinclair_ac
		name: Kitchen
		debug_ui: true
		debug_ui_port: 8080
```

Then open `http://<device-ip>:8080/`.

For ESP-01/ESP8266 debugging, keep the custom debug UI enabled and remove `captive_portal:` from your YAML to avoid HTTP server conflicts.

Network access:
- If the device joins your normal Wi-Fi, open the debug UI at `http://<device-ip>:8080/`.
- If normal Wi-Fi fails and the fallback AP starts, connect to that hotspot and open `http://192.168.4.1:8080/`.

The setup lives in your YAML:
- `wifi.ssid` / `wifi.password` control the normal Wi-Fi connection.
- `wifi.ap.ssid` / `wifi.ap.password` control the fallback hotspot.
- `climate.debug_ui` and `climate.debug_ui_port` control the custom debug UI.

What it provides:
- Live status (ready/init, mode, fan, temperatures, tx/rx ages)
- Recent RX/TX packet history (ring buffer)
- Structured control form (mode/temp/fan/swing/display/switches)
- Raw packet send form (manual protocol testing)

HTTP endpoints:
- `GET /api/status`
- `GET /api/packets`
- `POST /api/control`
- `POST /api/raw`

Notes:
- Intended for local debugging. There is no auth in v1.
- Raw packet send is rate limited to the protocol refresh period.
- Structured control queues packet generation through the existing protocol flow.

**TODO**
* Support Timers - maybe unnecessray as timers can be managed by Home Assistant
* Support Time sync - maybe unnecessray as timers can be managed by Home Assistant
