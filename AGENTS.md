# AGENTS.md

Notes for coding agents: the bench, the recipes, and the constraints
that are not visible in the code.

## Bench

- Board on `/dev/cu.usbserial-210` (CH340, "USB2.0-Ser!"): Wemos D1,
  ESP8266EX, 4 MB flash, MAC `94:b9:7e:14:8a:b8`, device id `8ab8`.
  The RadSens hangs on I2C 0x66 and cannot be unplugged from here, so
  the fault path in `radsens.cpp` is verified only by reading the code.
- The board and this machine share a LAN with internet. Device:
  `gammameter-8ab8.local`, currently `192.168.1.84`. MQTT broker:
  `192.168.1.102:1883`, no auth. Never change the Mac's network settings,
  so the setup AP and the fallback mode cannot be exercised from here.
- `esptool` v5 via pipx in `~/.local/bin`; its venv
  (`~/.local/pipx/venvs/esptool/bin/python`) is the one Python here
  with pyserial. The PlatformIO penv has none.

## Recipes

Build and flash (the CH340 takes 460800):

```bash
pio run
pio run -t upload --upload-port /dev/cu.usbserial-210
```

Console: `pio device monitor` wants a TTY. The CH340 on macOS sometimes
replays its last USB packet (26-32 bytes) hundreds of times per second
when the line goes idle: a log full of identical fragments with the same
timestamp is the adapter, not the firmware. From a script, open the port
with pyserial at 115200; pulsing RTS resets the board (DTR must stay
low or it enters the bootloader). Lines start with `I (ms) tag:`; the
74880-baud bootloader garbage before them is normal. `esptool` and the
monitor cannot share the port.

```python
import serial, time
s = serial.Serial("/dev/cu.usbserial-210", 115200, timeout=0.5)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
t = time.time(); b = b""
while time.time() - t < 20:
    b += s.read(4096)
print(b.decode("utf-8", "replace"))
```

Settings change over the API (the device restarts a second later):

```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"mqtt":{"enabled":true,"host":"192.168.1.102","discovery":true}}' \
  http://192.168.1.84/api/v1/config
mosquitto_sub -h 192.168.1.102 -v -t 'gammameter/#' \
  -t 'homeassistant/sensor/gammameter_8ab8_+/config'
```

Host tests: `cd test/host && make test`. The pages' scripts can be
syntax-checked with `node --check` after cutting them out of the HTML.

## Constraints

- Arduino core 3.0.2 on NonOS SDK: one `loop()`, no tasks. Nothing may
  block longer than a few hundred ms or Wi-Fi drops and the soft
  watchdog fires. `PubSubClient::connect()` is the one blocking call
  (up to 5 s on a dead broker); it runs only while the station has an
  IP and backs off to 60 s.
- Only `wifi_mgr` calls `WiFi.*` for the station and AP. Event handlers
  set flags; mode changes and AP teardown run in `wifi_mgr_tick()`.
  `WiFi.persistent(false)` stays: credentials live in `/config.json`.
- Disconnect reason 8 (ASSOC_LEAVE) is the SDK dropping an attempt we
  replaced with `begin()`; outside normal mode it never triggers a retry.
  In normal mode every disconnect counts, whatever the reason.
- `connect_pending` stays set from `begin()` until the IP arrives or a
  real disconnect; the 20 s guard covers association plus DHCP. Clearing
  it on association made the 5 s retry restart DHCP.
- The link watchdog (`link_check()`, every 30 s in normal mode) exists
  because the bench router changed channel 5 -> 7 and the SDK kept
  reporting a live link for half an hour with no packets flowing. It
  needs the gateway to answer ARP for its own address; every IPv4
  router does.
- `import_legacy_wifi()` runs before `wifi_mgr_start()` and only when
  `/config.json` is missing. Factory reset calls `ESP.eraseConfig()`
  so the import does not bring the old network back.
- MQTT publish and subscribe only from `mqtt_mgr_tick()`. The buffer is
  1024 bytes: a discovery config is ~450. PubSubClient publishes QoS 0.
- RadSens register writes go to its EEPROM: `configure_sensor()` writes
  only what differs from the frame just read. Never write them per poll.
- `fresh.h` is the only freshness rule. History records values x10 as
  uint16, `HIST_NONE` = missing. `/history.bin` is the raw struct;
  bump `HIST_VERSION` when it changes.
- lwIP on this chip has 5 TCP control blocks and 10 segments for
  everything, MQTT included, and ESP8266WebServer serves one client at a
  time, waiting up to 2 s for a keep-alive peer to close. So every
  response carries `Connection: close` (`one_shot()`), the pages fetch
  one thing at a time, and `tcp_reaper.cpp` aborts port-80 connections
  after four retransmissions (a tab on a sleeping phone otherwise holds
  the segment pool for minutes and pages stop being sent:
  "short send after timeout"). `tcp_reaper.cpp` must not include
  ESP8266WiFi.h: its tcp_state enum clashes with wl_definitions.h.
- `/api/v1/history` is streamed in chunks; 144 records do not fit a
  JsonDocument next to ~35 KB of free heap.
- Pages are self-contained HTML, no CDN: the captive portal has no
  internet. Style: flat, one accent `#FF6F3F`, hairlines, uppercase
  section labels, no cards, no explanatory copy. Zones 20/50 uR/h.
- `appconf.cpp` and `history_ring.cpp` must stay free of Arduino
  headers; the host tests compile them with `c++`.
- SPDX header in every source file, 4 spaces, `snake_case`. Comments
  only for what the code does not say. Commits in English, body says why.
