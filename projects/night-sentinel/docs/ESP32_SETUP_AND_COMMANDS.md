# ESP32 setup and command handbook

How the night-sentinel development environment was set up, and the commands used to build, flash and talk to the board. Everything here was rebuilt from the project files, the working machine, and the records of earlier work sessions (2026-10-03 and 2026-10-04). It was last checked on 2026-10-23.

Related docs:
- [notes.md](notes.md): board facts (chip, flash, PSRAM, pins).
- [issues_log.md](issues_log.md): every error and bug hit so far, by date.
- [../README.md](../README.md): project overview.
- [../tools/README.md](../tools/README.md): the PC-side Python detector.
- `docs/next_steps.md`: session handoff and the current day's guide. **This file is local only** because it is in `.gitignore`, so it doesn't exist in a fresh clone.

## Status labels

| Label | Meaning |
|---|---|
| ✅ **Verified** | Run on this machine or board, and the output was checked. |
| ⚠️ **Hit an error** | Attempted, failed, and the fix is recorded. |
| ⏳ **Not verified** | Suggested or documented, but never run on this setup. Treat it as untested. |
| 🔥 **Destructive** | Overwrites or erases what's on the board. |

---

## A. Hardware and environment

### Board

| Item | Value | Source | Status |
|---|---|---|---|
| Board | DFRobot **DFR1154** "ESP32-S3 AI Camera Module" | `resources/`, README | ✅ |
| SoC | ESP32-S3, silicon rev **v0.2**, 2 cores, Wi-Fi/BLE | hello_world boot log | ✅ |
| Flash | **16 MB** Winbond, **DIO** mode, 80 MHz | boot log (`detected chip: winbond`, `flash io: dio`) | ✅ |
| PSRAM | **8 MB**, **Octal** mode | menuconfig test on hello_world; esp-dl example ran | ✅ |
| Camera | OV3660 (datasheets in `resources/`), SCCB address `0x3C` | boot log | ✅ captures in firmware; pins in [E.2.2](#e22-camera-ov3660) |
| Status LED | GPIO3, active-high | firmware test | ✅ |
| IR LEDs | GPIO47 → `IR_CTL` → EN of SY7200A boost LED driver (U2), active-high | schematic v1.1 + firmware | ✅ (ticked in the project tracker; how the glow was seen wasn't recorded) |
| Power LED (D8) | Wired to 3.3 V, can't be switched off in code | schematic v1.1 | from schematic |

Datasheets, schematics (v1.0, v1.1) and dimensions are in [`../resources/`](../resources/).

### Development machine

| Item | Value | Status |
|---|---|---|
| OS | Windows 11 Home (10.0.26200) | ✅ |
| Shell | Windows PowerShell 5.1 (ESP-IDF is loaded into PowerShell) | ✅ |
| Editor | VS Code + **ESP-IDF extension** (`espressif.esp-idf-extension` 2.3.0) | ✅ |
| Git | `C:\Program Files\Git\cmd\git.exe` | ✅ |
| System Python | 3.14 at `C:\Python314\python.exe` (not used by ESP-IDF) | ✅ |
| uv | `C:\Users\18arj\.local\bin\uv.exe` (only for `tools/`) | ✅ |

### USB connection

- The board connects with **one USB-C cable** to the ESP32-S3's **built-in USB Serial/JTAG** port. There is no separate USB-UART chip.
- Windows shows it as **"USB Serial Device (COM5)"** under Device Manager → Ports. ✅
- **No driver was installed.** Windows' built-in USB serial driver handled it. ✅
- COM3 and COM4 on this PC are **"Standard Serial over Bluetooth link"** ports. They are not the board, so ignore them.

### Tools and frameworks

| Tool | Used? |
|---|---|
| ESP-IDF v6.1 (installed with EIM) | ✅ Yes, the only firmware framework |
| ESP-IDF VS Code extension | ✅ Yes, for the terminal and target selection |
| esp-dl / `pedestrian_detect` | ✅ The example ran on its own; not in the project yet |
| esp32-camera | ✅ 2.1.8, added 2026-10-23. `components/camera/` uses it and captures on the board. See [E.5](#e5-camera-clocks-and-frame-rate). |
| Arduino IDE / Arduino-ESP32 | ❌ **Deliberately not installed.** DFRobot's `.ino` sketches are read only as pin references. Installing both toolchains risks running the wrong `esptool` or using the wrong port. |
| PlatformIO | ❌ Not used |

---

## B. Accessing the ESP32 terminal

All commands are PowerShell, run in an **ESP-IDF terminal** (see [C.3](#c3-opening-an-esp-idf-terminal)) unless stated otherwise.

### B.1 Find the board's serial port

**On Windows, the port is `COMx`.** Linux-style names like `/dev/ttyUSB0` (USB-UART bridge chips) or `/dev/ttyACM0` (native USB, which this board would use on Linux) **don't apply on this machine.** Everywhere this handbook says `COM5`, use the number you find here.

1. Plug the board in.
2. List the ports. Either of these works in any PowerShell, no ESP-IDF needed:

   ```powershell
   # Just the names
   [System.IO.Ports.SerialPort]::GetPortNames()

   # Names with a description: look for "USB Serial Device"
   Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\(COM\d+\)" } | Select-Object Name, DeviceID
   ```

   Expected output with the board plugged in (recorded 2026-10-03):

   ```
   Standard Serial over Bluetooth link (COM3)
   Standard Serial over Bluetooth link (COM4)
   USB Serial Device (COM5)
   ```

   ✅ Both commands verified. On 2026-10-23 the board was unplugged and only COM3/COM4 appeared, which is how you can tell the board's port apart.

3. GUI alternative: **Device Manager → Ports (COM & LPT)**. Unplug and replug the board and see which entry disappears and comes back.

### B.2 Check USB connectivity and permissions

- **Windows has no `dialout` group or `/dev` permissions.** The Linux step `sudo usermod -aG dialout $USER` doesn't apply.
- If no new COM port appears when you plug in:
  - Try another USB-C cable. Charge-only cables are common. ⏳ suggestion
  - Try another USB port on the PC. ⏳ suggestion
- If the port appears but can't be opened, another program has it open. See [B.5](#b5-troubleshooting-connections).

### B.3 Open a serial terminal (monitor)

```powershell
cd D:\Projects\od
idf.py -p COM5 monitor
```

- **What it does:** opens ESP-IDF's serial monitor. It decodes crash addresses into source file and line using `build\night-sentinel.elf`, which is why you run it from the project folder.
- **Settings:** 115 200 baud (`CONFIG_ESPTOOLPY_MONITOR_BAUD=115200` in `sdkconfig`), 8N1. On the USB Serial/JTAG port the baud rate doesn't actually limit speed.
- Usually combined with flashing: `idf.py -p COM5 flash monitor` ✅ (this is the form that was actually used).

Expected output with the current project firmware (`main/app_main.c`, the Day 2 frame-rate test, recorded 2026-10-23):

```
I (1086) camera: Detected OV3660 camera
I (1446) ov3660: Calculated VCO: 160000000 Hz, PLLCLK: 160000000 Hz, SYSCLK: 40000000 Hz, PCLK: 20000000 Hz
I (1446) camera: camera ready (sensor PID 0x3660)
I (1446) main: camera init OK
I (91576) main: grabbed 1000 frames in 90124 ms, 0 failures, 11 fps, free memory: 8077692 -> 8077692
I (91576) main_task: Returned from app_main()
```

The result line takes about 90 s to appear. After it, the board sits idle.

### B.4 Exit the monitor

Press **`Ctrl+]`**. ✅

Other monitor keys (⏳ not used so far, from idf.py monitor's built-in help):
- `Ctrl+T` then `Ctrl+R`: reset the board.
- `Ctrl+T` then `Ctrl+H`: show all monitor shortcuts.

### B.5 Troubleshooting connections

| Symptom | What to do | Status |
|---|---|---|
| No COM port for the board | Different cable or USB port; check Device Manager while replugging. | ✅ happened 2026-10-23 (only COM3/COM4 listed); reconnecting the board brought COM5 back |
| `could not open port 'COM5'` / access denied | Another program has the port: a second `idf.py monitor`, the VS Code ESP-IDF monitor, or a serial terminal app. Close it and retry. Only one program can use a COM port at a time. | ⏳ never happened |
| `Failed to connect to ESP32-S3` during flash | Put the chip in download mode by hand: **hold BOOT, tap RST, release BOOT**, then run the flash command again. | ⏳ never needed |
| Port number changed | Windows can give a different COM number on another USB port. Re-run [B.1](#b1-find-the-boards-serial-port). | ⏳ |

---

## C. Development environment setup

### C.1 ESP-IDF v6.1 (via EIM)

**Purpose:** the official Espressif framework, which includes the build system (`idf.py`, CMake, Ninja), the Xtensa compiler, `esptool` for flashing, and the serial monitor.

**How it was installed:** ✅
1. Downloaded the **Espressif Installation Manager GUI**, `eim-gui-windows-x64-v0.19.0.exe` (saved in `C:\Users\18arj\.espressif\eim_gui\`).
2. Ran it and installed ESP-IDF. It installed **v6.1**, not the v5.4.x the original build plan named. That was later tested and kept; see [F](#f-troubleshooting-and-lessons-learned).

EIM's choices for this install were not recorded, so the exact options are ⏳. When reinstalling, the build plan's guidance was:
- Pick the `esp32s3` target. You can choose only that one to save space.
- Use a short install path with no spaces, like `C:\esp`.

**Where it ended up** (recorded in `C:\Espressif\tools\eim_idf.json`): ✅

| What | Path |
|---|---|
| ESP-IDF source (`IDF_PATH`) | `C:\esp\v6.1\esp-idf` |
| Tools (`IDF_TOOLS_PATH`) | `C:\Espressif\tools` |
| ESP-IDF's own Python venv | `C:\Espressif\tools\python\v6.1\venv` |
| **Activation script** | `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` |
| Install record | `C:\Espressif\tools\eim_idf.json` |

**Tool versions** (checked 2026-10-23 after loading the activation script): ✅

| Tool | Version | Path |
|---|---|---|
| ESP-IDF | v6.1 | `C:\esp\v6.1\esp-idf` |
| Python (IDF venv) | 3.14.5 | `C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe` |
| esptool | v5.3.1 | inside the IDF venv |
| CMake | 4.0.3 | `C:\Espressif\tools\cmake\4.0.3\bin\cmake.exe` |
| Ninja | 1.12.1 | `C:\Espressif\tools\ninja\1.12.1\ninja.exe` |
| Compiler | `xtensa-esp32s3-elf-gcc` 15.2.0 (`esp-15.2.0_20251204`) | `C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\...` |
| OpenOCD | `v0.12.0-esp32-20260703` | `C:\Espressif\tools\openocd-esp32\...` (installed, not used yet) |

**Verify:**

```powershell
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
idf.py --version                    # ESP-IDF v6.1
python -m esptool version           # v5.3.1
xtensa-esp32s3-elf-gcc --version    # ... 15.2.0
```

### C.2 Environment variables

| Variable | Value | Set by | Status |
|---|---|---|---|
| `IDF_PATH`, `IDF_TOOLS_PATH`, `IDF_PYTHON_ENV_PATH`, `PATH` additions | see C.1 | The activation script, **only in the terminal that loads it**. Nothing ESP-IDF-related is set globally. | ✅ |
| `PYTHONUTF8` | `1` (user-level, permanent) | `setx PYTHONUTF8 1`, to fix ESP-IDF's "not configured to handle Unicode characters" warning. Restart VS Code afterwards. | ✅ confirmed set on 2026-10-23 |
| `IDF_TARGET` | should be unset or `esp32s3` | The VS Code extension passes its own selected target into terminals. If unset there, it defaulted to `esp32`. See [F](#f-troubleshooting-and-lessons-learned). | ✅ fixed |

### C.3 Opening an ESP-IDF terminal

Use one of these. **Never activate the `tools/` Python venv in the same terminal.**

**Option A: any PowerShell window** ✅ (this is what was used for every build)

```powershell
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
idf.py --version
```

The leading `. ` (dot and space) matters: it loads the script into the current shell instead of running it in a child shell that exits straight away.

**Option B: VS Code ESP-IDF extension** ✅
1. Install **ESP-IDF** by Espressif Systems from Extensions (`Ctrl+Shift+X`).
2. When it asks for a setup, choose the existing **v6.1** at `C:\esp\v6.1\esp-idf`. Don't let it download a second copy. This writes `.vscode/settings.json` with `"idf.currentSetup": "C:\\esp\\v6.1\\esp-idf"`. `.vscode/` is git-ignored because the path is specific to this machine.
3. `Ctrl+Shift+P` → **ESP-IDF: Set Espressif Device Target** → **esp32s3** → **ESP32-S3 chip (via builtin USB-JTAG)**.
4. `Ctrl+Shift+P` → **ESP-IDF: Open ESP-IDF Terminal**.

The extension's commands run on **the folder open in VS Code**, so open `D:\Projects\od`, which has a top-level `CMakeLists.txt`, not a parent folder.

**Option C: a dedicated VS Code terminal profile** ⏳ suggested, not set up on this machine. Add to the user `settings.json`:

```json
"terminal.integrated.profiles.windows": {
  "ESP-IDF v6.1": {
    "path": "powershell.exe",
    "args": ["-NoExit", "-ExecutionPolicy", "Bypass",
             "-Command", ". 'C:\\Espressif\\tools\\Microsoft.v6.1.PowerShell_profile.ps1'"]
  }
}
```

### C.4 Python tools environment (`tools/`, separate from ESP-IDF)

**Purpose:** `tools/detect.py` is a PC-side YOLO (ultralytics) reference detector, used to compare against the board's detector later. It is **not** part of the firmware build.

```powershell
cd D:\Projects\od\tools
uv sync                      # creates tools\.venv from uv.lock
uv run detect.py --help      # sanity check
```

✅ Verified 2026-10-03 (`ultralytics==8.4.143` installed; `--help` printed usage). It needs Python ≥ 3.14 (`tools/pyproject.toml`).

To activate the venv by hand, in a **normal** terminal (not the ESP-IDF one):

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned   # only if PowerShell blocks scripts
.\tools\.venv\Scripts\Activate.ps1
deactivate                                                         # to leave
```

**Keep the two environments in separate terminals.** VS Code's Python extension auto-activates venvs in new terminals. When `(od)` appeared in the ESP-IDF terminal prompt, it mixed the two Python environments. To stop that per folder, add `"python.terminal.activateEnvironment": false` to `.vscode/settings.json` (⏳ suggested, not applied).

---

## D. Building, flashing and running

### D.1 Locations

| What | Path |
|---|---|
| Repository | `D:\Projects\od` (GitHub `Vanthoff007/od`, branch `main`) |
| ESP-IDF project root | `D:\Projects\od` (has `CMakeLists.txt`, `project(night-sentinel)`) |
| App component | `main/` (`app_main.c`) |
| Board drivers | `components/board/` (`board_pins.h`, `status_led.c`, `ir.c`) |
| Committed config | `sdkconfig.defaults` |
| Generated, git-ignored | `build/`, `sdkconfig`, `sdkconfig.old`, `managed_components/` |

### D.2 Configure

The board's settings live in **`sdkconfig.defaults`**, which is committed. On the first configure, ESP-IDF copies them into `sdkconfig`, which is generated and ignored by git:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
```

**First time in a fresh clone** (✅ run on 2026-10-04):

```powershell
cd D:\Projects\od
idf.py set-target esp32s3
```

- **What it does:** creates `sdkconfig` for the ESP32-S3 from `sdkconfig.defaults`.
- **Caution:** `set-target` always runs `fullclean` first. It deletes `build/` and regenerates `sdkconfig`, so any menuconfig changes that aren't in `sdkconfig.defaults` are lost.

**Interactive config** (✅ used on hello_world):

```powershell
idf.py menuconfig
```

Arrow keys to move, `S` to save, `Esc` to go back or exit. If you change a setting the project depends on, copy it into `sdkconfig.defaults` too, or it will be lost the next time `sdkconfig` is regenerated.

### D.3 Build

```powershell
cd D:\Projects\od
idf.py build
```

✅ Verified 2026-10-23. It ends with:

```
night-sentinel.bin binary size 0x2c530 bytes. Smallest app partition is 0x100000 bytes. 0xd3ad0 bytes (83%) free.
...
Project build complete. To flash, run:
 idf.py -p PORT flash
```

Optional size report (✅):

```powershell
idf.py size
```

> **Managed components:** `idf.py add-dependency "<namespace>/<name>"` creates or updates `main/idf_component.yml`. The next build or reconfigure resolves it into `dependencies.lock` and downloads the sources into `managed_components/` (git-ignored). Espressif recommends committing `idf_component.yml` and `dependencies.lock`.

### D.4 Flash 🔥 (overwrites the app on the board)

```powershell
cd D:\Projects\od
idf.py -p COM5 flash monitor
```

✅ Used repeatedly with hello_world, the project firmware, and the esp-dl example.

- **What it does:** builds if anything changed, then writes three images over USB at 460 800 baud, then opens the monitor:

  | Offset | Image |
  |---|---|
  | `0x0` | `build\bootloader\bootloader.bin` |
  | `0x8000` | `build\partition_table\partition-table.bin` |
  | `0x10000` | `build\night-sentinel.bin` |

- Flash parameters come from `sdkconfig`: `--flash-mode dio --flash-size 16MB --flash-freq 80m`.
- Reset is automatic (`--before default-reset --after hard-reset`). The BOOT/RST buttons were never needed.
- `idf.py -p COM5 flash` (without `monitor`) flashes only.

Expected flash output (abridged from a real run):

```
Changing baud rate to 460800...
Writing 'bootloader/bootloader.bin' at 0x00000000...
Hash of data verified.
Writing 'partition_table/partition-table.bin' at ...
Hash of data verified.
Writing '<app>.bin' at 0x00010000...
```

**Equivalent raw esptool command** (⏳ not run, printed by the build). It's for flashing without `idf.py`, from `D:\Projects\od\build`:

```powershell
python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset write-flash "@flash_args"
```

### D.5 Verify it's running

1. In the monitor, check the boot banner and app log. A known-good hello_world boot (recorded 2026-10-03) showed:

   ```
   I (187) efuse_init: Chip rev:         v0.2
   I (219) spi_flash: detected chip: winbond
   I (222) spi_flash: flash io: dio
   Hello world!
   This is esp32s3 chip with 2 CPU core(s), WiFi/BLE, silicon revision v0.2, 2MB external flash
   ```
   (The "2MB" is the hello_world default. A 16 MB setting removes the `Detected size(16384k) larger than...` warning.)

2. With PSRAM enabled, expect (✅ project firmware, 2026-10-23):

   ```
   I (203) esp_psram: Found 8MB PSRAM device
   I (207) esp_psram: Speed: 40MHz
   I (943) esp_psram: SPI SRAM memory test OK
   I (1019) esp_psram: Adding pool of 8192K of PSRAM memory to heap allocator
   ```
3. For the current project firmware: the frame-rate result line shown in [B.3](#b3-open-a-serial-terminal-monitor).

### D.6 Reset and erase

| Action | Command | Status |
|---|---|---|
| Reset | Press **RST** on the board, unplug/replug, or `Ctrl+T` `Ctrl+R` in the monitor | ⏳ the monitor shortcut wasn't used |
| Erase all flash 🔥 | `idf.py -p COM5 erase-flash` | ⏳ **never used.** It wipes the app, NVS (saved Wi-Fi credentials) and everything else. Not needed in normal work, since `flash` overwrites what it needs. |
| Read flash chip ID (read-only) | `python -m esptool --chip esp32s3 -p COM5 flash-id` | ⏳ not run. The boot log already showed the 16 MB Winbond flash. |

### D.7 Other firmware flashed so far

| Firmware | How | Result | Status |
|---|---|---|---|
| ESP-IDF `hello_world` | Copied `$env:IDF_PATH\examples\get-started\hello_world` to `D:\Projects\hello_world`, then `idf.py set-target esp32s3`, `idf.py build`, `idf.py -p COM5 flash monitor` | Booted. Used for the flash and PSRAM checks. The folder has since been deleted. | ✅ |
| DFRobot CameraWebServer (prebuilt `.bin` from [DFR1154_Examples](https://github.com/DFRobot/DFR1154_Examples)) | Done by the user. **The flashing method and offsets weren't recorded.** | Stream works (ticked in the tracker). | ✅ result, ⏳ procedure |
| esp-dl `pedestrian_detect` example | See [E.3](#e3-ml-esp-dl-pedestrian-detection) | 3 detections on the bundled image | ✅ |
| Project firmware (`night-sentinel`) | [D.3](#d3-build) + [D.4](#d4-flash--overwrites-the-app-on-the-board) | LED + IR on | ✅ |

> ✅ **Current state (2026-10-23):** the board runs the project firmware with the Day 2 frame-rate test.

---

## E. Project-specific configuration

### E.1 Memory (flash, SRAM, PSRAM)

| Item | Value | Status |
|---|---|---|
| Flash | 16 MB, DIO, 80 MHz. `docs/notes.md` records that QIO @ 80 MHz also worked when the esp-dl example used it; the project stays on DIO. | ✅ |
| Partition table | Default **single app**, 1 MB factory partition (`CONFIG_PARTITION_TABLE_SINGLE_APP=y`) | ✅ current |
| App size | 0x2c530 bytes (≈178 KB), 83 % of the partition free | ✅ 2026-10-23 |
| Internal RAM (DIRAM) | 51 954 / 341 760 bytes used (15.2 %) | ✅ `idf.py size` |
| PSRAM | 8 MB octal, `CONFIG_SPIRAM_USE_MALLOC=y`, speed 40 MHz in the project's `sdkconfig`. The esp-dl example used 80 MHz. | ✅ works |
| CPU | 240 MHz | ✅ |

> ⏳ **Needs doing before esp-dl goes into the project:** the `pedestrian_detect` app alone was about 2.7 MB, which doesn't fit the default 1 MB factory partition. The build plan calls for a custom `partitions.csv` with a factory partition of at least 6 MB.

**How PSRAM was first enabled (hello_world, ✅):** hello_world uses `idf_build_set_property(MINIMAL_BUILD ON)`, which hides the **ESP PSRAM** menu. The fix was to add `esp_psram` to `main/CMakeLists.txt` (`PRIV_REQUIRES spi_flash esp_psram`), then run `idf.py reconfigure` and `idf.py menuconfig` and set:
- **Serial flasher config → Flash size → 16 MB**
- **Component config → ESP PSRAM → Support for external, SPI-connected RAM → SPI RAM config → Mode → Octal Mode PSRAM**

The project's `sdkconfig.defaults` already contains these settings, so the project doesn't need this step.

### E.2 GPIO and peripherals

All pin numbers the firmware uses live in [`components/board/include/board_pins.h`](../components/board/include/board_pins.h). Add a pin there before using it anywhere else.

#### E.2.1 Pins used by the firmware

| Function | GPIO | `board_pins.h` name | Notes | Status |
|---|---|---|---|---|
| Status LED | 3 | `PIN_STATUS_LED` | Plain GPIO output, active-high | ✅ in firmware |
| IR LEDs | 47 | `PIN_IR_LED` | Drives EN of the SY7200A LED driver (`IR_CTL`), active-high. Plain GPIO now. If it is ever dimmed, use **LEDC timer 1 / channel 1**, because the camera's XCLK uses timer 0 / channel 0. | ✅ in firmware |

#### E.2.2 Camera (OV3660)

Source: DFRobot [`5_3.ino`](https://github.com/DFRobot/DFR1154_Examples/blob/master/5.3%20CameraWebServer/source%20code/5_3/5_3.ino) lines 5–22. ✅ **Verified on the board 2026-10-23:** the driver detected the OV3660 (`PID=0x3660`) at SCCB address `0x3C`, and 1,000 frames were captured with 0 NULL frames.

| Signal | GPIO | `board_pins.h` name | `camera_config_t` field | Arduino name (`5_3.ino`) |
|---|---|---|---|---|
| Power-down | −1 (not connected) | `PIN_CAM_PWDN` | `pin_pwdn` | `PWDN_GPIO_NUM` |
| Reset | −1 (not connected) | `PIN_CAM_RESET` | `pin_reset` | `RESET_GPIO_NUM` |
| XCLK (clock to sensor) | 5 | `PIN_CAM_XCLK` | `pin_xclk` | `XCLK_GPIO_NUM` |
| SCCB data (I2C SDA) | 8 | `PIN_CAM_SIOD` | `pin_sccb_sda` | `SIOD_GPIO_NUM` |
| SCCB clock (I2C SCL) | 9 | `PIN_CAM_SIOC` | `pin_sccb_scl` | `SIOC_GPIO_NUM` |
| D0 | 16 | `PIN_CAM_D0` | `pin_d0` | `Y2_GPIO_NUM` |
| D1 | 18 | `PIN_CAM_D1` | `pin_d1` | `Y3_GPIO_NUM` |
| D2 | 21 | `PIN_CAM_D2` | `pin_d2` | `Y4_GPIO_NUM` |
| D3 | 17 | `PIN_CAM_D3` | `pin_d3` | `Y5_GPIO_NUM` |
| D4 | 14 | `PIN_CAM_D4` | `pin_d4` | `Y6_GPIO_NUM` |
| D5 | 7 | `PIN_CAM_D5` | `pin_d5` | `Y7_GPIO_NUM` |
| D6 | 6 | `PIN_CAM_D6` | `pin_d6` | `Y8_GPIO_NUM` |
| D7 | 4 | `PIN_CAM_D7` | `pin_d7` | `Y9_GPIO_NUM` |
| VSYNC | 1 | `PIN_CAM_VSYNC` | `pin_vsync` | `VSYNC_GPIO_NUM` |
| HREF | 2 | `PIN_CAM_HREF` | `pin_href` | `HREF_GPIO_NUM` |
| PCLK (pixel clock from sensor) | 15 | `PIN_CAM_PCLK` | `pin_pclk` | `PCLK_GPIO_NUM` |

- **Arduino `Y2`–`Y9` = ESP-IDF `pin_d0`–`pin_d7`.** The names are offset by two, which is the most common mistake when copying.
- The camera's XCLK takes **LEDC timer 0 / channel 0**. Nothing else may use them.
- The sensor is mounted upside down, so `ns_cam_init()` calls `set_vflip(s, 1)`.
- Clock settings and the measured frame rate are in [E.5](#e5-camera-clocks-and-frame-rate).

#### E.2.3 Other board peripherals (not in the firmware yet)

These come from DFRobot's examples and wiki. None of them is in `board_pins.h` or verified on this board yet.

| Function | GPIO | Source | Status |
|---|---|---|---|
| microSD (SPI mode) | CS 10 · MOSI 11 · MISO 13 · SCK 12 | DFRobot `6_9.ino` / `6_10.ino` | ⏳ Day 9 |
| Speaker amp (MAX98357, I2S) | BCLK 45 · LRCLK/WS 46 · DOUT 42 | DFRobot examples 5.2 / 6.10, `setPins(45, 46, 42)` | ⏳ Day 7. No amp-enable pin in any example. |
| Microphone (PDM) | CLK 38 · DATA 39 | DFRobot example 5.2 | not planned |
| Light sensor (LTR-308) | Shares the camera's SCCB bus, GPIO 8 / 9 | DFRobot wiki | ⏳ I2C address unknown. A community firmware reports bus contention with the camera. |
| BOOT button | 0 | DFRobot wiki | hardware |
| Gravity UART | TX 43 · RX 44 | DFRobot wiki | not planned |

#### E.2.4 Pins to avoid

From the ESP32-S3 datasheet, for this board's configuration:

| GPIO | Why |
|---|---|
| 19, 20 | USB D− / D+. This is the USB Serial/JTAG port used for flashing and the monitor. |
| 26–32 | Connected to the SPI flash chip. |
| 33–37 | Used by the **octal** PSRAM. |
| 0, 3, 45, 46 | **Strapping pins**: they are read at reset to choose the boot mode and other settings. GPIO3 (status LED) and 45/46 (speaker) are already wired by DFRobot, so don't add pull-up or pull-down resistors to them. |

How pins were checked: DFRobot example code first, then the DFRobot wiki, then the schematic (`resources/.../Schematic v1.1.pdf`). The full method is in `docs/next_steps.md` → "Where pin numbers come from".

### E.3 ML: esp-dl pedestrian detection

✅ Run on 2026-10-04 as a standalone test to decide between ESP-IDF v6.1 and v5.4.

```powershell
. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
cd <some scratch folder outside the repo>
idf.py create-project-from-example "espressif/esp-dl:pedestrian_detect"
cd pedestrian_detect
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor      # 🔥 replaces the project firmware
```

- The example **doesn't use the camera.** It runs on a `pedestrian.jpg` built into the firmware.
- Result: 3 boxes, scores **0.884 / 0.879 / 0.786**, which matches Espressif's reference (0.884 / 0.884 / 0.805). Inference took **~130 ms**.
- Its own `sdkconfig` used octal PSRAM, DIO, 8 MB flash and 80 MHz PSRAM.
- Component versions at the time: `espressif/esp-dl` 3.3.x, `espressif/pedestrian_detect` 0.3.2.

**Outcome:** stay on ESP-IDF v6.1. The original scratch folder has been deleted, so re-run the commands above to reproduce it.

### E.4 In progress and not started

- **Day 2, 2026-10-23:**
  - ✅ `idf.py add-dependency "espressif/esp32-camera"` run from `D:\Projects\od`. `dependencies.lock` resolved `espressif/esp32-camera` **2.1.8**, which pulls in `espressif/esp_jpeg` **1.3.1**.
  - ✅ `components/camera/` (`ns_cam_init`, `ns_cam_grab`, `ns_cam_release`) and the 1,000-frame FPS test in `app_main.c` work on the board: **11.1 FPS, 0 NULL frames, PSRAM stable**. See [E.5](#e5-camera-clocks-and-frame-rate).
  - ⏳ Not committed yet. Data types are in [section I](#i-data-types-used-in-the-firmware).
- Not started: Wi-Fi, `/snap.jpg`.
- Wi-Fi station and HTTP server.
- Unit tests (Unity). The planned layout is `components/board/test/` plus a `test/` project modelled on `C:\esp\v6.1\esp-idf\examples\system\unit_test`.
- Bringing esp-dl into the project, the partition table change, and model or quantization work.

### E.5 Camera clocks and frame rate

✅ Measured 2026-10-23: QVGA RGB565, `fb_count = 2`, `CAMERA_GRAB_LATEST`, 1,000 frames each.

| Clock setting | Log line (`ov3660: Calculated ...`) | Result |
|---|---|---|
| Driver default | `SYSCLK: 20000000 Hz, PCLK: 10000000 Hz` | **5.5 FPS** (180.2 s), 0 NULL frames |
| `set_pll(s, 0, 8, 1, 0, 0, 0, 1, 8)`: SYSCLK doubled only | `SYSCLK: 40000000 Hz, PCLK: 10000000 Hz` | **No frames at all**: `cam_hal: Failed to get frame: timeout` every 4 s |
| `set_pll(s, 0, 8, 1, 0, 0, 0, 1, 4)`: both doubled (**in use**) | `SYSCLK: 40000000 Hz, PCLK: 20000000 Hz` | **11.1 FPS** (90.1 s), 0 NULL frames, PSRAM 8,077,692 → 8,077,692 bytes |

- **Why the default is slow:** at QVGA the driver sets a frame of 2300 × 783 clock cycles (line length × binned frame height, from `ov3660_settings.h`). At 20 MHz that is 90 ms per pass, and the measured frame period is 180 ms. The driver's own "10.25 FPS" comment is wrong.
- **Ruled out:** lighting (a torch in front of the lens changed nothing) and frame buffers (`fb_count = 3` gave exactly the same 180 s).
- **`set_pll` argument order** (the public function): `bypass, multiplier, sys_div, root_2x, pre_div, seld5, pclk_manual, pclk_div`. The driver's internal `set_pll` swaps `root_2x` and `pre_div`.
- ⏳ **Image quality at 20 MHz PCLK is not checked yet.** That is close to the limit of the ESP32-S3 camera interface. If `/snap.jpg` shows streaks, shifted lines or wrong colours, remove the `set_pll` line first.

---

## F. Troubleshooting and lessons learned

Quick-lookup table by symptom. The full, dated history of each problem is in [issues_log.md](issues_log.md).

| Problem or error | Likely cause | Solution or diagnostic steps | Status |
|---|---|---|---|
| `idf.py`, `cmake`, `ninja`, compiler all "not found" | EIM GUI was open but hadn't installed anything yet | Finish the EIM install, then load the activation script ([C.3](#c3-opening-an-esp-idf-terminal)) | ✅ Fixed |
| EIM installed **v6.1**, but the build plan specified v5.4.x | EIM's default version | Built **and ran** esp-dl `pedestrian_detect` on v6.1 with correct results ([E.3](#e3-ml-esp-dl-pedestrian-detection)). Decision: **stay on v6.1**. | ✅ Resolved 2026-10-04 |
| `Project sdkconfig ... was generated for target 'esp32s3', but environment variable IDF_TARGET is set to 'esp32'` | The VS Code ESP-IDF extension passes its own target into terminals, and it defaulted to `esp32` | **Don't** run `idf.py set-target esp32`; the board is an ESP32-S3. Set the target in the extension (**ESP-IDF: Set Espressif Device Target → esp32s3 → builtin USB-JTAG**) and open a new terminal. Quick fix for one terminal: `$env:IDF_TARGET = "esp32s3"`. | ✅ Fixed |
| `(od)` appeared in the ESP-IDF terminal prompt | VS Code's Python extension auto-activated the repo's Python venv | Use separate terminals for ESP-IDF and Python; optionally set `"python.terminal.activateEnvironment": false` | ✅ Practice adopted (setting ⏳ not applied) |
| `CMakeLists.txt not found in project directory D:\Projects\od` | The extension ran **Set Target** on a folder that wasn't an ESP-IDF project at the time (hello_world was in a subfolder) | Open the ESP-IDF project folder itself in VS Code. `D:\Projects\od` is now a proper project with a top-level `CMakeLists.txt`. | ✅ Fixed |
| `Your environment is not configured to handle Unicode characters` | The Windows console codepage isn't UTF-8 | `setx PYTHONUTF8 1`, then restart VS Code completely. (Per terminal: `chcp 65001`.) | ✅ Fixed |
| `W spi_flash: Detected size(16384k) larger than the size in the binary image header(2048k)` | hello_world's default flash size is 2 MB | Set **Flash size → 16 MB**. The project's `sdkconfig.defaults` already does this. | ✅ Fixed |
| **ESP PSRAM** missing from menuconfig | `MINIMAL_BUILD ON` hides components that `main` doesn't depend on | Add `esp_psram` to `PRIV_REQUIRES` in `main/CMakeLists.txt`, then `idf.py reconfigure` | ✅ Fixed |
| An edit "didn't work" (seen twice) | The file wasn't saved in VS Code (dot on the editor tab) | `Ctrl+S`, then re-run. Check with `git status`. | ✅ Lesson |
| IR LEDs not visible on a phone camera | 940 nm IR is filtered out by most phone cameras, especially rear ones | Try the phone's front camera in the dark, use a USB power meter (expect a current jump), or best of all use the board's own camera (CameraWebServer stream or a Day 2 snapshot) | ✅ IR confirmed working (method not recorded) |
| Log said "status LED OFF" while the LED was on | Log text was stale after a code change | Fixed in `app_main.c` | ✅ Fixed |
| `Failed to connect` while flashing | Chip not entering download mode | Hold **BOOT**, tap **RST**, release BOOT, retry | ⏳ Suggested, never needed |
| No COM port appears | Charge-only cable or bad port | Swap the cable or USB port | ⏳ Suggested, never needed |
| Port busy / access denied | Another monitor has the port open | Close the other program | ⏳ Suggested, never needed |
| PSRAM not found / boot loop after enabling PSRAM | Wrong PSRAM mode | Switch Octal ↔ Quad in menuconfig. On this board **Octal** is correct. | ⏳ Not hit (Octal worked the first time) |
| `ERROR: Component "espressif/camera" not found` | Wrong component name | The driver is **`espressif/esp32-camera`**: `idf.py add-dependency "espressif/esp32-camera"` | ✅ Fixed 2026-10-23 |
| `multiple definition of 'cam_init'` at link time | The driver's `cam_hal.c` already defines a global `cam_init`. C has one global namespace for functions. | Prefix the project's functions: `ns_cam_init`, `ns_cam_grab`, `ns_cam_release` | ✅ Fixed 2026-10-23 |
| Own header named `esp_camera.h` | Would hide the driver's header of the same name | Named it `camera.h` instead | ✅ Avoided |
| Camera only 5.5 FPS at QVGA | Driver's default OV3660 clocks for RGB565 | `set_pll` in `ns_cam_init()`, see [E.5](#e5-camera-clocks-and-frame-rate) | ✅ Fixed (11.1 FPS) |
| `cam_hal: Failed to get frame: timeout` every 4 s | Sensor SYSCLK raised without raising PCLK | Change both together, or remove the `set_pll` call | ✅ Understood |
| `gpio_get_level()` always reads 0 in a unit test | On the ESP32-S3, an output-only pin can't be read back | Use `GPIO_MODE_INPUT_OUTPUT` or `gpio_dump_io_configuration()` | ⏳ Known in advance; tests not written yet |

---

## G. Quick reference: commands

"IDF terminal" means a PowerShell with the activation script loaded (G.3).

### G.1 Identify and access the ESP32

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `[System.IO.Ports.SerialPort]::GetPortNames()` | List COM port names | anywhere | any PowerShell |
| `Get-CimInstance Win32_PnPEntity \| Where-Object { $_.Name -match "\(COM\d+\)" } \| Select-Object Name, DeviceID` | List COM ports with descriptions. The board is "USB Serial Device". | anywhere | any PowerShell |
| `python -m esptool --chip esp32s3 -p COM5 flash-id` ⏳ | Read the chip and flash ID (read-only) | anywhere | IDF terminal, board plugged in |

### G.2 Open and close the serial terminal

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `idf.py -p COM5 monitor` | Open the serial monitor at 115 200 baud | `D:\Projects\od` | IDF terminal, project built (for address decoding) |
| `Ctrl+]` | Exit the monitor | in the monitor | — |
| `Ctrl+T`, `Ctrl+R` ⏳ | Reset the board from the monitor | in the monitor | — |

### G.3 Activate the development environment

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"` | Load ESP-IDF v6.1 into this terminal | anywhere | EIM install |
| VS Code: **ESP-IDF: Open ESP-IDF Terminal** | Same thing, from VS Code | VS Code with `D:\Projects\od` open | ESP-IDF extension |
| `.\tools\.venv\Scripts\Activate.ps1` | Activate the Python tools venv (**separate terminal**) | `D:\Projects\od` | `uv sync` done |

### G.4 Install or verify dependencies

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `idf.py --version` | Expect `ESP-IDF v6.1` | anywhere | IDF terminal |
| `python -m esptool version` | Expect `v5.3.1` | anywhere | IDF terminal |
| `xtensa-esp32s3-elf-gcc --version` | Expect GCC 15.2.0 | anywhere | IDF terminal |
| `[Environment]::GetEnvironmentVariable("PYTHONUTF8","User")` | Expect `1` | anywhere | — |
| `uv sync` | Create or update the Python tools venv | `D:\Projects\od\tools` | uv |
| `uv run detect.py --help` | Check the Python tool runs | `D:\Projects\od\tools` | `uv sync` done |

### G.5 Configure and build

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `idf.py set-target esp32s3` | First-time setup in a fresh clone (**runs fullclean, regenerates `sdkconfig`**) | `D:\Projects\od` | IDF terminal |
| `idf.py menuconfig` | Interactive config editor (`S` save, `Esc` back) | `D:\Projects\od` | IDF terminal |
| `idf.py reconfigure` | Re-run CMake after changing component dependencies | `D:\Projects\od` | IDF terminal |
| `idf.py build` | Compile bootloader, partition table and app | `D:\Projects\od` | IDF terminal |
| `idf.py size` | Memory usage summary | `D:\Projects\od` | built |
| `idf.py fullclean` | Delete `build/` (fixes stale-config problems) | `D:\Projects\od` | IDF terminal |

### G.6 Flash and reset the ESP32

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `idf.py -p COM5 flash` 🔥 | Build if needed and write firmware | `D:\Projects\od` | IDF terminal, board on COM5 |
| `idf.py -p COM5 flash monitor` 🔥 | Flash, then open the monitor | `D:\Projects\od` | same |
| `idf.py -p COM5 erase-flash` 🔥⏳ | **Erase the whole flash.** Never used. | `D:\Projects\od` | same |
| BOOT + RST buttons ⏳ | Manual download mode if auto-reset fails | — | — |

### G.7 Run firmware and inspect logs

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `idf.py -p COM5 monitor` | Watch logs. With the current firmware, expect the frame-rate result about 90 s after boot ([B.3](#b3-open-a-serial-terminal-monitor)). | `D:\Projects\od` | flashed |

### G.8 Debug common issues

| Command | What it does | Run from | Needs |
|---|---|---|---|
| `$env:IDF_TARGET = "esp32s3"` | Override a wrong `IDF_TARGET` in this terminal | anywhere | — |
| `setx PYTHONUTF8 1` | Permanently fix the Unicode warning (restart VS Code after) | anywhere | — |
| `chcp 65001` | Per-terminal Unicode fix | anywhere | — |
| `git status --short` | Check that edits were actually saved | `D:\Projects\od` | — |
| `Select-String -Path sdkconfig -Pattern "SPIRAM_MODE\|FLASHSIZE"` | Confirm PSRAM mode and flash size in the generated config | `D:\Projects\od` | `sdkconfig` exists |

---

## H. Reproducing the setup from scratch

From a clean Windows machine to the project firmware running on the board. ⏳ marks steps whose exact details weren't recorded or that haven't been re-run from scratch.

1. **Install Git for Windows.** ✅ (pre-existing; exact installer not recorded ⏳)
2. **Clone the repo** to `D:\Projects\od`:
   ```powershell
   git clone https://github.com/Vanthoff007/od.git D:\Projects\od
   ```
   ⏳ The repo was not cloned fresh; it already existed on this machine.
3. **Install ESP-IDF v6.1 with the EIM GUI** (`eim-gui-windows-x64-v0.19.0.exe` was used). Choose ESP-IDF **v6.1**, target **esp32s3**, the install path `C:\esp`, and the tools path `C:\Espressif\tools`. ⏳ The exact GUI options weren't recorded; check that the result matches the paths in [C.1](#c1-esp-idf-v61-via-eim).
4. **Fix the Unicode warning** once: `setx PYTHONUTF8 1`, then close and reopen all terminals and VS Code. ✅
5. **Open an IDF terminal and verify** ([G.4](#g4-install-or-verify-dependencies)):
   ```powershell
   . "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
   idf.py --version            # ESP-IDF v6.1
   ```
   ✅
6. **(Optional) VS Code:** install the ESP-IDF extension, pick the existing v6.1 setup, open `D:\Projects\od`, and set the target to **esp32s3 / builtin USB-JTAG** ([C.3](#c3-opening-an-esp-idf-terminal), Option B). ✅
7. **Plug in the board** with a data-capable USB-C cable and find its COM port ([B.1](#b1-find-the-boards-serial-port)). It was COM5 here; substitute yours. ✅
8. **(Optional, recommended once per new board) toolchain smoke test** with hello_world:
   ```powershell
   Copy-Item "$env:IDF_PATH\examples\get-started\hello_world" D:\Projects\hello_world -Recurse
   cd D:\Projects\hello_world
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COM5 flash monitor     # expect "esp32s3 chip with 2 CPU core(s) ... revision v0.2"; Ctrl+] to exit
   ```
   ✅
9. **Configure and build the project:**
   ```powershell
   cd D:\Projects\od
   idf.py set-target esp32s3
   idf.py build                      # "Project build complete"
   ```
   ✅
10. **Flash and watch it run** 🔥:
    ```powershell
    idf.py -p COM5 flash monitor
    ```
    Expect `camera init OK`, then after about 90 s the frame-rate line (`... 0 failures, 11 fps ...`). ✅
11. **(Optional) Python tools:** in a separate, non-IDF terminal:
    ```powershell
    cd D:\Projects\od\tools
    uv sync
    uv run detect.py --help
    ```
    Needs `uv` and Python ≥ 3.14 ⏳ (how uv was installed wasn't recorded). ✅ for the commands themselves.

### Still to confirm

- ⏳ The exact EIM options and whether EIM adds a Start-menu or desktop shortcut for the IDF PowerShell.
- ⏳ How the DFRobot CameraWebServer `.bin` was flashed (tool, offset).
- ⏳ The `erase-flash`, `flash-id`, raw `esptool write-flash` and monitor-reset commands, which have never been run on this board.

---

## I. Data types used in the firmware

Types that appear in the project's C code (`main/`, `components/board/`, `components/camera/`) as of 2026-10-23. The definitions below were checked against the installed headers (ESP-IDF v6.1 in `C:\esp\v6.1\esp-idf\components\`, and esp32-camera 2.1.8 in `managed_components/`) and against the compiler itself.

### I.1 Basic C types on the ESP32-S3

The ESP32-S3 is a **32-bit** Xtensa chip. Sizes reported by `xtensa-esp32s3-elf-gcc -dM -E -` (✅ checked):

| Type | Size | Underlying type | Notes |
|---|---|---|---|
| `char` | 1 byte | — | **Unsigned by default** on Xtensa (`__CHAR_UNSIGNED__`). Use `int8_t` if you need a signed byte. |
| `int`, `unsigned int` | 4 bytes | — | |
| `long` | 4 bytes | — | Same size as `int` (not 8 like on 64-bit Linux). |
| `long long` | 8 bytes | — | |
| pointer (`T *`) | 4 bytes | — | |
| `size_t` | 4 bytes | `unsigned int` | Buffer lengths, heap sizes. |
| `int8_t` / `uint8_t` | 1 byte | `signed char` / `unsigned char` | Raw bytes, pixel data. |
| `int16_t` / `uint16_t` | 2 bytes | `short` / `unsigned short` | e.g. RGB565 pixels, sensor IDs. |
| `int32_t` / `uint32_t` | 4 bytes | **`long` / `unsigned long`** | Not `int`. This matters for `printf` (I.4). |
| `int64_t` / `uint64_t` | 8 bytes | `long long` / `unsigned long long` | Timestamps. |
| `float` | 4 bytes | — | Done in hardware on the S3's FPU. |
| `double` | 8 bytes | — | The FPU is single-precision only, so `double` maths runs in software and is slower. Prefer `float` in hot loops. |
| `bool` | 1 byte | `_Bool` | Needs `#include <stdbool.h>` (included by `ir.h` and `status_led.h`). |

### I.2 ESP-IDF and FreeRTOS types

| Type | Defined in | What it is | Used where |
|---|---|---|---|
| `esp_err_t` | `esp_common/include/esp_err.h` | `typedef int`. `ESP_OK` = 0, `ESP_FAIL` = −1, other codes are positive. Convert to text with `esp_err_to_name(err)`. | `ns_cam_init()` return value; GPIO driver calls return it too (currently ignored in `ir.c` / `status_led.c`) |
| `gpio_num_t` | `esp_hal_gpio/esp32s3/include/soc/gpio_num.h` | `enum` of pins: `GPIO_NUM_0` … `GPIO_NUM_48` (e.g. `GPIO_NUM_47 = 47`). | `gpio_reset_pin`, `gpio_set_direction`, `gpio_set_level` take it. `board_pins.h` passes plain `#define` integers, which C converts implicitly. |
| `gpio_mode_t` | `esp_hal_gpio/include/hal/gpio_types.h` | `enum`: `GPIO_MODE_OUTPUT`, `GPIO_MODE_INPUT_OUTPUT`, … | `gpio_set_direction(..., GPIO_MODE_OUTPUT)` in `ir.c`, `status_led.c` |
| `uint32_t` level | `gpio_set_level(gpio_num_t, uint32_t level)` | 0 = low, non-zero = high | `ir_set(bool on)` passes the `bool` straight through (`true` → 1) |
| `ledc_timer_t`, `ledc_channel_t` | `esp_hal_ledc/include/hal/ledc_types.h` | `enum`s naming an LEDC timer / channel | `camera_config_t.ledc_timer = LEDC_TIMER_0`, `.ledc_channel = LEDC_CHANNEL_0` (the camera's XCLK) |
| `TickType_t` | FreeRTOS `portmacro.h` | `uint32_t` (ESP-IDF sets `configUSE_16_BIT_TICKS 0`). One tick = 10 ms here (`CONFIG_FREERTOS_HZ=100`). | `vTaskDelay(pdMS_TO_TICKS(5000))` in the Day 1 code |
| `BaseType_t` / `UBaseType_t` | FreeRTOS `portmacro.h` | `int` / `unsigned int`; FreeRTOS's return/status type (`pdTRUE`, `pdPASS`) | not used yet (will appear with tasks and queues) |
| `int64_t` (time) | `esp_timer/include/esp_timer.h` | `esp_timer_get_time()` returns **microseconds since boot** as `int64_t` | FPS timing in `app_main.c` |
| `size_t` (heap) | `heap/include/esp_heap_caps.h` | `heap_caps_get_free_size(uint32_t caps)` returns free bytes. `MALLOC_CAP_SPIRAM` (`1<<10`) selects PSRAM. | PSRAM check in `app_main.c` |
| `const char *` tag | `esp_log.h` | `static const char *TAG = "main";` is the prefix shown in each log line | `main`, `camera` |

### I.3 Camera driver types (esp32-camera 2.1.8)

From `managed_components/espressif__esp32-camera/driver/include/esp_camera.h` and `sensor.h`. ✅ Checked against the headers. ✅ The camera code runs on the board (2026-10-23).

| Type | Kind | Key fields / values | Used in |
|---|---|---|---|
| `camera_config_t` | `struct` | Pins are all **`int`** (`pin_pwdn`, `pin_reset`, `pin_xclk`, `pin_sccb_sda`, `pin_sccb_scl`, `pin_d0`–`pin_d7`, `pin_vsync`, `pin_href`, `pin_pclk`). **−1 means not connected** (PWDN and RESET on this board). Also `int xclk_freq_hz`, `ledc_timer_t ledc_timer`, `ledc_channel_t ledc_channel`, `pixformat_t pixel_format`, `framesize_t frame_size`, `int jpeg_quality` (0–63, lower is better), `size_t fb_count`, `camera_fb_location_t fb_location`, `camera_grab_mode_t grab_mode`. | `ns_cam_init()` in `components/camera/camera.c` |
| `camera_fb_t` | `struct` (one frame) | `uint8_t *buf` pixel data · `size_t len` bytes · `size_t width`, `size_t height` · `pixformat_t format` · `struct timeval timestamp` (time since boot of the frame's first DMA buffer) | returned by `ns_cam_grab()`, passed to `ns_cam_release()` |
| `pixformat_t` | `enum` | `PIXFORMAT_RGB565` (2 bytes per pixel), also `PIXFORMAT_JPEG`, `PIXFORMAT_GRAYSCALE`, `PIXFORMAT_YUV422`, … | `.pixel_format = PIXFORMAT_RGB565` |
| `framesize_t` | `enum` | `FRAMESIZE_QVGA` = 320×240, also `FRAMESIZE_VGA`, … | `.frame_size = FRAMESIZE_QVGA` |
| `camera_fb_location_t` | `enum` | `CAMERA_FB_IN_PSRAM`, `CAMERA_FB_IN_DRAM` | `.fb_location = CAMERA_FB_IN_PSRAM` |
| `camera_grab_mode_t` | `enum` | `CAMERA_GRAB_WHEN_EMPTY`, `CAMERA_GRAB_LATEST` | `.grab_mode = CAMERA_GRAB_LATEST` |
| `sensor_t` | `struct` of sensor info and **function pointers** | e.g. `int (*set_vflip)(sensor_t *sensor, int enable)`; call as `s->set_vflip(s, 1)`. `s->id` is a `sensor_id_t`. | `ns_cam_init()` after `esp_camera_sensor_get()` |
| `sensor_id_t` | `struct` | `uint16_t PID` (sensor product ID), plus other ID fields | logged in `ns_cam_init()` |

**One RGB565 QVGA frame** = 320 × 240 × 2 bytes = **153 600 bytes** (`fb->len`). With `fb_count = 2`, that's about 300 KB of frame buffers, which is why they go in PSRAM rather than the ~340 KB of internal RAM.

**In RGB565**, each pixel is one 16-bit value (5 bits red, 6 green, 5 blue) stored as two `uint8_t` bytes in `buf`. The byte order matters when decoding it. The Day 2 guide (`docs/next_steps.md`) flags a swapped order as a cause of wrong colours.

### I.4 Printing these types in `ESP_LOGx`

`ESP_LOGI`/`ESP_LOGE` use `printf`-style formats. ESP-IDF builds with `-Wall`, so format mismatches produce warnings, and possibly errors. Because `int32_t`/`uint32_t` are `long` here, `%d`/`%u` are wrong for them.

| Type | Specifier | Portable form (`#include <inttypes.h>`) |
|---|---|---|
| `int` / `bool` / `esp_err_t` | `%d` | — |
| `size_t` | `%zu` | — |
| `uint32_t` | `%lu` | `"%" PRIu32` (recommended) |
| `int32_t` | `%ld` | `"%" PRId32` |
| `int64_t` (e.g. `esp_timer_get_time()`) | `%lld` | `"%" PRId64` |
| `uint16_t` / `uint8_t` | `%u` (promoted to `int`) or `%x` for hex | `"%" PRIu16` |
| `float` / `double` | `%f` (`float` is promoted to `double`) | — |
| pointer | `%p` | — |
| `esp_err_t` as text | `%s` with `esp_err_to_name(err)` | — |

The current `app_main.c` log line uses `%lld` for an `int64_t` expression, `%d` for `int`, and `%zu` for `size_t`. Those all match this compiler.
