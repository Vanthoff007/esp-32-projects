# night-sentinel

Night-time person alert running entirely on the DFRobot DFR1154 (ESP32-S3 AI Camera): IR camera → on-device pedestrian detection → confirmed intrusion → spoken warning, phone push, JPEG evidence on SD.

## Layout

| Path | Contents |
|------|----------|
| `main/`, `components/` | ESP-IDF firmware: board (LED, IR), camera, net (Wi-Fi + setup page, mDNS), debug_http (live view, controls) |
| `docs/` | Setup and commands, board notes, issues log |
| `resources/` | DFR1154 datasheets, schematics, dimensions |
| `tools/` | PC-side Python tools: YOLO reference detector, snapshot and Day 3 capture scripts — see [tools/README.md](tools/README.md) |
| `testdata/`, `test-results/` | Labelled test images (Day 4 input) and saved measurements |

## First run

The board has no Wi-Fi login built in. When it can't join a network, it opens the hotspot `NightSentinel-Setup` (password `sentinel123`). Join it, open `http://192.168.4.1/`, choose your 2.4 GHz network, and enter its password. After that, the camera page is at `http://night-sentinel.local/`. Details: [docs/ESP32_SETUP_AND_COMMANDS.md §4.2](docs/ESP32_SETUP_AND_COMMANDS.md#42-connect-to-wi-fi-setup-page).

## Toolchain

- ESP-IDF v6.1 (installed with EIM), target `esp32s3`
- Board flashes over the built-in USB Serial/JTAG port
- Setup, build, flash and serial commands: [docs/ESP32_SETUP_AND_COMMANDS.md](docs/ESP32_SETUP_AND_COMMANDS.md)
