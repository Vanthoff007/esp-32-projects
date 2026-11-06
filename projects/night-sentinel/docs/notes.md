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

Network: Wi-Fi STA (2.4 GHz), IP 192.168.1.22 on Airtel_Connect (DHCP, 2026-10-23), mDNS name night-sentinel.local
Web: port 80 = page, /snap.jpg, /control, /status; port 81 = /stream
Wi-Fi MAC: 28:84:85:5c:6c:cc
I2C on the camera bus (GPIO 8/9, I2C port 1, new i2c_master driver), scanned after camera init 2026-11-06:
- 0x3C OV3660, 0x53 LTR-308 light sensor, part ID register 0x06 = 0xB1 (matches datasheet)
- Reading lux while the camera runs is safe: 2 x 300 reads at 10/s during full-speed capture, 0 errors,
  0 NULL frames, 0 FB-SIZE drops (bus test, issues log). Driver: components/camera/light.c, gain 9x, 100 ms
Day/night signal: LTR-308 lux (chosen 2026-11-06). In room light (~35 lux), IR on moved it only 34 -> 37 lux,
  while the camera's gain fell 8.6x -> 1.25x and exposure 60 -> 30 ms, and luma stayed flat (auto-exposure).
  So frame luma can't tell day from night once IR is on; the ALS can. Thresholds: set from night readings (Day 3 run sheet)
OV3660 default gain ceiling after init: 15x (0x3a18/19 = 0x00F8)
Night sweep 2026-11-06 (test-results/day3/tune-20261106-140725), 0.1 lux, IR on, empty room, no person:
- baseline luma 21 (gain 15x, exposure 59.8 ms = max without frame stretching): nearly black
- gain ceiling 16/32/63x: luma 26/25/29. Real gain tops out near 16x, so a higher ceiling is pointless
- ae_level +1/+2: no change (AE already at its limits). brightness +1: luma 45, but only lifts black, no new detail
- grey: luma 17, removes the IR purple tint
- AEC2 (night mode): luma 43 and real detail, but exposure 180.4 ms (3 frames), so ~5.5 fps, below the 6 fps target
- Candidate setting: gain ceiling 16x + AEC2 with exposure capped at 2 frames (~120 ms, ~8 fps)
- Boot defaults since 2026-11-06 (ns_cam_init): gain ceiling 16x, AEC2 OFF. With AEC2 on at boot the sensor held
  180 ms exposure (gain 3.4x) in 18 lux room light, i.e. ~5.5 fps in daylight, so AEC2 must follow IR (on only when dark).
  All camera settings explained: ESP32_SETUP_AND_COMMANDS.md Appendix B
- AEC2 = OV3660 night mode (0x3a00 bit 2; 0x3a -> 0x3e when on). Max night exposure is in 0x3a02/03 (60 Hz) and
  0x3a14/15 (50 Hz), both 0x0930 = 2352 lines = the 180.4 ms seen. Writing 0x061E (2 frames) sticks; whether the
  sensor obeys it in the dark is untested (test skipped 2026-11-06). Debug: GET /reg?addr=0x3a14[&val=0x06]
Night test images (testdata/night, 2026-11-06, 0.1 lux, IR on, camera tilted up at the ceiling):
- 9 x person at ~1 m (3 at default settings, 6 with AEC2 + ceiling 16x): clearly recognisable with AEC2
- 3 x person at ~2.5 m with AEC2 + 16x: NOT recognisable to a human viewer; I mistook them for empty frames
- 0 empty frames. M2 (person recognisable at 3 m below 1 lux) not met with the current IR and settings
- Earlier sweeps at 8-14 lux (13:47) and the run cut short by slow .local lookups (13:57) aren't valid night data
Capture at ~35 lux, Wi-Fi off: 11.4 fps (Day 2 measured 16.6); open, see issues log
OV3660 gain ceiling: 0x3A18/0x3A19, 10 bits in 1/16 steps (16x = 256). The driver's set_gainceiling()
  writes its enum unscaled (GAINCEILING_16X = 3 -> 0.19x), so use ns_cam_set_gain_ceiling(times)
Live AE readout: gain = 0x350A[1:0]:0x350B / 16; exposure lines = 0x3500-0x3502 (>> 4), 1 line = 76.7 us
RGB565 frames are big-endian (high byte first: RRRRRGGG GGGBBBBB)
Wi-Fi: login set from the setup page is saved in NVS namespace "ns_wifi". Setup hotspot
  "NightSentinel-Setup" / sentinel123, page http://192.168.4.1/. idf.py erase-flash clears the saved login
