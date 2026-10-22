# Board notes

Board: DFRobot DFR1154, hardware revision v1.1 (schematic: resources/.../Schematic v1.1.pdf)
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
PSRAM speed: 80 MHz (CONFIG_SPIRAM_SPEED_80M), memory test passes (2026-10-23)

Camera (OV3660, SCCB 0x3C), QVGA RGB565, 2026-10-23:
- Clocks SYSCLK 30 MHz / PCLK 15 MHz via set_pll(s, 0, 12, 1, 0, 0, 2, 1, 4): 16.6 fps, 0 NULL, 0 short frames (Wi-Fi off)
- Driver default 20/10 MHz: 5.5 fps. 40/20 MHz: 11.1 fps but ~every other frame short (FB-SIZE), dropped
- Anti-flicker band step set for the 30 MHz clock: 50 Hz = 130 lines, 60 Hz = 109 lines
- With Wi-Fi + /stream: ~16 FB-SIZE drops/min (discarded, never shown). Camera task on core 1
- Stream (JPEG q80 on chip): ~9 fps, ~8.5 KB per frame
- Plain-wall test: no flicker over time, no horizontal noise lines in the frames the board sends

Power (schematic v1.1): USB-C VBUS -> diode D9, and 2-pin P4 "Vin 3.7~15V" -> diode D10,
both into the JW3651 3.3 V buck (U12). Diode-OR: both can be connected at once, the higher
voltage powers the board, no back-feed into the laptop.

Network: Wi-Fi STA (2.4 GHz), IP 192.168.1.22 (DHCP), mDNS name night-sentinel.local
Wi-Fi MAC: 28:84:85:5c:6c:cc