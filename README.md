# Gamma Meter

ESP8266 firmware for a ClimateGuard RadSens Geiger module. Reads the
static and dynamic gamma intensity over I2C and serves them as a web
page, a JSON API and MQTT with Home Assistant discovery.

## Hardware

- Wemos D1 (ESP8266EX, 4 MB flash). Any ESP8266 board with 4 MB works
  after changing `board` in `platformio.ini`.
- ClimateGuard RadSens v1.2 on I2C address 0x66, SDA on GPIO4, SCL on
  GPIO5. The module is fed from the board and keeps counting across ESP
  resets.
- `esp8266_y_meter_box.zip` holds the STL files for the enclosure.

## Build from source

PlatformIO Core 6 with the `espressif8266` platform (Arduino core 3.0.2).

```bash
pio run
pio run -t upload --upload-port /dev/cu.usbserial-XXX
pio device monitor -b 115200
```

`tools/prebuild.py` gzips `web/*.html` into the build directory and sets
`APP_VER` from the git revision. LittleFS takes the last megabyte of
flash and holds `/config.json` and `/history.bin`; both survive a
sketch upload.

Tests:

```bash
cd test/host && make test        # appconf validation, history ring
```

## Use

With no Wi-Fi saved the device opens an access point `GammaMeter-XXXX`
(the last four hex digits of its MAC). Join it from a phone; the setup
page opens by itself on most phones, otherwise open
`http://192.168.4.1/`. Pick your network, enter the password, save. The
device restarts and answers at `http://gammameter-xxxx.local/`, or at
the IP your router gave it.

A board that ran the previous firmware keeps its Wi-Fi: the credentials
the SDK saved are imported into the settings file on the first boot.

Pages: Dashboard (both intensities with zones, two 24-hour charts),
Settings (Wi-Fi, MQTT, device name, NTP server, restart, factory
reset), Device Info.

Zones: green below 20 µR/h, yellow to 50, red above.

### API

JSON over HTTP, no authentication, `Access-Control-Allow-Origin: *`.

| Method | Path | |
| --- | --- | --- |
| GET | `/api/v1/current` | current readings, `null` when older than 30 s |
| GET | `/api/v1/history` | 144 ten-minute averages, oldest first |
| GET | `/api/v1/status` | sensor, Wi-Fi, MQTT, time, chip, firmware |
| GET | `/api/v1/config` | settings; passwords as `*_set` flags only |
| POST | `/api/v1/config` | save and restart; omitted fields keep their value |
| GET | `/api/v1/wifi/scan` | cached scan; `?refresh=1` starts a new one |
| POST | `/api/v1/reboot` | restart |
| POST | `/api/v1/factory-reset` | erase settings and restart |

```json
GET /api/v1/current
{
  "timestamp": 1792835770, "time_synced": true, "uptime_s": 62,
  "sensor": { "connected": true },
  "static": { "urh": 11.7, "age_s": 0 },
  "dynamic": { "urh": 11.8, "age_s": 0 }
}
```

### MQTT

Plain numbers to `gammameter/<id>/static` and `gammameter/<id>/dynamic`
every 30 s by default (5 to 3600 s). `gammameter/<id>/availability`
carries `online`/`offline` with a last will. With Discovery on, Home
Assistant adds one device with two sensors and gets the configs again
after it restarts. Plain MQTT only: TLS does not fit in the ESP8266 heap
next to the web server.

### Factory reset

The red button on the Settings page. Settings and the SDK's copy of the
Wi-Fi credentials are erased, history is kept, the device comes back as
the setup access point.

## Behavior worth knowing

- Static intensity is the sensor's own mean over 500 s, dynamic over
  the last two minutes. The sensor keeps both across ESP resets.
- A reading older than 30 s is not shown, published or recorded.
  Three failed polls mark the sensor disconnected; every ten failures
  in a row the I2C bus is clocked free and reinitialised. When the
  sensor answers again its HV generator, sensitivity and LED are set
  once more.
- History and MQTT readings start after the clock has synced over NTP.
- Every settings change restarts the device.
- With the saved network gone for two minutes the device opens the
  setup access point beside the station and keeps retrying once a
  minute; the AP closes as soon as a connection comes up.
- Every 30 s the device checks that the SDK still reports an IP and that
  the gateway answers ARP. A router that changes channel can leave the
  ESP8266 associated on the old one with no disconnect event; after
  three missed ARP replies the station reconnects on its own. The
  counters are in `/api/v1/status` under `wifi.link_checks` and
  `wifi.link_recoveries`.
- One HTTP client at a time: the chip has five TCP slots in total, so
  every response closes its connection and the pages fetch one thing at a
  time. Connections whose peer vanished are dropped after four
  retransmissions; the count is `device.http_dropped` in the status.
- No HTTPS, no login, no OTA; updates go over USB.

## License

MIT, see `LICENSE`.
