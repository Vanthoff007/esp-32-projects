# Night Sentinel

A night-time person alert that runs entirely on a DFRobot DFR1154 (ESP32-S3 AI Camera). No video leaves the board.

```text
IR camera → on-device person detection → confirmed intrusion (3 of 5 frames)
  → spoken warning + phone push with photo + JPEG on SD + CSV log → cooldown
```

Part of [ESP32 Projects](../../README.md).

## Status

A 14-day build, currently through **Day 3**.

| Day | Milestone | State |
|---|---|---|
| 1 | M0: toolchain and board verified | ✅ |
| 2 | M1: camera capture (QVGA RGB565, 16.6 fps) | ✅ |
| 3 | M2: IR night vision | ⚠️ closed with a gap: a person at ~1 m is clear at 0.1 lux; at ~2.5 m not yet recognisable |
| 4 | M3: on-device person detection | next |
| 5–14 | timing, intrusion logic, speaker, push, SD, integration, night field test, soak test | planned |

Built so far: camera driver and night tuning, IR control, LTR-308 light sensor, Wi-Fi with a phone setup page, mDNS, and a debug web server (live view, snapshots, camera controls). Open issues: [docs/issues_log.md](docs/issues_log.md).

## Hardware

| Part | Details |
|---|---|
| Board | DFRobot DFR1154 v1.1: ESP32-S3 (2 × 240 MHz), 16 MB flash, 8 MB octal PSRAM |
| Camera | OV3660, 160° lens, sees visible light and 940 nm IR |
| IR illuminator | 940 nm LEDs on GPIO47 (on-board) |
| Light sensor | LTR-308 on the camera's I2C bus (0x53) |
| Status LED | GPIO3 |
| Planned | MAX98357 speaker amp (I2S 45/46/42) with a 4 Ω/8 Ω speaker; microSD over SPI (CS 10, MOSI 11, MISO 13, SCK 12) |
| Power | USB-C 5 V (≥ 2 A for field tests) or VIN 3.7–15 V |

Every GPIO is defined in [components/board/include/board_pins.h](components/board/include/board_pins.h). Pin sources and datasheets: [docs/ESP32_SETUP_AND_COMMANDS.md, Appendix A](docs/ESP32_SETUP_AND_COMMANDS.md#appendix-a-pins) and [resources/](resources/).

## Architecture

```text
main/app_main.c          boot order: LED + IR → NVS → camera → light sensor → Wi-Fi → mDNS → web server
components/
├── board/               status LED and IR LEDs (plain GPIO)
├── camera/              OV3660 driver setup (camera.c) and LTR-308 light sensor (light.c)
├── net/                 Wi-Fi station + setup hotspot/page (wifi.c, wifi_portal.c), mDNS
└── debug_http/          web server: page, snapshot, controls, status (port 80); live stream (port 81)
```

- **Camera:** QVGA 320×240 RGB565, 2 frame buffers in PSRAM, always the newest frame. Sensor clocks raised to 30/15 MHz for 16.6 fps. At boot: gain ceiling 16×, night mode off. Every setting is explained in [docs/ESP32_SETUP_AND_COMMANDS.md, Appendix B](docs/ESP32_SETUP_AND_COMMANDS.md#appendix-b-camera-configuration).
- **Day/night signal:** the LTR-308 light sensor, which barely reacts to the board's own IR LEDs. It shares the camera's I2C bus; a stress test showed no interference.
- **Wi-Fi:** tries the saved network. If that fails, it opens the hotspot `NightSentinel-Setup`, whose page lists nearby networks. The working login is saved in NVS.
- **Planned (Day 4 onward):** Espressif `pedestrian_detect` (esp-dl) on core 1, an intrusion state machine, then audio, push and SD tasks on core 0.

## Software and dependencies

| Item | Version |
|---|---|
| ESP-IDF | v6.1, target `esp32s3` |
| `espressif/esp32-camera` | 2.1.8 (pulls in `espressif/esp_jpeg` 1.3.1) |
| `espressif/mdns` | 1.14.0 |
| Planned | `espressif/pedestrian_detect` 0.3.2 with `espressif/esp-dl` 3.3.x (already tested on this board: ~130 ms per frame) |

Versions are pinned in [dependencies.lock](dependencies.lock); the component manager downloads them into `managed_components/` on the first build. Board settings (flash, octal PSRAM at 80 MHz, 240 MHz CPU, 6 MB app partition) are in [sdkconfig.defaults](sdkconfig.defaults) and [partitions.csv](partitions.csv).

## Setup and build

Full step-by-step guide, including ESP-IDF installation on Windows, ports, problems and fixes: [docs/ESP32_SETUP_AND_COMMANDS.md](docs/ESP32_SETUP_AND_COMMANDS.md). Short version, in a terminal with ESP-IDF v6.1 loaded:

```powershell
cd D:\Projects\esp32-projects\projects\night-sentinel
idf.py set-target esp32s3        # first time only: creates sdkconfig from sdkconfig.defaults
idf.py build
idf.py -p COM5 flash monitor     # COM5 = this board on the development PC; Ctrl+] exits
```

In VS Code, open `projects/night-sentinel` itself (not the repository root) so the ESP-IDF extension finds the project.

## First run

1. The board has no Wi-Fi login built in. If it can't join a network, it opens the hotspot **`NightSentinel-Setup`** (password `sentinel123`).
2. Join it from a phone or laptop and open `http://192.168.4.1/`. Pick a 2.4 GHz network (or type its name) and enter the password. To use a phone's own hotspot, do this from a laptop, or see the phone-only steps in [the setup guide §4.2](docs/ESP32_SETUP_AND_COMMANDS.md#42-connect-to-wi-fi-setup-page).
3. Once online, the status LED turns on and the camera page is at **`http://night-sentinel.local/`**.

Optional: a fallback network, and the hotspot's name and password, can be set in `idf.py menuconfig` → Night Sentinel Wi-Fi.

## Web interface (debug)

| URL | What it does |
|---|---|
| `/` | Live view with IR and camera controls, plus a status line |
| `/snap.jpg` | One JPEG snapshot |
| `/status` | JSON: IR state, light-sensor lux, frame brightness, gain, exposure, settings |
| `/control?var=NAME&val=N` | Change one setting (`ir`, `gainceiling`, `aec2`, …). See [Appendix B.3](docs/ESP32_SETUP_AND_COMMANDS.md#b3-live-settings-controlvarnamevaln) |
| `/reg?addr=0x3a14[&val=0x06]` | Read or write a sensor register (experiments) |
| `:81/stream` | MJPEG live stream |

## Tools (PC side)

Python scripts in [tools/](tools/), managed with uv: snapshot grabbing, labelled test-image capture, night-settings sweeps, light logging, and a YOLO reference detector. See [tools/README.md](tools/README.md).

Camera images (`testdata/`, `test-results/`) are kept local and are not committed; their CSV logs are.

## Documentation

| File | Contents |
|---|---|
| [docs/ESP32_SETUP_AND_COMMANDS.md](docs/ESP32_SETUP_AND_COMMANDS.md) | Setup, build, flash, run and debug; pins (Appendix A); camera configuration (Appendix B) |
| [docs/notes.md](docs/notes.md) | Measured board facts and test results |
| [docs/issues_log.md](docs/issues_log.md) | Every problem hit, by date, with cause and fix |
| [resources/](resources/) | DFR1154 datasheets, schematics, dimensions |

## License

Apache-2.0, see the [repository LICENSE](../../LICENSE).
