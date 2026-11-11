# Issues log

Errors, bugs and problems hit while building night-sentinel, grouped by date, **newest first**. Add new entries at the top of the date they happened.

Every entry records:
- **Symptom:** what was seen (paste the exact error line).
- **Cause:** why it happened.
- **Fix:** what solved it.
- **Status:** ✅ fixed · 🟡 open or worked around · ℹ️ lesson only.

Related: setup and commands in [ESP32_SETUP_AND_COMMANDS.md](ESP32_SETUP_AND_COMMANDS.md), board facts and measured results in [results.md](results.md), adding and testing models in [MODEL_GUIDE.md](MODEL_GUIDE.md).

### Template

```markdown
### <short title>
- **Symptom:** ...
- **Cause:** ...
- **Fix:** ...
- **Status:** ✅ / 🟡 / ℹ️
```

---

## 2026-11-11

### Board reset when the model was loaded after Wi-Fi was up
- **Symptom:** pressing Start (or uploading the weights file) with Wi-Fi connected: `ESP_ERROR_CHECK failed: esp_err_t 0x101 (ESP_ERR_NO_MEM) ... func: phy_track_pll_init`, `abort() was called`, reboot. One time it survived with `min_int` = 12 bytes.
- **Cause:** building an ESP-DL model needs tens of KB of internal RAM for a moment. Before the refactor the model was always loaded at boot, before Wi-Fi took its share; the new engine could load it later, when only ~58 KB was free.
- **Fix:** the engine loads the model in `ns_infer_start()`, before Wi-Fi (`platform/infer/infer.c`). The weights file is read into PSRAM by the engine in 4 KB pieces and handed to the model as bytes. Loading later (file uploaded after boot) is still possible but tight on memory: restart the board after uploading weights.
- **Status:** ✅ fixed for the normal boot path · 🟡 late load still risky

### Lowest internal RAM fell from 18.9 KB to about 10 KB after the refactor
- **Symptom:** `/status` `min_int` 10544 right after boot (was 18944 on firmware 0.9.0); free in steady state is unchanged (~42 KB).
- **Cause:** not pinned down. The dip happens while Wi-Fi starts. New since 0.9.0: the frame hub and recorder tasks and a few more static buffers.
- **Fix:** recorder and JPEG encoder stacks moved to PSRAM; web reply buffers moved to PSRAM; `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` lowered from 4096 to 1024 (`sdkconfig.defaults`). Result: ~49 KB free while running (was 42), lowest 11–14 KB.
- **Status:** 🟡 better, still below the old 18.9 KB; watch it in the soak test

### `idf.py` run from the wrong folder left a `build/` inside a managed component
- **Symptom:** `CMake Error ... Configuring incomplete` and a new `managed_components/espressif__esp-dl/build/`.
- **Cause:** the shell was still inside `managed_components/espressif__esp-dl` when `idf.py flash` ran.
- **Fix:** deleted that `build/` folder; always `Set-Location` to the project folder before `idf.py`.
- **Status:** ℹ️ lesson

### Event photo stamped in UTC right after a reboot
- **Symptom:** `ev/E00015_20261110-184755.jpg` while the others were `..._20261111-0011xx` (IST).
- **Cause:** the clock survives a software reset (RTC), but the time zone was only set in `ns_time_start()`, after Wi-Fi. An alert in the first seconds used UTC.
- **Fix:** `setenv("TZ", CONFIG_NS_TZ)` + `tzset()` at the very start of `app_main`.
- **Status:** ✅ fixed

### NUL byte written into debug_http.c again
- **Symptom:** `grep` reported "Binary file debug_http.c matches".
- **Cause:** a Python helper wrote `'\\0'` inside a normal (non-raw) string, which became a real NUL byte in the C comment.
- **Fix:** replaced it with text. Lesson: use raw strings or plain words for `\0` in generator scripts.
- **Status:** ✅ fixed

### `-Werror=format-truncation` in the new /sd handler
- **Symptom:** `'%s' directive output may be truncated writing up to 255 bytes into a region of size 200`.
- **Cause:** FAT long file names can be 255 bytes; the line buffers were 200/300.
- **Fix:** buffers 300/420, static so they stay off the server task's stack.
- **Status:** ✅ fixed

## 2026-11-10

### Detection drops to ~4 fps while the live view is open
- **Symptom:** `T,frames=50,grab_ms=89.4,infer_ms=154.7,...,fps=4.08` with `http: stream: 2200 frames, 4.1 fps` in the log; normal is 7.9 fps, grab ~0 ms, infer 125 ms.
- **Cause:** the browser live view (`/stream`) holds camera frame buffers while it JPEG-encodes them, so the detect task waits ~90 ms for a free frame; the encoding also competes for PSRAM bandwidth (model 125 → 155 ms).
- **Fix:** none needed for deployment (debug server is off in the demo build). Close the live view during walk tests and FPS checks.
- **Status:** ℹ️ lesson only

### `gpio: conflict found for GPIO[10]` on every SD retry with no card
- **Symptom:** every 30 s: `W gpio: conflict found for GPIO[10]`, `E sdmmc_sd: send_if_cond (1) returned 0x108`, `E vfs_fat_sdmmc: esp_vfs_fat_sdspi_sdcard_init failed (0x108)`.
- **Cause:** no card inserted (0x108 = no response). The GPIO10 (SD CS) warning is the retry re-claiming a pin the failed attempt already configured.
- **Fix:** harmless; goes away once a card is in. Possible tidy-up: release the CS pin after a failed mount.
- **Status:** 🟡 open (cosmetic)

### "Walked out and back, no new alert"
- **Symptom:** a second walk-in soon after an alert gave no speaker/push.
- **Cause:** by design: after the person is gone for 3 s the FSM goes ALERT_ACTIVE → COOLDOWN for 30 s (`S,2461394,ALERT_ACTIVE,COOLDOWN`); re-detection during cooldown is only logged. There's also a cap of 10 alerts per hour.
- **Fix:** none. For walk tests wait > 30 s between walks; demo mode uses a 15 s cooldown.
- **Status:** ℹ️ lesson only

### Night detection fails beyond ~1 m (Day 4 no-go)
- **Symptom:** night person frames at <= 4 m: 1/17 with a real box >= 0.5. The raw count looked like 9/17, but those were whole-frame boxes `(0,0)-(319,239)` that also appear on 13/20 empty frames.
- **Cause:** the frames, not the model: at 0.1 lux the on-board IR lights a person well only to ~1 m. Image fixes (stretch, gamma, CLAHE, grey) didn't help, and pre-trained YOLO11n also found 0/18 people at 2.5 m+.
- **Fix:** none in software. Owner's decision (10 Nov): a dim room light stays on (never fully dark) and the camera is mounted 1–1.5 m from the door. Full Roboflow NIR run (990 close-up images) on the board: 61 % of single frames accepted at 0.6, 75 % at 0.5. No training. Details in results.md, "Day 4 night go/no-go".
- **Status:** 🟡 worked around; confirm with walk-ins under the dim light

### Board aborts when the COCO YOLO11n model loads
- **Symptom:** `ESP_ERROR_CHECK failed: esp_err_t 0x101 (ESP_ERR_NO_MEM) ... func: phy_track_pll_init`, then `abort()` and a reboot, right after `FbsLoader: The partition size is 3072 KB`.
- **Cause:** loading the 2.86 MB YOLO11n model made many small allocations in internal RAM, and Wi-Fi's next allocation failed.
- **Fix:** the experiment was dropped (it also didn't detect people at night beyond 1 m, and needs ~1 s per frame); the COCO component and its flash partition were removed again.
- **Status:** ✅ reverted

### HTTPS push fails, internal RAM drops to 1.7 KB
- **Symptom:** `/action?name=push_test` → `HTTP -1`; `/status` afterwards: `"free_int":6355,"min_int":1716`.
- **Cause:** mbedTLS allocated its TLS buffers (~16 KB in + out) from internal RAM, which Wi-Fi, the camera DMA and the task stacks had already shrunk to ~47 KB.
- **Fix:** in `sdkconfig.defaults`: `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` (TLS in PSRAM), `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`.
- **Status:** ✅ fixed: retest gave `HTTP 200 in 5470 ms`, internal RAM back to ~46 KB after the push, and the message + `E00002.jpg` attachment were on the ntfy server

### No SD card: `sdmmc_init_sd_if_cond ... returned 0x108`
- **Symptom:** at boot `E sdmmc_sd: sdmmc_init_sd_if_cond: send_if_cond (1) returned 0x108`, then `storage: no SD card (ESP_ERR_INVALID_RESPONSE)`.
- **Cause:** most likely no card in the slot (the card answered nothing valid).
- **Fix:** none needed in firmware: storage keeps running without a card and retries the mount before each write (at most every 30 s). Insert a FAT32 card to test Day 9.
- **Status:** 🟡 waiting for a card

### Build: `this statement may fall through` in intrusion_fsm.c
- **Symptom:** `intrusion_fsm.c:117:18: error: this statement may fall through [-Werror=implicit-fallthrough=]`.
- **Cause:** a deliberate fall-through from `ST_CONFIRMED` into `ST_ALERT_ACTIVE` with only a comment; ESP-IDF builds with `-Werror`.
- **Fix:** both cases share one label block (`case ST_CONFIRMED: case ST_ALERT_ACTIVE:`), no fall-through.
- **Status:** ✅ fixed

### Build: `-Werror=format-truncation` in wifi.c after switching to -O2
- **Symptom:** `wifi.c:181:65: error: '. Retrying every 30 s while ...' directive output may be truncated writing 52 bytes into a region of size between 45 and 128`.
- **Cause:** `CONFIG_COMPILER_OPTIMIZATION_PERF` (Day 4 speed settings) turns on deeper format checks; a 32-char SSID + reason text can overflow the 160-byte message.
- **Fix:** message buffer 256 bytes.
- **Status:** ✅ fixed

### serial_log.py `--reset` didn't reset the board
- **Symptom:** the log started at uptime 39 s instead of at boot.
- **Cause:** on Windows (usbser.sys) an RTS change is only sent together with a DTR write.
- **Fix:** write `port.dtr = port.dtr` after each RTS change, as esptool does.
- **Status:** ✅ fixed

### Detector self-test: one whole-frame box at 0.32 on an empty room
- **Symptom:** `detector self-test: 1 box(es) ... score 0.32 box 0,0 - 319,239`, first frame after camera start.
- **Cause:** model noise on an unsettled first frame; low score.
- **Fix:** none needed: below `ACCEPT_SCORE` 0.60, and `MAX_BOX_AREA` 0.80 rejects whole-frame boxes anyway.
- **Status:** ℹ️ lesson

### Person runs saved as `empty`
- **Symptom:** 3 of 4 night runs (person at 4.5 m facing, 3 m, 5 m side) were saved as `empty_*.jpg` with `label=empty` in `testdata/night/index.csv`.
- **Cause:** the empty-room `snap` command was reused from the terminal history without changing `--label`/`--dist`/`--pose`.
- **Fix:** renamed the 15 files to `person_<dist>m[_<pose>]_…` and corrected their `index.csv` rows. Before each `snap`, check the label matches the scene.
- **Status:** ✅ fixed

### VS Code: `command "create.espidfterminal" not found`
- **Symptom:** **ESP-IDF: Open ESP-IDF Terminal** failed with `command "create.espidfterminal" not found`.
- **Cause (likely):** VS Code was open on the repo root `D:\Projects\esp32-projects`, which isn't an ESP-IDF project after the 2026-11-06 move to the ESP32 Projects layout. The extension doesn't start there, so its commands don't exist.
- **Fix:** open `projects/night-sentinel` itself as the folder (File → Open Folder), then run the command again. If it still fails: Developer: Reload Window, and check Output → ESP-IDF. Fallback that always works: `. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"` in any PowerShell.
- **Status:** 🟡 cause not yet confirmed

## 2026-11-06

### First Wi-Fi connect after flashing times out (reason 2)
- **Symptom:** on the first boot after a flash, the station connect failed with disconnect reason 2 (`WIFI_REASON_AUTH_EXPIRE`). The automatic retry 30 s later connected normally.
- **Cause (likely):** the router dropped or never answered the first auth exchange. This is common right after a reset while the AP still holds the old association. Not seen on later boots.
- **Fix:** none needed; the retry loop recovers. If it keeps happening, shorten the first retry delay (e.g. 2 s, then back off to 30 s) so boot-to-online is faster.
- **Status:** 🟡 open, low priority; not blocking

### Night mode always on would slow the camera in daylight too
- **Symptom:** with `set_aec2(s, 1)` in `ns_cam_init()`, `/status` in 18 lux room light showed exposure 180.4 ms at gain 3.4×, steady for 30 s. That's ~5.5 fps instead of ~16.
- **Cause:** with night mode on, the sensor's auto-exposure uses the stretched frames in place of gain and keeps them while the picture looks right. When AEC2 was switched on live in the same light earlier, it stayed at 59.8 ms, so it depends on the starting state.
- **Fix:** AEC2 is off at boot (gain ceiling 16× kept). It should switch on together with IR when the LTR-308 says it's dark.
- **Status:** ✅ default fixed; automatic switching comes with the day/night logic

### At night, a person at 2.5 m isn't recognisable (M2 not met)
- **Symptom:** 0.1 lux, IR on, AEC2 (night mode) + gain ceiling 16x, 180 ms exposure. A person at ~1 m is clear, but at ~2.5 m they can't be picked out: three frames were first mislabelled as empty.
- **Cause (likely):** not enough IR light reaches 2.5 m. Gain is already at its real limit (~16x) and exposure at the night-mode maximum (3 frames). The camera was also tilted up at the ceiling, so the person was near the bottom edge of the frame.
- **Options:** re-aim the camera level and retest at 3 m; bounce IR off a lighter wall/floor; use greyscale; accept a shorter range and note it; an external 940 nm IR lamp. Day 4 will show whether the detector does better than the eye on these frames.
- **Status:** 🟡 open; carried into Day 4

### Is it safe to read the LTR-308 while the camera runs? (shared I2C bus)
- **Concern:** the LTR-308 sits on the camera's SCCB lines (GPIO 8/9). Community firmware reported bus contention.
- **Test:** `RUN_LIGHT_BUS_TEST` in `main/app_main.c`. A task grabs frames at full speed while the main task, 10× per second, reads lux plus the camera's chip ID (0x300a/0x300b) and its AE registers. That's far more I2C traffic than normal use (one read every 2–5 s). Run 1: 30 s camera only, then 30 s camera + I2C. Run 2: 10 s warm-up, then 20 s camera only, 30 s camera + I2C, 20 s camera only.
- **Result:** both runs: 300 / 300 lux reads OK, 0 chip-ID mismatches, 0 NULL frames, no `FB-SIZE` drops during the phases. Frame rate with I2C: 15.8 and 13.6 fps; camera only: 11.1–11.4 fps.
- **Why it works:** both devices use the same `i2c_master` bus handle (port 1), and the driver queues their transfers. `light.c` borrows the camera's bus handle instead of creating a second bus.
- **Status:** ✅ safe. LTR-308 chosen as the day/night signal

### Capture at 11.4 fps instead of 16.6 fps
- **Symptom:** in the bus test (~35 lux room light), camera-only capture ran at 11.1–11.4 fps. Day 2's capture test gave 16.6 fps.
- **Ruled out:** start-up settling (no change after a 10 s warm-up, or after the I2C phase); power management (`CONFIG_PM_ENABLE` off); I2C (the busy-bus phase was *faster*); register reads changing the sensor (the driver's `get_reg` only reads). The OV3660's night mode (0x3a00 bit 2) is off in the driver's init table.
- **Note:** exposure sat at its maximum (780 lines = 59.8 ms, frame length 783), so the light level is a suspect.
- **Status:** 🟡 open, not blocking (the detector needs ≥ 6 fps). Measure on Day 5

### Wi-Fi never connects: reason 201
- **Symptom:** `W (3845) wifi: disconnected (reason 201), reconnecting` repeats every ~2.4 s, then `E (16435) wifi: no IP after 15000 ms (still retrying in the background)`.
- **Cause:** 201 = `WIFI_REASON_NO_AP_FOUND`. The scan found no AP named `Airtel_Connect`. It isn't a password or auth problem: those show as 15/204, and a security mismatch shows as 210/211. The same SSID worked on 2026-10-23 (IP 192.168.1.22), so the code and SSID are fine. Something changed on the router or radio side: router off or rebooted, 2.4 GHz band turned off or renamed, channel moved, or the board is too far away. The ESP32-S3 can only use 2.4 GHz.
- **Found:** the laptop couldn't see `Airtel_Connect` either. It was on the `iPhone` hotspot (2.4 GHz, channel 6), so the router simply wasn't in range. The board was right.
- **Fix:** a Wi-Fi setup page (`components/net/wifi_portal.c`). If the saved network fails, the board opens the hotspot `NightSentinel-Setup` (password `sentinel123`). The page at `http://192.168.4.1/` lists the networks the board can see. Pick one, type its password, and the board saves it to NVS once it has actually connected. While nobody is on the hotspot, the board retries the saved network every 60 s, which covers a router that was briefly off. Tested: the board falls back to the hotspot and its scan finds 4 networks. Joining a network from the page hasn't been tested yet.
- **Follow-up:** a phone can't run its hotspot while it's joined to `NightSentinel-Setup`, so the hotspot never shows up in the list. Fix: the page now takes a typed network name, and the board retries the last typed login every 30 s while nobody is on the setup Wi-Fi. Setting up from the laptop also works, with no code change.
- **Verified:** the board joined the iPhone hotspot (172.20.10.14) through the setup page and rejoins it from the saved login on every boot.
- **Status:** ✅ fixed

### `set_gainceiling()` sets the wrong ceiling on the OV3660
- **Symptom:** found while reading the driver (`sensors/ov3660.c`), before it caused a problem.
- **Cause:** the driver writes the enum value (`GAINCEILING_16X` = 3) straight into 0x3A18/0x3A19. Those registers count in 1/16 steps, so the ceiling becomes 3/16 = 0.19×. DFRobot's web-server slider passes the same raw 0–6 values.
- **Fix:** `ns_cam_set_gain_ceiling(times)` in `components/camera/camera.c` writes `times × 16`. `/control?var=gainceiling` uses it.
- **Status:** ✅ worked around

### `cam_hal: FB-SIZE: 138240 != 153600`
- **Symptom:** one line in the log during the Wi-Fi retries.
- **Cause:** QVGA RGB565 is 320×240×2 = 153600 bytes. This frame ended at 138240 bytes (216 of 240 lines), so it was cut short. The driver drops the frame and carries on.
- **Fix:** none needed if it only happens now and then. Look into it if it shows up often.
- **Status:** ℹ️ lesson only

## 2026-10-23: Day 2, camera capture

### Finding the board's IP after a restart
- **Symptom:** off the laptop cable (no serial log), there's no way to see the IP. DHCP can change it.
- **Fix:** mDNS (`espressif/mdns` 1.14.0, `components/net/ns_mdns.c`). The board answers as `night-sentinel.local`. Tested: `http://night-sentinel.local/` returned HTTP 200 from the laptop and resolved to 192.168.1.22. As a fallback, `arp -a` and look for MAC `28-84-85-5c-6c-cc`.
- **Status:** ✅ fixed

### Horizontal lines and flicker in the browser live view
- **Symptom:** the `/stream` live view in the browser showed horizontal lines and flickered. Snapshots looked clean.
- **Step 1, anti-flicker values:** the driver's 50/60 Hz band step (98 lines) assumed its own clock. At SYSCLK 30 MHz a line is 76.7 µs, so a 10 ms band is 130 lines (60 Hz: 109). `ns_cam_init()` now writes `0x3a08/09/0e` and `0x3a0a/0b/0d`. The lines were still visible afterwards.
- **Step 2, power-noise theory** (online reports for the ESP32-CAM): tested by recording `/stream` on the PC and measuring each frame. "Row noise" is the fine row-to-row pattern in dark areas, in grey levels; flicker is the frame-to-frame brightness change.

  | Test (25 s, ~9 fps) | Row noise (median) | Flicker |
  |---|---|---|
  | Busy scene, Wi-Fi TX 20 dBm (default) | 2.42 | none (brightness std 1.3) |
  | Same scene, Wi-Fi TX 8.5 dBm (`esp_wifi_set_max_tx_power(34)`) | 2.46 | none |
  | **Plain wall**, TX 20 dBm | **0.42** | none after auto-exposure settles (148–151); bands don't move between frames |

  Cutting transmit power by more than 10× changed nothing. On a plain wall there are no lines in the frames the board sends. The busy-scene score came from real texture (the router's ridges, ports and LEDs), so power noise isn't the cause. The TX power test line was removed again.
- **Remaining suspect:** the display. The page shows 320×240 at 960 px wide with `image-rendering: pixelated`, which turns diagonal edges and JPEG blocks into visible stair-steps. Not confirmed yet. The check is to open `/snap.jpg` alone at 100% and compare.
- **Power hardware (schematic v1.1):** USB VBUS and the 2-pin `Vin 3.7~15V` connector (P4) feed the 3.3 V buck through diodes D9/D10. An external 5–12 V supply on VIN can stay connected together with the laptop's USB cable, with no back-feed into the laptop.
- **Status:** 🟡 open. The camera output is clean; the browser display is still to be checked.

### `FB-SIZE` short frames return when Wi-Fi + streaming run
- **Symptom:** with Wi-Fi on and `/stream` open, `cam_hal: FB-SIZE: 138240 != 153600` appears about once every 2 s. With Wi-Fi off there were 0 in 1,000 frames.
- **Tried:** each run was 45 s of streaming plus 5 snapshots (~57 s).

  | Change | Short frames | Stream FPS | Result |
  |---|---|---|---|
  | Baseline (camera task core 0, PSRAM 40 MHz) | ~1 per 2 s | 8.8 | clean images |
  | Camera task → core 1 (`CONFIG_CAMERA_CORE1`) | 39 | 8.7 | no real change |
  | + PSRAM 80 MHz (`CONFIG_SPIRAM_SPEED_80M`) | 23 → 16 | 9.0 | ✅ kept; PSRAM memory test passes |
  | + PSRAM DMA mode (`CONFIG_CAMERA_PSRAM_DMA`) | 0 | 7.7 | ❌ **garbage band across the bottom of some frames**, JPEGs grow to 14–35 KB. Reverted. |

- **Cause:** frames are copied from a small DMA buffer into PSRAM, while the stream's JPEG encoder reads PSRAM at the same time. In PSRAM DMA mode the driver no longer discards incomplete frames, so the corruption becomes visible instead.
- **Status:** 🟡 worked around. The remaining drops are discarded frames, never shown. Revisit when the detector replaces JPEG streaming as the main load.

### `idf.py flash` killed halfway by an output filter (tooling)
- **Symptom:** the board kept running old firmware after a "flash". `ninja: error: opening deps log: Permission denied` on the next build.
- **Cause:** `... | Select-String -Pattern "Done" | Select-Object -First 4`. `Select-String` is case-insensitive, so it matched CMake's `-- ... - done` lines. `-First 4` then stopped the pipeline after 4 matches, which killed `idf.py` mid-build and left a stuck `ninja.exe` holding the build folder.
- **Fix:** stop the stuck `ninja` process; use `Select-String -CaseSensitive` without `-First` when filtering build output.
- **Status:** ✅ fixed

### App would nearly fill the 1 MB partition once Wi-Fi is linked in
- **Symptom:** a test build with an SSID set was `0xe8700` bytes, 9% free in the default 1 MB app partition.
- **Cause:** the Wi-Fi stack, lwIP and the HTTP server add about 600 KB. With an empty SSID the compiler removes all Wi-Fi code, which hid this.
- **Fix:** custom `partitions.csv` with a 6 MB factory app, set in `sdkconfig.defaults` (and `sdkconfig`).
- **Status:** ✅ fixed

### "11.1 FPS, 0 failures" was hiding dropped frames
- **Symptom:** `cam_hal: FB-SIZE: 138240 != 153600` printed over and over. 1,207 of these appeared during one 1,000-frame test at SYSCLK 40 / PCLK 20 MHz.
- **Cause:** at 20 MHz PCLK about every other frame arrives 24 lines short (216 of 240). The driver discards short frames without returning NULL, so the test loop never saw them. The driver prints these with a bare `printf`, with no `W (...)` prefix, and the earlier log filter only searched for prefixed lines, so they went unnoticed.
- **Fix:** measured three clock settings, counting `FB-SIZE` lines this time:

  | SYSCLK / PCLK | Good FPS | Short frames |
  |---|---|---|
  | 20 / 10 MHz (default) | 5.5 | 0 |
  | **30 / 15 MHz** | **16.6** | **0** |
  | 40 / 20 MHz | 11.1 | about every other frame |

  `ns_cam_init()` now uses `s->set_pll(s, 0, 12, 1, 0, 0, 2, 1, 4)`. 16.6 FPS matches the theory exactly: a 2300 × 783-cycle frame at 30 MHz is 60 ms.
- **Lesson:** when testing the camera, count the driver's `FB-SIZE` lines as well as NULL frames.
- **Status:** ✅ fixed. 🟡 Pixel quality still needs a visual check via `/snap.jpg`.

### Wi-Fi code missing from the build
- **Symptom:** after adding the Wi-Fi component, the app grew only about 14 KB. The board logged `E wifi: no SSID: set it in idf.py menuconfig`.
- **Cause:** the SSID in `sdkconfig` was still `""`. The menuconfig change wasn't saved, or happened after the build. The compiler sees `strlen("") == 0` and removes all the Wi-Fi code.
- **Fix:** set the SSID in `idf.py menuconfig` → Component config → Night Sentinel Wi-Fi, and press `S` to save. Check with `Select-String -Path sdkconfig -Pattern "NS_WIFI_SSID"`.
- **Status:** ✅ fixed: SSID entered, the board joins `Airtel_Connect` (WPA2, RSSI −29 dBm) and gets 192.168.1.22. The first attempt after boot fails with reason 202 (auth failure) and the second succeeds about 2.5 s later, which is normal.

### Monitor holding COM5 blocks flashing
- **Symptom:** `PermissionError(13, 'Access is denied.')` / `could not open port 'COM5'`.
- **Cause:** two `idf.py -p COM5 flash monitor` sessions were still open in other terminals. Only one program can use a COM port at a time.
- **Fix:** close the monitor with `Ctrl+]` before flashing from another terminal.
- **Status:** ✅ fixed

### Faster sensor clock alone stops all frames
- **Symptom:** after adding `s->set_pll(s, 0, 8, 1, 0, 0, 0, 1, 8)`, the log showed `SYSCLK: 40000000 Hz, PCLK: 10000000 Hz`, then `W cam_hal: Failed to get frame: timeout` every 4 s. No frame arrived in 4 minutes.
- **Cause:** the sensor read out pixels twice as fast (SYSCLK), but the pixel clock carrying bytes to the ESP32 (PCLK) stayed at 10 MHz. The sensor's output buffer overflowed, so it never produced a valid frame.
- **Fix:** raise both clocks together: `s->set_pll(s, 0, 8, 1, 0, 0, 0, 1, 4)` (SYSCLK 40 MHz, PCLK 20 MHz).
- **Status:** ✅ fixed

### Camera only 5.5 FPS at QVGA RGB565
- **Symptom:** `grabbed 1000 frames in 180166 ms, 0 failures, 5 fps`. The plan's target is well above 10 FPS.
- **Cause:** the esp32-camera driver's default OV3660 clocks for RGB565 QVGA (SYSCLK 20 MHz, PCLK 10 MHz). A frame takes 2300 × 783 clock cycles, giving a 180 ms frame. The driver's "10.25 FPS" comment is wrong.
- **Ruled out:**
  - lighting: a torch in front of the lens changed nothing.
  - frame buffers: `fb_count = 3` gave the same 180.15 s.
- **Fix:** first tried `set_pll(s, 0, 8, 1, 0, 0, 0, 1, 4)` (40/20 MHz): 11.1 FPS, 0 NULL frames, but it later turned out to drop about every other frame (see "11.1 FPS, 0 failures was hiding dropped frames" above). Final setting: 30/15 MHz, 16.6 FPS.
- **Status:** ✅ fixed (superseded by the 30/15 MHz setting)

### Board disappeared: `Could not open COM5`
- **Symptom:** `A fatal error occurred: Could not open COM5, the port is busy or doesn't exist.` Only COM3/COM4 (Bluetooth) were listed. Device Manager still remembered the board (USB `303A:1001`, COM5) with status "Unknown".
- **Cause:** the board wasn't connected over USB at the time.
- **Fix:** reconnected the board, and COM5 came back. To check: `[System.IO.Ports.SerialPort]::GetPortNames()`.
- **Status:** ✅ fixed

### Link error: `multiple definition of 'cam_init'`
- **Symptom:** `cam_hal.c:537: multiple definition of 'cam_init'; ... components/camera/camera.c:15: first defined here`, then `collect2.exe: error: ld returned 1 exit status`.
- **Cause:** the esp32-camera driver already has a global function `cam_init` in its internal `cam_hal.c`. C has one global namespace for functions, so the project's `cam_init` clashed with it.
- **Fix:** renamed the project's functions to `ns_cam_init`, `ns_cam_grab`, `ns_cam_release`. The `ns_` prefix stands for night-sentinel.
- **Lesson:** give every public function in a component the same prefix.
- **Status:** ✅ fixed

### First draft of the FPS test would not compile
- **Symptom:** caught by review before building, so there was no compiler output.
- **Cause:**
  - `int_64` instead of `int64_t`.
  - `status_led_init()` / `ir_init()` were called while their `#include`s were commented out.
  - `esp_timer` was added to `components/board/CMakeLists.txt` instead of `main/CMakeLists.txt`.
- **Fix:** corrected the type, restored the includes, and moved `esp_timer` to `main`.
- **Lesson:** a requirement goes in the `CMakeLists.txt` of the component whose `.c` file has the `#include`.
- **Status:** ✅ fixed

### `main/CMakeLists.txt` has two `PRIV_REQUIRES` lines
- **Symptom:** none. It builds.
- **Cause:** `esp_timer` was added on a second `PRIV_REQUIRES` line.
- **Fix:** merged into one line: `PRIV_REQUIRES board camera esp_timer`.
- **Status:** ✅ fixed

### Own header named `esp_camera.h`
- **Symptom:** none. Caught before building.
- **Cause:** an empty `components/camera/include/esp_camera.h` had the same name as the driver's header. `#include "esp_camera.h"` could have picked up the empty file and broken the build.
- **Fix:** deleted it; the component's header is `camera.h`.
- **Status:** ✅ fixed

### `ERROR: Component "espressif/camera" not found`
- **Symptom:** `idf.py add-dependency "espressif/camera"` failed with that error.
- **Cause:** wrong component name.
- **Fix:** `idf.py add-dependency "espressif/esp32-camera"`. It resolved to 2.1.8, which pulls in `espressif/esp_jpeg` 1.3.1.
- **Status:** ✅ fixed

---

## 2026-10-03 to 2026-10-04: Day 1, toolchain and bring-up

The exact day of each entry below wasn't recorded. They come from the troubleshooting table in [ESP32_SETUP_AND_COMMANDS.md](ESP32_SETUP_AND_COMMANDS.md), section F.

### `idf.py`, `cmake`, `ninja` and the compiler all "not found"
- **Cause:** the EIM GUI was open but hadn't installed anything yet.
- **Fix:** finished the EIM install, then loaded `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`.
- **Status:** ✅ fixed

### EIM installed ESP-IDF v6.1 instead of the plan's v5.4.x
- **Cause:** EIM's default version.
- **Fix:** built and ran esp-dl `pedestrian_detect` on v6.1 with correct results (0.884 / 0.879 / 0.786, ~130 ms). Decision: stay on v6.1.
- **Status:** ✅ resolved 2026-10-04

### `IDF_TARGET is set to 'esp32'`, but the project is for `esp32s3`
- **Cause:** the VS Code ESP-IDF extension passes its own target into terminals, and it defaulted to `esp32`.
- **Fix:** **ESP-IDF: Set Espressif Device Target → esp32s3 → builtin USB-JTAG**, then open a new terminal. Quick fix for one terminal: `$env:IDF_TARGET = "esp32s3"`. Don't run `idf.py set-target esp32`.
- **Status:** ✅ fixed

### `(od)` appeared in the ESP-IDF terminal prompt
- **Cause:** VS Code's Python extension auto-activated the repo's `tools/` venv (the tools package was then named `od`; it's now `night-sentinel-tools`).
- **Fix:** use separate terminals for ESP-IDF and Python.
- **Status:** ℹ️ practice adopted

### `CMakeLists.txt not found in project directory <folder opened in VS Code>`
- **Cause:** the extension ran Set Target on a folder that wasn't an ESP-IDF project yet.
- **Fix:** open the project folder itself, which has the top-level `CMakeLists.txt`. Since the move into ESP32 Projects, that's `projects/night-sentinel`, not the repository root.
- **Status:** ✅ fixed

### `Your environment is not configured to handle Unicode characters`
- **Cause:** the Windows console codepage isn't UTF-8.
- **Fix:** `setx PYTHONUTF8 1`, then restart VS Code completely.
- **Status:** ✅ fixed

### `Detected size(16384k) larger than the size in the binary image header(2048k)`
- **Cause:** hello_world defaults to 2 MB flash.
- **Fix:** Flash size → 16 MB. The project's `sdkconfig.defaults` sets this.
- **Status:** ✅ fixed

### ESP PSRAM menu missing from menuconfig
- **Cause:** `MINIMAL_BUILD ON` hides components that `main` doesn't depend on.
- **Fix:** added `esp_psram` to `PRIV_REQUIRES` in `main/CMakeLists.txt`, then ran `idf.py reconfigure`.
- **Status:** ✅ fixed

### An edit "didn't work" (seen twice)
- **Cause:** the file wasn't saved in VS Code.
- **Fix:** `Ctrl+S`, then check with `git status`.
- **Status:** ℹ️ lesson

### IR LEDs not visible on a phone camera
- **Cause:** most phone cameras filter out 940 nm IR.
- **Fix:** use the front camera in the dark, or the board's own camera.
- **Status:** ✅ IR confirmed working (the method wasn't recorded)

### Log said "status LED OFF" while the LED was on
- **Cause:** the log text was stale after a code change.
- **Fix:** corrected in `app_main.c`.
- **Status:** ✅ fixed
