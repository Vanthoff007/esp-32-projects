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
- **Fix:** `s->set_pll(s, 0, 8, 1, 0, 0, 0, 1, 4)` after `set_vflip` in `ns_cam_init()`. Result: **11.1 FPS** (90.1 s), 0 NULL frames, PSRAM unchanged.
- **Status:** ✅ fixed. 🟡 Image quality at 20 MHz PCLK isn't checked yet; check it with `/snap.jpg`.

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
