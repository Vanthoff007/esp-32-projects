# Board notes

Chip: ESP32-S3, silicon rev v0.2, 2 cores
Flash: 16 MB Winbond, DIO
PSRAM: 8 MB, Octal mode (CONFIG_SPIRAM_MODE_OCT=y) - works
ESP-IDF: v6.1 (plan says v5.4.x) - confirmed OK: esp-dl pedestrian_detect example runs on v6.1,
  bundled image -> 3 boxes, scores 0.884 / 0.879 / 0.786, inference ~130 ms (2026-10-04)
Flash QIO @ 80 MHz also works (esp-dl example enabled it); our project still uses DIO
Port: COM5 (built-in USB Serial/JTAG)
Status LED: GPIO3, active-high
IR LED: GPIO47 → SY7200A EN, active-high
ESP-IDF: https://github.com/espressif/esp-idf/tree/v6.1
11.1 fps at QVGA RGB565 with SYSCLK 40 MHz / PCLK 20 MHz, 0 NULL frames, PSRAM stable. Default clocks give 5.5 fps.