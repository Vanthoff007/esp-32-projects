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

Every GPIO is defined in [platform/board/include/board_pins.h](platform/board/include/board_pins.h). Pin sources and datasheets: [docs/ESP32_SETUP_AND_COMMANDS.md, Appendix A](docs/ESP32_SETUP_AND_COMMANDS.md#appendix-a-pins) and [resources/](resources/).

## Code layout

Three layers. `models/` and `app/` use `platform/`; `platform/` never uses them.

```text
main/app_main.c   start-up order only: board → camera → SD → speaker → push → recorder
                  → app → model → frame hub → Wi-Fi → clock → mDNS → web server
platform/         REUSABLE FIRMWARE: knows nothing about people or alarms
├── board/        every GPIO (board_pins.h), status LED patterns, IR LEDs
├── camera/       OV3660 setup (camera.c), light sensor (light.c), day/night switch (night.c),
│                 frame hub (frame_hub.c): one grab and one JPEG shared by everyone
├── infer/        THE CONTRACT (include/infer.h) + the engine that runs the chosen model
├── recorder/     video to the SD card (.avi, Motion-JPEG): continuous or a clip per event
├── storage/      microSD: mount, writer task, files and CSV lines
├── net/          Wi-Fi + setup page, mDNS, SNTP clock, ntfy phone push
├── alerts/       speaker: I2S + embedded 16 kHz warning clip (assets/warn.pcm)
└── debug_http/   web server: /snap.jpg, /status, /action, /sd, live stream; others add routes
models/           YOUR WORKSPACE: one folder per model (see models/README.md)
├── pedestrian_pico/   Espressif's pre-trained person detector (ESP-DL), weights on the SD card
└── dummy/             no network: pipeline test and the template for new models
app/              NIGHT SENTINEL: what this project does with the model's results
├── sentinel/     results → state machine → speaker, push, SD evidence, clip; the web page
├── intrusion/    pure-C state machine + thresholds (intrusion_config.h); also builds on the PC
└── event/        one intrusion event, shared by reference count (one JPEG per event)
host_test/        FSM unit tests and the log replay tool (built with tools/host_test.py)
```

**Trying another model touches only `models/`.** Pick it in `idf.py menuconfig` → Platform: inference → Model, put its weights on the SD card (`uv run sd.py push-model <name>`), build and flash. Step-by-step manual: [docs/MODEL_GUIDE.md](docs/MODEL_GUIDE.md).

| Task | Core | Job |
|---|---|---|
| capture | 1 | grab the newest frame → model (125 ms) → hand the results to the app (box filters → state machine → on an intrusion encode one JPEG and hand the event on). Watched by the task watchdog |
| jpeg | 0 | encode frames for `/snap.jpg`, the live stream and the recorder; idle when nobody asks |
| recorder | 0 | write video files |
| audio / notify / storage | 0 | speak the warning · HTTPS push with the JPEG (retry, then text-only) · write JPEG + CSV row (after the push result is known) |
| monitor | 0 | every 2 s: IR on below 3 lux, off above 10 lux (night mode follows IR); heartbeat every 60 s |
| led | 0 | slow flash = monitoring, solid = alert active, fast blink = error or model not loaded |

### Three independent switches

| Switch | At build time (`idf.py menuconfig`) | While running |
|---|---|---|
| Inference | Platform: inference → Model (`none` compiles it out), "Start inference at boot" | Start/Stop on the page, `/action?name=detect&arg=0` or `1` |
| Live stream | Platform: web server → Live video stream | "Allow live video" on the page, `/action?name=stream&arg=0` or `1` |
| Recording | Platform: recorder (on/off, mode at boot, fps, clip length, free space kept) | Off / Everything / Clips on the page, `/action?name=rec&arg=0`, `1` or `2` |

Any mix works. Measured: inference alone 8.0 fps; with recording 6.4 fps; with recording and a live viewer 6.0 fps.

- **Intrusion logic** (plan section 8): a box counts if score ≥ 0.60, height ≥ 10 % of the frame and area ≤ 80 %. 3 hits in 5 frames confirm; the alert fires once; no hit for 3 s → 30 s cooldown; at most 10 alerts an hour.
- **Graceful degradation:** no Wi-Fi → still detects and speaks. No SD card → the model can't be loaded, so the LED blinks fast and the page says which file is missing; camera, stream and Wi-Fi still work.
- **Camera:** QVGA 320×240 RGB565 (big-endian), 2 frame buffers in PSRAM, always the newest frame, sensor clocks 30/15 MHz. Settings explained in [docs/ESP32_SETUP_AND_COMMANDS.md, Appendix B](docs/ESP32_SETUP_AND_COMMANDS.md#appendix-b-camera-configuration).

## Software and dependencies

| Item | Version |
|---|---|
| ESP-IDF | v6.1, target `esp32s3` |
| `espressif/esp32-camera` | 2.1.8 (pulls in `espressif/esp_jpeg` 1.3.1) |
| `espressif/mdns` | 1.14.0 |
| `espressif/esp-dl` | 3.3.x: runs the `pedestrian_pico` model (Espressif's pre-trained Pico weights, 224×224) |
| `espressif/esp_new_jpeg` | 1.x: decodes JPEGs posted to `/detect` |

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

Optional: a fallback network, and the hotspot's name and password, can be set in `idf.py menuconfig` → Night Sentinel network. Other options are grouped by layer: Platform: inference / recorder / web server / camera and IR, Night Sentinel app, Night Sentinel alerts, Night Sentinel build.

5. The model's weights must be on the SD card once: `cd tools`, then `uv run sd.py push-model pedestrian_pico`.

## Web interface

No password: anyone on the same Wi-Fi can use it. Turn it off for a build that leaves the house (menuconfig → Night Sentinel build).

| URL | What it does |
|---|---|
| `/` | The page: camera view with the detection box, the three switches (inference, camera view, recording), alarm tests and volume, night-vision settings, SD card, board health, latest alert photos. Source: `app/sentinel/www/index.html` |
| `/snap.jpg` | One JPEG snapshot |
| `/status` | JSON: camera, IR, memory, Wi-Fi, SD card, model, fps, recorder, alarm state |
| `POST /detect` | Run the model on a posted JPEG, get boxes as JSON (used by `tools/eval_detector.py`) |
| `/action?name=…` | `detect` (0/1), `rec` (0 off, 1 continuous, 2 event clips), `rec_clip`, `stream` (0/1), `accept` (30–95 %, 0 = default), `alert_test`, `push_test`, `beep`, `gain`, `sd_test`, `ir_mode`, `tasks`, `reboot` |
| `/sd?path=…` | SD card: `GET` lists a folder (`name<TAB>size`) or downloads a file, `POST` uploads the request body, `DELETE` removes a file or an empty folder |
| `/control?var=NAME&val=N` | Change one camera setting (`ir`, `gainceiling`, `aec2`, …). See [Appendix B.3](docs/ESP32_SETUP_AND_COMMANDS.md#b3-live-settings-controlvarnamevaln) |
| `/reg?addr=0x3a14[&val=0x06]` | Read or write a sensor register (experiments) |
| `:81/stream` | MJPEG live stream |

## SD card from a terminal

The board serves its card over Wi-Fi; `tools/sd.py` wraps that. Needs the board online, the web server built in, and a FAT32 card in the slot.

```powershell
cd tools
uv run sd.py ls                      # the card's root
uv run sd.py ls ev                   # one folder
uv run sd.py tree rec                # every file under a folder
uv run sd.py pull events.csv         # download one file
uv run sd.py pull ev ..\sd_copy      # download a whole folder
uv run sd.py push my.espdl models/   # upload a file
uv run sd.py push-model pedestrian_pico
uv run sd.py rm rec/20261111-113533_V.avi
uv run sd.py df                      # free space
```

Add `--host 192.168.1.22` (the board's IP, from the serial log) if `night-sentinel.local` is slow to resolve.

Without the script, the same four things with `curl.exe` (plain `curl` in PowerShell 5.1 is a different command):

```powershell
curl.exe "http://night-sentinel.local/sd?path=ev"
curl.exe -o events.csv "http://night-sentinel.local/sd?path=events.csv"
curl.exe --data-binary "@my.espdl" "http://night-sentinel.local/sd?path=models/my.espdl"
curl.exe -X DELETE "http://night-sentinel.local/sd?path=rec/old.avi"
```

For many large videos, taking the card out and using a card reader is faster.

What the firmware keeps on the card: `models/` (weights), `rec/` (videos; the oldest are deleted when less than 100 MB is free), `ev/` + `events.csv` (alert photos and log), `heartbeat.csv`, `boot.csv`. It never touches anything else on the card.

## Tools (PC side)

Python scripts in [tools/](tools/), managed with uv: snapshot grabbing, labelled test-image capture, night-settings sweeps, light logging, a YOLO reference detector, and for Days 4–13: `serial_log.py` (record F/S/H/T lines), `eval_detector.py` (score test images on the board), `host_test.py` (FSM tests + replay, compiled with zig cc), `sweep.py` (threshold sweep), `plot_heartbeat.py` (soak plot), `make_audio.py` (warning clip), `sd.py` (browse the SD card), `try_model.py` (run the model on your own pictures). See [tools/README.md](tools/README.md).

Camera images (`testdata/`, `test-results/`) are kept local and are not committed; their CSV logs are.

## Documentation

Five documents in [docs/](docs/), plus two short READMEs next to the code they describe.

| File | Read it when | What it contains |
|---|---|---|
| [docs/ESP32_SETUP_AND_COMMANDS.md](docs/ESP32_SETUP_AND_COMMANDS.md) | Setting up a PC, building, flashing, or something won't connect | Installing ESP-IDF on Windows, serial ports, build/flash/monitor commands, the menuconfig menus, the expected boot log and what each warning means, Wi-Fi setup page, a quick-reference table of every command. Appendices: all pins, every camera setting and register used, C types |
| [docs/MODEL_GUIDE.md](docs/MODEL_GUIDE.md) | Trying a new model | Step by step: add a model folder, pick it in menuconfig, build and flash, put its weights on the SD card, then test it on your own pictures (`try_model.py`), on labelled sets, and live. Troubleshooting table. How to get an `.espdl` file |
| [docs/results.md](docs/results.md) | You need a number | Measured results against the targets: speed, memory, detection rates by set, alert tests, before/after the modular refactor. Ends with the board notes: chip, flash, clocks, power, light sensor and night-sweep findings |
| [docs/issues_log.md](docs/issues_log.md) | Something broke | Every problem hit, newest first, each with the exact symptom, cause, fix and status |
| `docs/next_steps.md` | Starting a work session | Where the project stands, what to do next, run sheets for the on-site tests, and the parked ideas. **Local only:** it is git-ignored, so it is not on GitHub |
| [models/README.md](models/README.md) | Looking for a model | The list of models in the repo and a one-page summary of how they plug in |
| [tools/README.md](tools/README.md) | Running a PC-side script | One section per tool: what it does, commands, where its output goes |

Datasheets, schematics and dimensions for the DFR1154 are in [resources/](resources/).

On 2026-11-11 `notes.md` was merged into `results.md` and `future.md` into `next_steps.md`.

## License

Apache-2.0, see the [repository LICENSE](../../LICENSE).
