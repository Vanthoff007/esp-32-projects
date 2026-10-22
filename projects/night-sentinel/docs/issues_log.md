# Issues log

Errors, bugs and problems hit while building night-sentinel, grouped by date, **newest first**. Add new entries at the top of the date they happened.

Every entry records:
- **Symptom:** what was seen (paste the exact error line).
- **Cause:** why it happened.
- **Fix:** what solved it.
- **Status:** ✅ fixed · 🟡 open or worked around · ℹ️ lesson only.

Related: setup and commands in [ESP32_SETUP_AND_COMMANDS.md](ESP32_SETUP_AND_COMMANDS.md), board facts in [notes.md](notes.md).

### Template

```markdown
### <short title>
- **Symptom:** ...
- **Cause:** ...
- **Fix:** ...
- **Status:** ✅ / 🟡 / ℹ️
```

---

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
- **Cause:** VS Code's Python extension auto-activated the repo's `tools/` venv.
- **Fix:** use separate terminals for ESP-IDF and Python.
- **Status:** ℹ️ practice adopted

### `CMakeLists.txt not found in project directory D:\Projects\od`
- **Cause:** the extension ran Set Target on a folder that wasn't an ESP-IDF project yet.
- **Fix:** open the project folder itself. `D:\Projects\od` now has a top-level `CMakeLists.txt`.
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
