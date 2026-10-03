# night-sentinel

Night-time person alert running entirely on the DFRobot DFR1154 (ESP32-S3 AI Camera): IR camera → on-device pedestrian detection → confirmed intrusion → spoken warning, phone push, JPEG evidence on SD.

## Layout

| Path | Contents |
|------|----------|
| `docs/` | Board notes and findings |
| `resources/` | DFR1154 datasheets, schematics, dimensions |
| `tools/` | PC-side Python tools (YOLO reference detector) — see [tools/README.md](tools/README.md) |

## Toolchain

- ESP-IDF v6.1 (installed with EIM), target `esp32s3`
- Board flashes over the built-in USB Serial/JTAG port
