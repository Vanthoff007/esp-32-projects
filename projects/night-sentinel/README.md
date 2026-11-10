# Night Sentinel

A night-time person alert that runs entirely on a DFRobot DFR1154 (ESP32-S3 AI Camera). No video leaves the board.

```text
IR camera → on-device person detection → confirmed intrusion (3 of 5 frames)
  → spoken warning + phone push with photo + JPEG on SD + CSV log → cooldown
```

Part of [ESP32 Projects](../../README.md).

## Status

A 14-day build. The firmware for the whole alert chain is written and running (firmware 0.9.0); the tests that need a person, a speaker, an SD card or a night are still open.

| Day | Milestone | State |
|---|---|---|
| 1 | M0: toolchain and board verified | ✅ |
| 2 | M1: camera capture (QVGA RGB565, 16.6 fps) | ✅ |
| 3 | M2: IR night vision | ⚠️ a person at ~1 m is clear at 0.1 lux; at ~2.5 m not recognisable |
| 4 | M3: on-device person detection | ⚠️ works in room light and to ~1 m at night; **night no-go beyond ~1 m** with the on-board IR alone; deployed with a dim room light and a 1–1.5 m mount (see [notes](docs/notes.md#day-4-night-gono-go-2026-11-10)) |
| 5 | M4: ≥ 6 fps | ✅ 7.9 fps in room light; 5.5 fps at night (long exposure) |
| 6 | Intrusion state machine | ✅ 12/12 host tests |
| 7 | M5: speaker | 🟡 built; needs a speaker to hear |
| 8 | M6: phone push | 🟡 push with photo reaches ntfy; needs the phone app subscribed |
| 9 | M7: SD evidence | 🟡 built; needs a card |
| 10 | M8: end-to-end alert | 🟡 built and running; needs walk-in tests |
| 11–14 | tuning, night field test, soak, demo | tools ready; need on-site runs |

Measured numbers: [docs/results.md](docs/results.md). What's left and how to run it: `docs/next_steps.md` (local file). Open issues: [docs/issues_log.md](docs/issues_log.md).

## Hardware

| Part | Details |
|---|---|
| Board | DFRobot DFR1154 v1.1: ESP32-S3 (2 × 240 MHz), 16 MB flash, 8 MB octal PSRAM |
| Camera | OV3660, 160° lens, sees visible light and 940 nm IR |
| IR illuminator | 940 nm LEDs on GPIO47 (on-board) |
| Light sensor | LTR-308 on the camera's I2C bus (0x53) |
| Status LED | GPIO3 |
| Speaker | MAX98357 amp on I2S (BCLK 45, WS 46, DOUT 42), 4 Ω/8 Ω speaker on the MX1.25 socket |
| microSD | SPI mode: CS 10, MOSI 11, SCK 12, MISO 13; FAT32 |
| Power | USB-C 5 V (≥ 2 A for field tests) or VIN 3.7–15 V |

Every GPIO is defined in [components/board/include/board_pins.h](components/board/include/board_pins.h). Pin sources and datasheets: [docs/ESP32_SETUP_AND_COMMANDS.md, Appendix A](docs/ESP32_SETUP_AND_COMMANDS.md#appendix-a-pins) and [resources/](resources/).

## Architecture

```text
main/app_main.c   boot: NVS → camera → light sensor → detector → SD → speaker → push task
                  → start monitoring (0.8 s after power-on) → Wi-Fi → clock → mDNS → web server
components/
├── board/        every GPIO (board_pins.h), status LED, IR LEDs
├── camera/       OV3660 setup and night tuning (camera.c), LTR-308 light sensor (light.c)
├── detector/     C++ wrapper around esp-dl's pedestrian_detect with a C API (ns_det_run)
├── intrusion/    pure-C state machine + thresholds (intrusion_config.h); also builds on the PC
├── event/        one intrusion event, shared by reference count (one JPEG per event)
├── alerts/       speaker: I2S + embedded 16 kHz warning clip (assets/warn.pcm)
├── net/          Wi-Fi + setup page, mDNS, SNTP clock, ntfy phone push
├── storage/      microSD: JPEG per event, events.csv, heartbeat.csv, boot.csv
└── debug_http/   dev web server: live view, /snap.jpg, /status, /detect, /action test hooks
host_test/        FSM unit tests and the log replay tool (built with tools/host_test.py)
```

| Task | Core | Job |
|---|---|---|
| detect | 1 | grab the newest frame → model (125 ms) → box filters → state machine → on an intrusion encode one JPEG and hand the event on. Watched by the task watchdog |
| audio / notify / storage | 0 | speak the warning · HTTPS push with the JPEG (retry, then text-only) · write JPEG + CSV row (after the push result is known) |
| monitor | 0 | every 2 s: IR on below 3 lux, off above 10 lux (night mode follows IR); heartbeat every 60 s |
| led | 0 | slow flash = monitoring, solid = alert active, fast blink = error |

- **Intrusion logic** (plan section 8): a box counts if score ≥ 0.60, height ≥ 10 % of the frame and area ≤ 80 %. 3 hits in 5 frames confirm; the alert fires once; no hit for 3 s → 30 s cooldown; at most 10 alerts an hour.
- **Graceful degradation:** no Wi-Fi → still detects and speaks; no SD card → still detects, speaks and pushes, and retries the card every 30 s.
- **Camera:** QVGA 320×240 RGB565 (big-endian), 2 frame buffers in PSRAM, always the newest frame, sensor clocks 30/15 MHz. Settings explained in [docs/ESP32_SETUP_AND_COMMANDS.md, Appendix B](docs/ESP32_SETUP_AND_COMMANDS.md#appendix-b-camera-configuration).

## Software and dependencies

| Item | Version |
|---|---|
| ESP-IDF | v6.1, target `esp32s3` |
| `espressif/esp32-camera` | 2.1.8 (pulls in `espressif/esp_jpeg` 1.3.1) |
| `espressif/mdns` | 1.14.0 |
| `espressif/pedestrian_detect` | 0.3.2 (Pico model, 224×224, pre-trained), with `espressif/esp-dl` |

Versions are pinned in [dependencies.lock](dependencies.lock); the component manager downloads them into `managed_components/` on the first build. Board settings (flash, octal PSRAM at 80 MHz, 240 MHz CPU, 6 MB app partition, esp-dl speed settings: 64 KB data cache, 64 B lines, -O2; TLS and Wi-Fi buffers in PSRAM) are in [sdkconfig.defaults](sdkconfig.defaults) and [partitions.csv](partitions.csv).

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
3. Once online, the camera page is at **`http://night-sentinel.local/`**. The status LED flashes briefly every 2 s while monitoring.
4. Phone alerts: set a secret topic in `idf.py menuconfig` → Night Sentinel network → ntfy topic, and subscribe to it in the ntfy app.

Optional: a fallback network, and the hotspot's name and password, can be set in `idf.py menuconfig` → Night Sentinel network. App options (debug server, per-frame CSV, demo mode, IR mode and thresholds) are under → Night Sentinel app; speaker options under → Night Sentinel alerts.

## Web interface (debug)

| URL | What it does |
|---|---|
| `/` | Dashboard: camera (off / snapshot every 1 s / live stream) with the detection box drawn on top, start/stop person detection, accept-score slider, alert/speaker/push tests, volume, IR mode and camera settings, SD card space and the latest event photos, memory, reboot. Page source: `components/debug_http/www/index.html` |
| `/snap.jpg` | One JPEG snapshot |
| `/status` | JSON: IR, lux, brightness, gain, exposure, settings, plus state, fps, alerts, memory |
| `POST /detect` | Run the detector on a posted JPEG, get boxes as JSON (used by `tools/eval_detector.py`) |
| `/action?name=…` | Test hooks: `alert_test`, `push_test`, `beep`, `gain`, `sd_test`, `ir_mode`, `tasks`, `detect` (0 = stop, 1 = start person detection), `accept` (30–95 %, 0 = default), `reboot` |
| `/sd?path=…` | SD card: a folder lists `name<TAB>size`, a file downloads, e.g. `/sd?path=ev`, `/sd?path=events.csv` |
| `/control?var=NAME&val=N` | Change one setting (`ir`, `gainceiling`, `aec2`, …). See [Appendix B.3](docs/ESP32_SETUP_AND_COMMANDS.md#b3-live-settings-controlvarnamevaln) |
| `/reg?addr=0x3a14[&val=0x06]` | Read or write a sensor register (experiments) |
| `:81/stream` | MJPEG live stream |

## Tools (PC side)

Python scripts in [tools/](tools/), managed with uv: snapshot grabbing, labelled test-image capture, night-settings sweeps, light logging, a YOLO reference detector, and for Days 4–13: `serial_log.py` (record F/S/H/T lines), `eval_detector.py` (score test images on the board), `host_test.py` (FSM tests + replay, compiled with zig cc), `sweep.py` (threshold sweep), `plot_heartbeat.py` (soak plot), `make_audio.py` (warning clip). See [tools/README.md](tools/README.md).

Camera images (`testdata/`, `test-results/`) are kept local and are not committed; their CSV logs are.

## Documentation

| File | Contents |
|---|---|
| [docs/ESP32_SETUP_AND_COMMANDS.md](docs/ESP32_SETUP_AND_COMMANDS.md) | Setup, build, flash, run and debug; pins (Appendix A); camera configuration (Appendix B) |
| [docs/notes.md](docs/notes.md) | Measured board facts and test results |
| [docs/results.md](docs/results.md) | Measured results against the plan's targets |
| [docs/issues_log.md](docs/issues_log.md) | Every problem hit, by date, with cause and fix |
| [docs/future.md](docs/future.md) | Ideas parked outside the 14-day scope |
| [resources/](resources/) | DFR1154 datasheets, schematics, dimensions |

## License

Apache-2.0, see the [repository LICENSE](../../LICENSE).
