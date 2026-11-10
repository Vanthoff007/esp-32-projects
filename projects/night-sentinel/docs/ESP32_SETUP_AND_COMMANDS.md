# ESP32 setup and command handbook

How to set up, build, flash, run and debug the night-sentinel firmware on the DFRobot DFR1154, in the order you'd do it. Last checked 2026-11-06 on the machine and board described below.

Related docs: [notes.md](notes.md) (board facts) · [issues_log.md](issues_log.md) (every problem hit, by date, with full measurements) · [../tools/README.md](../tools/README.md) (PC-side Python tools) · `docs/next_steps.md` (session handoff; **local only**, git-ignored).

**Status labels:** ✅ verified on this setup · ⏳ not verified (documented or suggested, never run here) · 🔥 overwrites or erases the board.

Commands are **PowerShell**. "IDF terminal" means a PowerShell with ESP-IDF loaded ([step 1.4](#14-open-an-esp-idf-terminal)). Replace `COM5` with your port ([step 2.1](#21-find-the-serial-port)).

---

## 1. Prerequisites and environment setup

### 1.1 Hardware

| Item | Value | Status |
|---|---|---|
| Board | DFRobot **DFR1154** "ESP32-S3 AI Camera Module", hardware **v1.1**. Schematics (v1.0, v1.1), datasheets and dimensions in [`../resources/`](../resources/) | ✅ |
| SoC | ESP32-S3, silicon rev **v0.2**, 2 cores, 240 MHz, Wi-Fi/BLE | ✅ boot log |
| Flash | **16 MB** Winbond, **DIO**, 80 MHz | ✅ boot log |
| PSRAM | **8 MB octal**, run at **80 MHz** | ✅ boot log |
| Camera | OV3660, SCCB address `0x3C` | ✅ |
| USB | One USB-C cable to the chip's **built-in USB Serial/JTAG**. No USB-UART chip, **no driver needed** on Windows | ✅ |
| Cable | Must carry data: charge-only USB-C cables power the board but no COM port appears | ✅ |

**Power** (schematic v1.1): USB-C VBUS → diode D9, and the 2-pin connector **P4 "Vin 3.7~15V"** → diode D10, both into the JW3651 3.3 V regulator (U12). The inputs are diode-OR'd, so the higher voltage powers the board and neither feeds back into the other.

| Power setup | Status |
|---|---|
| USB-C to the laptop only: power + data | ✅ in use |
| Phone charger (5 V, ≥ 2 A) on USB-C: runs standalone over Wi-Fi, no serial log or flashing | ✅ by design |
| **5–12 V on VIN (P4) + USB-C to the laptop**: adapter powers the board, USB still carries data. Check P4's connector type (DFRobot wiki) and polarity before wiring | ⏳ not tried |
| Powered USB hub between laptop and board | ⏳ not tried |
| Power consumption | ⏳ **not measured** (needs a USB-C power meter: Wi-Fi idle, streaming, streaming + IR) |

### 1.2 Development machine and tools

| Item | Value | Status |
|---|---|---|
| OS / shell | Windows 11 Home (10.0.26200), Windows PowerShell 5.1 | ✅ |
| Editor | VS Code + **ESP-IDF extension** (`espressif.esp-idf-extension` 2.3.0) | ✅ |
| Git | `C:\Program Files\Git\cmd\git.exe` | ✅ |
| uv (only for `tools/`) | `C:\Users\18arj\.local\bin\uv.exe`. How it was installed wasn't recorded | ✅ ⏳ |
| System Python | 3.14 at `C:\Python314\python.exe`, **not used by ESP-IDF** | ✅ |
| Not used | **Arduino IDE / Arduino-ESP32** (deliberately not installed: a second toolchain risks the wrong `esptool` or port; DFRobot's `.ino` sketches are only read for pin numbers), PlatformIO | — |

### 1.3 Install ESP-IDF v6.1

1. Run the **Espressif Installation Manager GUI** (`eim-gui-windows-x64-v0.19.0.exe`, kept in `C:\Users\18arj\.espressif\eim_gui\`) and install ESP-IDF **v6.1** for target **esp32s3**, with a short path without spaces. ✅ The exact GUI options weren't recorded ⏳. Check the result matches this table (from `C:\Espressif\tools\eim_idf.json`):

   | What | Path / version |
   |---|---|
   | ESP-IDF (`IDF_PATH`) | `C:\esp\v6.1\esp-idf` |
   | Tools (`IDF_TOOLS_PATH`) | `C:\Espressif\tools` |
   | ESP-IDF Python venv | `C:\Espressif\tools\python\v6.1\venv` (Python 3.14.5) |
   | **Activation script** | `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` |
   | esptool / CMake / Ninja | v5.3.1 / 4.0.3 / 1.12.1 |
   | Compiler | `xtensa-esp32s3-elf-gcc` 15.2.0 (`esp-15.2.0_20251204`) |
   | OpenOCD | `v0.12.0-esp32-20260703` (installed, not used) |

   > The original build plan named v5.4.x. v6.1 was kept after esp-dl's `pedestrian_detect` example ran correctly on it ([Appendix C](#appendix-c-other-firmware-flashed-to-this-board)).

2. Fix ESP-IDF's "not configured to handle Unicode characters" warning once, then restart VS Code and all terminals: ✅

   ```powershell
   setx PYTHONUTF8 1
   ```

### 1.4 Open an ESP-IDF terminal

ESP-IDF's variables (`IDF_PATH`, `PATH` additions, its Python venv) exist **only in a terminal that loads the activation script**. Nothing is set globally. Use one of:

- **Any PowerShell** ✅ (used for every build). The leading `. ` loads the script into the current shell:

  ```powershell
  . "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"
  idf.py --version                    # ESP-IDF v6.1
  python -m esptool version           # v5.3.1
  xtensa-esp32s3-elf-gcc --version    # 15.2.0
  ```

- **VS Code ESP-IDF extension** ✅
  1. Open the project folder `D:\Projects\esp32-projects\projects\night-sentinel` itself, since the extension acts on the open folder (a parent folder gives `CMakeLists.txt not found`).
  2. When asked for a setup, pick the existing **v6.1** at `C:\esp\v6.1\esp-idf`; don't download a second copy. (This writes `.vscode/settings.json`, which is git-ignored.)
  3. `Ctrl+Shift+P` → **ESP-IDF: Set Espressif Device Target** → **esp32s3** → **ESP32-S3 chip (via builtin USB-JTAG)**.
  4. `Ctrl+Shift+P` → **ESP-IDF: Open ESP-IDF Terminal**.

- **Dedicated VS Code terminal profile** ⏳ (suggested, not set up). Add to the user `settings.json`:

  ```json
  "terminal.integrated.profiles.windows": {
    "ESP-IDF v6.1": {
      "path": "powershell.exe",
      "args": ["-NoExit", "-ExecutionPolicy", "Bypass",
               "-Command", ". 'C:\\Espressif\\tools\\Microsoft.v6.1.PowerShell_profile.ps1'"]
    }
  }
  ```

**Problems at this step**

| Symptom | Fix | Status |
|---|---|---|
| `idf.py`, `cmake`, `ninja`, compiler "not found" | Finish the EIM install, then load the activation script | ✅ |
| `sdkconfig ... generated for target 'esp32s3', but environment variable IDF_TARGET is set to 'esp32'` | The extension passed its default target. Set the target as in step 3 above and open a new terminal; for one terminal: `$env:IDF_TARGET = "esp32s3"`. **Don't** run `idf.py set-target esp32` | ✅ |
| `(night-sentinel-tools)` (or another venv name) in the ESP-IDF prompt | VS Code auto-activated the `tools/` venv. **Keep ESP-IDF and `tools/` in separate terminals.** To stop it per folder: `"python.terminal.activateEnvironment": false` in `.vscode/settings.json` ⏳ | ✅ |
| Unicode warning persists | `setx` only affects new processes: restart VS Code completely. Per terminal: `chcp 65001` | ✅ |

On a new machine or board, optionally confirm the toolchain with ESP-IDF's `hello_world` before building the project ([Appendix C](#appendix-c-other-firmware-flashed-to-this-board)). It needs the COM port from [step 2.1](#21-find-the-serial-port).

### 1.5 Get the project

```powershell
git clone https://github.com/Vanthoff007/esp-32-projects.git D:\Projects\esp32-projects
cd D:\Projects\esp32-projects\projects\night-sentinel   # this project lives in projects/night-sentinel
```

Night Sentinel is one project in the [ESP32 Projects](../../../README.md) repository. Paths below are relative to `projects/night-sentinel/`.

| Path | Contents |
|---|---|
| `CMakeLists.txt` | ESP-IDF project root, `project(night-sentinel)` |
| `main/` | `app_main.c`; `idf_component.yml` (depends on `espressif/esp32-camera`) |
| `components/board/` | `board_pins.h` (**every GPIO**), `status_led.c`, `ir.c` (`ir_set`, `ir_get`) |
| `components/camera/` | `ns_cam_init / ns_cam_grab / ns_cam_release`; `ns_cam_mean_luma` (frame brightness), `ns_cam_read_ae` (live gain/exposure), `ns_cam_set_gain_ceiling`, `ns_cam_scan_bus` (I2C scan + LTR-308 check) |
| `components/net/` | Wi-Fi (`ns_wifi_connect`: saved network, else setup hotspot), setup page (`wifi_portal.c`), mDNS (`ns_mdns_start`); menu in its `Kconfig`; `idf_component.yml` (depends on `espressif/mdns`) |
| `components/debug_http/` | Debug web server: port 80 `/`, `/snap.jpg`, `/control`, `/status`; port 81 `/stream` |
| `sdkconfig.defaults`, `partitions.csv` | Committed board configuration ([step 3.1](#31-configure)) |
| `dependencies.lock` | Resolved component versions (committed) |
| `tools/` | PC-side Python: `detect.py`, `grab_snaps.py`, `capture_set.py` (Day 3) |
| `test-results/` | Saved snapshots, Day 3 tuning sweeps and light logs |
| `testdata/` | Labelled day/night test images for Day 4 (`capture_set.py snap`), each set with an `index.csv` |
| git-ignored | `build/`, `sdkconfig`, `sdkconfig.old`, `managed_components/`, `.vscode/` (repository `.gitignore`); `docs/next_steps.md`, camera images (project `.gitignore`) |

### 1.6 Python tools (optional, separate terminal)

In a **normal** (non-IDF) terminal. Needs uv and Python ≥ 3.14:

```powershell
cd D:\Projects\esp32-projects\projects\night-sentinel\tools
uv sync                     # creates tools\.venv from uv.lock
uv run detect.py --help     # ✅ 2026-10-03 (ultralytics 8.4.143)
```

`uv run` needs no activation. To activate by hand: `.\tools\.venv\Scripts\Activate.ps1` (if blocked: `Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned`), and `deactivate` to leave.

---

## 2. Connect to the ESP32

### 2.1 Find the serial port

On Windows the board is a `COMx` port. Linux names (`/dev/ttyACM0`, `/dev/ttyUSB0`) and the `dialout` group don't apply. Plug the board in, then in any PowerShell:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "\(COM\d+\)" } | Select-Object Name, DeviceID
```

✅ The board is **"USB Serial Device (COM5)"** (USB ID `303A:1001`). On this PC, **COM3 and COM4 are Bluetooth** ports; ignore them. GUI alternative: Device Manager → Ports (COM & LPT), unplug and replug to see which entry changes.

### 2.2 Open and close the serial monitor

From the project folder, so crash addresses are decoded with `build\night-sentinel.elf`:

```powershell
cd D:\Projects\esp32-projects\projects\night-sentinel
idf.py -p COM5 monitor
```

- Exit: **`Ctrl+]`** ✅. Reset the board: `Ctrl+T` then `Ctrl+R` ⏳. All shortcuts: `Ctrl+T` then `Ctrl+H` ⏳.
- 115 200 baud (`CONFIG_ESPTOOLPY_MONITOR_BAUD`); on USB Serial/JTAG this doesn't limit speed.
- **Only one program can hold COM5.** Close every monitor (`Ctrl+]` in each window) before flashing from another terminal.

**Problems at this step**

| Symptom | Fix | Status |
|---|---|---|
| No board in the port list (only COM3/COM4) | Board not connected or cable data-less: reconnect, try another cable or USB port; Device Manager still lists it as "Unknown" when disconnected | ✅ 2026-10-23 |
| `could not open port 'COM5'` / `PermissionError(13, 'Access is denied.')` | Another monitor (other terminal, VS Code) holds the port; close it | ✅ 2026-10-23 |
| Port number changed | Different USB port can mean a different number; repeat [2.1](#21-find-the-serial-port) | ⏳ |

---

## 3. Build and flash

Run in an IDF terminal from `D:\Projects\esp32-projects\projects\night-sentinel`.

### 3.1 Configure

1. **First time in a fresh clone** ✅: creates `sdkconfig` from `sdkconfig.defaults`.

   ```powershell
   idf.py set-target esp32s3
   ```

   ⚠️ `set-target` runs `fullclean` and regenerates `sdkconfig`, so any setting not in `sdkconfig.defaults` is lost, including the optional fallback Wi-Fi in menuconfig (step 3). A login saved from the setup page lives in NVS (flash), so it survives this.

2. **Board settings** live in `sdkconfig.defaults` (committed) ✅:

   | Setting | Value | Why |
   |---|---|---|
   | `CONFIG_IDF_TARGET` | `"esp32s3"` | |
   | `CONFIG_ESPTOOLPY_FLASHMODE_DIO`, `..._FLASHSIZE_16MB` | `y` | 16 MB Winbond, DIO |
   | `CONFIG_SPIRAM`, `..._MODE_OCT`, `..._USE_MALLOC` | `y` | 8 MB octal PSRAM (quad mode is wrong for this board) |
   | `CONFIG_SPIRAM_SPEED_80M` | `y` | Default 40 MHz; 80 MHz halves camera frame drops while streaming |
   | `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240` | `y` | |
   | `CONFIG_PARTITION_TABLE_CUSTOM`, `..._CUSTOM_FILENAME` | `y`, `"partitions.csv"` | The default 1 MB app slot is too small |
   | `CONFIG_CAMERA_CORE1` | `y` | Camera copy task on core 1, away from Wi-Fi (core 0); measured no change in frame drops |

   `partitions.csv`: `nvs` 0x9000 24 KB · `phy_init` 0xf000 4 KB · `factory` 0x10000 **6 MB**.

   Do **not** enable `CONFIG_CAMERA_PSRAM_DMA`: it stopped the drops but let corrupted frames through ([issues log](issues_log.md)).

3. **Wi-Fi**: nothing needs to be set at build time. On first boot, or whenever it can't join its network, the board opens a setup hotspot and you choose a network from your phone ([4.2](#42-connect-to-wi-fi-setup-page)).

   Optional, in **`idf.py menuconfig` → Component config → Night Sentinel network** (`S` to save, `Esc` to exit):

   | Option | Default | Use |
   |---|---|---|
   | Wi-Fi SSID / password (optional fallback) | empty | Tried only while nothing has been saved from the setup page. ✅ `Airtel_Connect` set here |
   | Setup hotspot name | `NightSentinel-Setup` | |
   | Setup hotspot password | `sentinel123` | 8+ characters for WPA2. Shorter makes the hotspot open, so anyone nearby could see the password you type |

   These go into `sdkconfig`, which is git-ignored, so they never reach GitHub. Any other menuconfig change the project depends on must also be copied into `sdkconfig.defaults`.

4. **Component dependencies** come from the component manager. Versions resolved in `dependencies.lock` ✅:

   | Component | Version | Declared in |
   |---|---|---|
   | `espressif/esp32-camera` | 2.1.8 | `main/idf_component.yml` |
   | `espressif/esp_jpeg` | 1.3.1 | pulled in by esp32-camera |
   | `espressif/mdns` | 1.14.0 | `components/net/idf_component.yml` |

   They download into `managed_components/` on the next build. To add one: `idf.py add-dependency "<namespace>/<name>"` (or `--component <name>` to add it to that component), then `idf.py reconfigure`. The camera driver is **`espressif/esp32-camera`**; `espressif/camera` doesn't exist.

### 3.2 Build

```powershell
idf.py build
idf.py size        # memory summary
```

✅ 2026-11-06: ends with `Project build complete`.

| Measure | Value |
|---|---|
| App binary | 0xf7100 bytes (≈ 988 KB), 84 % of the 6 MB slot free (2026-11-06) |
| Internal RAM (DIRAM), static | 135 342 / 341 760 bytes (39.6 %); Wi-Fi and the camera also allocate at runtime |
| IRAM | 16 384 / 16 384 bytes. That's a small fixed block; 100 % is normal |

**Problems at this step**

| Symptom | Fix | Status |
|---|---|---|
| App grew only ~14 KB after adding Wi-Fi; board logs `no SSID` | Old firmware (before 2026-11-06): with an empty SSID the compiler removed all Wi-Fi code. Current firmware always builds Wi-Fi and falls back to the setup page | ✅ |
| `error: '%s' directive output may be truncated` (`-Werror=format-truncation`) | The `snprintf` buffer can't hold the longest possible result. Make the buffer bigger | ✅ 2026-11-06 |
| `unexpected EOF while looking for matching` in Git Bash | Shell quoting broke on a long command with mixed quotes; nothing ran. Edit files in the editor instead | ✅ 2026-11-06 |
| `multiple definition of 'cam_init'` | The camera driver already defines `cam_init`. Project functions use the `ns_` prefix; don't name a header `esp_camera.h` either | ✅ |
| `ninja: error: opening deps log: Permission denied` | A previous `idf.py` was killed mid-build and left `ninja.exe` running. Stop it (Task Manager or `Stop-Process -Name ninja`), then rebuild | ✅ |
| Stale configuration after changing Kconfig or dependencies | `idf.py reconfigure`; last resort `idf.py fullclean` | ⏳ |
| An edit "didn't work" | File not saved in VS Code (dot on the tab): `Ctrl+S`; check with `git status --short` | ✅ |

### 3.3 Flash 🔥

```powershell
idf.py -p COM5 flash monitor     # or "flash" alone, without the monitor
```

✅ Builds if needed, writes at 460 800 baud (`--flash-mode dio --flash-size 16MB --flash-freq 80m`), resets the board automatically, then opens the monitor. Success shows `Hash of data verified.` per image.

| Offset | Image |
|---|---|
| `0x0` | `build\bootloader\bootloader.bin` |
| `0x8000` | `build\partition_table\partition-table.bin` |
| `0x10000` | `build\night-sentinel.bin` |

**Problems at this step**

| Symptom | Fix | Status |
|---|---|---|
| `Could not open COM5, the port is busy or doesn't exist` | A monitor holds the port, or the board isn't connected ([2.2](#22-open-and-close-the-serial-monitor)) | ✅ |
| `Failed to connect to ESP32-S3` | Manual download mode: **hold BOOT, tap RST, release BOOT**, flash again | ⏳ never needed |

Also available, never run ⏳: erase the whole flash including NVS (Wi-Fi calibration data **and the Wi-Fi login saved from the setup page**), `idf.py -p COM5 erase-flash` 🔥 (not needed normally, since `flash` overwrites what it needs and keeps NVS); flash without idf.py, `python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset write-flash "@flash_args"` (from `build\`); read the flash ID, `python -m esptool --chip esp32s3 -p COM5 flash-id`.

---

## 4. Run and debug

### 4.1 Check the boot log

Current firmware (`main/app_main.c`): status LED + IR init → NVS → camera → I2C bus scan → Wi-Fi (or setup hotspot) → mDNS → debug web server → light log every 5 s. Expected log (✅ up to the I2C scan and the setup-mode lines, 2026-11-06; the online part ✅ 2026-10-23 with the older firmware, and its `http:` / `light:` lines are taken from the current code):

```
I (...) boot:  2 factory          factory app      00 00 00010000 00600000
I (...) esp_psram: Found 8MB PSRAM device
I (...) esp_psram: Speed: 80MHz
I (...) esp_psram: SPI SRAM memory test OK
I (...) camera: Detected OV3660 camera
I (...) ov3660: Calculated VCO: 240000000 Hz, PLLCLK: 120000000 Hz, SYSCLK: 30000000 Hz, PCLK: 15000000 Hz
I (...) camera: camera ready (sensor PID 0x3660)
I (...) main: camera init OK
I (...) camera: I2C scan: device at 0x3c (OV3660 camera)
I (...) camera: I2C scan: device at 0x53 (LTR-308 light sensor?)
I (...) camera: I2C scan: 2 device(s) on GPIO 8/9
I (...) camera: LTR-308 part ID 0xb1 (datasheet says 0xB1)
I (...) wifi: connecting to "<saved network>"
I (...) wifi: got IP 192.168.1.22
I (...) main: wifi OK
I (...) mdns: open http://night-sentinel.local/
I (...) http: debug server: port 80 /, /snap.jpg, /control, /status; port 81 /stream
I (...) main: light: luma 92.4, gain 1.94x, exposure 59.9 ms, IR off      ← every 5 s
```

If the saved network can't be reached (✅ 2026-11-06):

```
W (...) wifi: disconnected (reason 201), retry 1/3        ← 3 retries, ~10 s
W (...) wifi: couldn't join "Airtel_Connect": network not found (too far away, or not on 2.4 GHz) (reason 201)
I (...) esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 192.168.4.1
I (...) portal: scan found 4 networks
W (...) wifi: SETUP MODE: join Wi-Fi "NightSentinel-Setup" (password "sentinel123") and open http://192.168.4.1/
```

When online, the **status LED (GPIO3) turns on**, which is useful without a serial cable. LED off after boot means setup mode. An earlier `ov3660` line with `SYSCLK: 20000000 Hz` is the driver default, before the project's clock change.

| Log line | Meaning / fix |
|---|---|
| `W wifi: SETUP MODE: ...` | No saved network reachable: use the setup page ([4.2](#42-connect-to-wi-fi-setup-page)) |
| `W wifi: disconnected (reason N), retry k/3` | 201 = network not found (out of range, or 5 GHz only), 15 / 202 / 204 = wrong password, 210 / 211 = open/WEP network (refused). After 3 retries the board switches to setup mode |
| `W wifi: disconnected (reason N), reconnecting` | Lost the network after being online. Retries forever by itself |
| `W camera: LTR-308 not found at 0x53` | The light sensor didn't answer. Day/night falls back to frame brightness |
| `cam_hal: FB-SIZE: 138240 != 153600` (no `W`/`E` prefix) | A short frame the driver **silently dropped**. ~16 per minute while streaming is expected; constant drops mean the camera clocks are too fast |
| `cam_hal: Failed to get frame: timeout` every 4 s | No frames at all: the sensor clocks are inconsistent ([Appendix B](#appendix-b-camera-configuration)) |
| `E camera init failed` | Check camera pins against [Appendix A](#appendix-a-pins) |

### 4.2 Connect to Wi-Fi (setup page)

At boot the board tries the network saved from the setup page, or, if none has been saved, the menuconfig fallback. If that fails, it switches to setup mode:

1. On your phone, join Wi-Fi **`NightSentinel-Setup`**, password **`sentinel123`**. If the phone warns "no internet", choose to stay connected; turning off mobile data helps.
2. Open **`http://192.168.4.1/`**. It lists the networks the board can see, strongest first, with signal strength (−50 dBm is great, −80 dBm weak). Open/WEP networks are greyed out. **Scan again** refreshes the list.
3. Pick a network, or type its name under "Not in the list?" (a typed name wins over the list). Type the password (8–64 characters) and press **Connect**.
4. If it works, the hotspot disappears, the status LED turns on, and the login is saved in NVS for every later boot. If it fails, the page reloads after 25 s with the reason (wrong password / not found). Your phone may drop off the hotspot for a moment while the board tries; rejoin it if needed.

While no device is on the setup hotspot, the board retries every 30 s: first the last login typed into the page that hasn't worked yet, then the saved network. So it also comes back by itself after a router outage.

**Using your phone's own hotspot.** A phone can't share its hotspot while it's joined to another Wi-Fi, so it can't be on `NightSentinel-Setup` with its hotspot running. Either:
- **Do the setup from the laptop (easiest):** keep the phone hotspot on, join `NightSentinel-Setup` from the laptop, open `http://192.168.4.1/`, and pick the phone's hotspot from the list (it's there because the board scanned while it was on). Afterwards, put the laptop back on the phone hotspot.
- **Phone only:** on the page, type the hotspot's name in "Not in the list?" plus its password, then press Connect. The first attempt fails (the hotspot is off), and that's expected. Leave `NightSentinel-Setup`, turn the hotspot on, and wait ~30–45 s. The board joins, saves the login, and the board shows up in the hotspot's connected devices.

- **Only 2.4 GHz.** For an iPhone hotspot, turn on **Maximize Compatibility** (Settings → Personal Hotspot). ✅ The iPhone hotspot ran on 2.4 GHz channel 6 on 2026-11-06.
- **Laptop on the same network** as the board, or `night-sentinel.local` won't answer.
- **See what the laptop sees:** `netsh wlan show interfaces` (current network, band, channel) and `netsh wlan show networks mode=bssid` (nearby networks). ✅
- **Forget the saved network:** `idf.py -p COM5 erase-flash` 🔥 ⏳, then flash again. Usually not needed: a failing network leads to the setup page anyway.

Status: ✅ 2026-11-06: fallback to setup mode, the scan, and joining the iPhone hotspot (IP 172.20.10.14), then rejoining it from the saved login after each reflash.

### 4.3 View the camera and change settings

Two servers run so the stream doesn't block anything: **port 80** serves the page, snapshots, controls and status, and **port 81** serves only the live stream.

| What | How | Status |
|---|---|---|
| Live view + controls | Browser: `http://night-sentinel.local/`. IR on/off, gain ceiling, AE level, brightness, AEC2, greyscale; the status line updates every 2 s | ✅ 2026-11-06 |
| Live view in VLC | Media → Open Network Stream → `http://night-sentinel.local:81/stream` | ⏳ (curl: 109 frames in ~12 s ✅) |
| One snapshot | `http://night-sentinel.local/snap.jpg`; also works while a stream is open (HTTP 200 in 0.22 s) | ✅ |
| Change one setting | `http://night-sentinel.local/control?var=ir&val=1` (names below); unknown name → HTTP 400 | ✅ |
| Current state (JSON) | `http://night-sentinel.local/status`: `ir`, `als_lux` (LTR-308), `luma` (0–255), `gain`, `exposure_ms`, gain ceiling, settings | ✅ |
| Save snapshots | `cd D:\Projects\esp32-projects\projects\night-sentinel\tools` then `uv run grab_snaps.py night-sentinel.local -n 10` (`-t` interval, `-o` folder) → `test-results\day2\` | ✅ |
| Day 3 captures | `uv run capture_set.py tune --ir on` / `snap ...` / `light ...` ([tools/README.md](../tools/README.md)) | ⏳ |
| IP without mDNS | `arp -a`, line with MAC `28-84-85-5c-6c-cc` (DHCP may change the IP; reserve it in the router to fix it) | ✅ |

`/control` names, ranges, boot values and what each does: [Appendix B.3](#b3-live-settings-controlvarnamevaln). Registers via `/reg`: [B.4](#b4-registers-reference). Settings reset at reboot.

The stream is MJPEG encoded on the chip (JPEG quality 80): ~9 fps, ~8.5 KB per frame. The monitor logs `http: stream: N frames, X fps` every 50 frames and `stream closed after N frames` when the viewer leaves.

To run without the laptop, power it from a charger ([1.1](#11-hardware)) and use the addresses above.

### 4.4 Capture test

To re-measure camera capture, set `#define RUN_CAPTURE_TEST 1` in `main/app_main.c`, then flash. It grabs 1,000 frames before Wi-Fi starts and logs fps, NULL frames and free PSRAM. ✅ Expected: `grabbed 1000 frames in ~60200 ms, 0 failures, 16 fps`. **Also count the `FB-SIZE` lines:** they aren't NULL frames, and the loop doesn't see them. Set it back to 0 afterwards; it would compete with the stream for frames.

The brightness log (`main: light: ...` every 5 s) is controlled by `#define LOG_LIGHT_EVERY_MS 5000` in the same file; 0 turns it off.

### 4.5 Open issues

| Issue | Status |
|---|---|
No open issues (2026-11-06). Closed:

| Issue | Outcome |
|---|---|
| Horizontal lines in the **browser** live view | ✅ closed 2026-11-06: gone with the new page, which no longer uses `image-rendering: pixelated`. The frames were always clean |
| IR LEDs light the scene | ✅ closed 2026-11-06: checked by you in the dark. Also measured in room light: IR on cut sensor gain from 8.6× to 1.25× and exposure from 60 to 30 ms |
| Setup page / saved login | ✅ closed 2026-11-06: board joined the iPhone hotspot (172.20.10.14) and rejoins it from the saved login at every boot |
| Reading LTR-308 lux while the camera runs (shared bus) | ✅ closed 2026-11-06: 2 runs × 300 lux reads plus camera register reads at 10/s during full-speed capture: 0 I2C errors, 0 camera chip-ID mismatches, 0 NULL frames, 0 `FB-SIZE` drops, frame rate not lower ([4.6](#46-shared-i2c-bus-test)) |

Found during the bus test, for Day 5: with the scene at ~35 lux, capture ran at **11.4 fps** with no other load (Day 2: 16.6 fps). It wasn't start-up settling, power management, or I2C (the busy-bus phase was faster, 13.6–15.8 fps). Still above the detector's ≥6 fps; measure again on Day 5.

### 4.6 Shared I2C bus test

Set `#define RUN_LIGHT_BUS_TEST 1` in `main/app_main.c` and flash. Before Wi-Fi starts, the board grabs frames at full speed for 80 s: warm-up 10 s → camera only 20 s → camera + I2C (LTR-308 lux + camera chip-ID + AE registers, 10× per second) 30 s → camera only 20 s. Pass: 0 lux errors, 0 chip-ID mismatches, 0 NULL frames, no `FB-SIZE` lines between the phase markers, and the I2C phase's fps no lower than camera-only. Set it back to 0 afterwards (it delays boot by 80 s).

✅ 2026-11-06, run 2:

```
bus test: phase "warm-up" end: 111 frames in 10.0 s = 11.1 fps, 0 NULL
bus test: phase "camera only" end: 228 frames in 20.0 s = 11.4 fps, 0 NULL
bus test: phase "camera + I2C" end: 408 frames in 30.0 s = 13.6 fps, 0 NULL
bus test: LTR-308 300 ok / 0 errors (33.8..36.7 lux), camera chip-ID mismatches 0
bus test: phase "camera only again" end: 228 frames in 20.0 s = 11.4 fps, 0 NULL
```

Full history and measurements of every problem: [issues_log.md](issues_log.md).

---

## 5. Quick reference

| Task | Command (IDF terminal, in `D:\Projects\esp32-projects\projects\night-sentinel` unless noted) | Details |
|---|---|---|
| Load ESP-IDF | `. "C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1"` | [1.4](#14-open-an-esp-idf-terminal) |
| List COM ports | `[System.IO.Ports.SerialPort]::GetPortNames()` (any PowerShell) | [2.1](#21-find-the-serial-port) |
| First-time config | `idf.py set-target esp32s3` (wipes `sdkconfig`) | [3.1](#31-configure) |
| Wi-Fi setup | Phone → Wi-Fi `NightSentinel-Setup` / `sentinel123` → `http://192.168.4.1/` | [4.2](#42-connect-to-wi-fi-setup-page) |
| Settings (fallback Wi-Fi, hotspot name) | `idf.py menuconfig` → Night Sentinel network, `S` to save | [3.1](#31-configure) |
| Build / size | `idf.py build` · `idf.py size` | [3.2](#32-build) |
| Flash + monitor 🔥 | `idf.py -p COM5 flash monitor` | [3.3](#33-flash-) |
| Monitor only / exit | `idf.py -p COM5 monitor` · `Ctrl+]` | [2.2](#22-open-and-close-the-serial-monitor) |
| Re-resolve components | `idf.py reconfigure` | [3.1](#31-configure) |
| Live view / snapshot | `http://night-sentinel.local/` · `/snap.jpg` · stream `:81/stream` | [4.3](#43-view-the-camera-and-change-settings) |
| IR / camera setting | `/control?var=ir&val=1` · state: `/status` | [4.3](#43-view-the-camera-and-change-settings) |
| Save snapshots | `uv run grab_snaps.py night-sentinel.local -n 10` (in `tools\`, normal terminal) | [4.3](#43-view-the-camera-and-change-settings) |
| Day 3 captures | `uv run capture_set.py tune --ir on` · `snap --set night --label person --dist 3` · `light --lux N` | [tools/README.md](../tools/README.md) |
| Find the board's IP | `arp -a` → `28-84-85-5c-6c-cc` | [4.3](#43-view-the-camera-and-change-settings) |
| Laptop's Wi-Fi / nearby networks | `netsh wlan show interfaces` · `netsh wlan show networks mode=bssid` | [4.2](#42-connect-to-wi-fi-setup-page) |
| Check config values | `Select-String -Path sdkconfig -Pattern "SPIRAM_MODE\|FLASHSIZE\|NS_WIFI_SSID\|NS_PORTAL"` | [3.1](#31-configure) |

---

## Appendix A. Pins

Every GPIO the firmware uses is defined in [`components/board/include/board_pins.h`](../components/board/include/board_pins.h); add new pins there first. Pins were checked against DFRobot's example code, then the DFRobot wiki, then schematic v1.1.

**In the firmware** ✅

| Function | GPIO | `board_pins.h` | Notes |
|---|---|---|---|
| Status LED | 3 | `PIN_STATUS_LED` | Active-high. Power LED D8 is hard-wired to 3.3 V |
| IR LEDs | 47 | `PIN_IR_LED` | `IR_CTL` → EN of the SY7200A LED driver (U2), active-high. For dimming use LEDC **timer 1 / channel 1** |
| Camera | see below | `PIN_CAM_*` | From DFRobot [`5_3.ino`](https://github.com/DFRobot/DFR1154_Examples/blob/master/5.3%20CameraWebServer/source%20code/5_3/5_3.ino) lines 5–22 |
| Light sensor LTR-308 | shares SCCB 8 / 9 | `PIN_CAM_SIOD/SIOC` | I2C address **0x53**, part ID 0xB1. Driver `components/camera/light.c` (gain 9×, 100 ms, 18-bit). Safe to read while the camera runs ✅ 2026-11-06 |

| Camera signal | GPIO | `camera_config_t` field | Arduino name |
|---|---|---|---|
| PWDN / RESET | −1 / −1 (not connected) | `pin_pwdn` / `pin_reset` | `PWDN_` / `RESET_GPIO_NUM` |
| XCLK | 5 | `pin_xclk` | `XCLK_GPIO_NUM` |
| SCCB SDA / SCL | 8 / 9 | `pin_sccb_sda` / `pin_sccb_scl` | `SIOD_` / `SIOC_GPIO_NUM` |
| D0 – D7 | 16, 18, 21, 17, 14, 7, 6, 4 | `pin_d0` – `pin_d7` | **`Y2` – `Y9`** (offset by two) |
| VSYNC / HREF / PCLK | 1 / 2 / 15 | `pin_vsync` / `pin_href` / `pin_pclk` | `VSYNC_` / `HREF_` / `PCLK_GPIO_NUM` |

The camera's XCLK uses **LEDC timer 0 / channel 0**; nothing else may use them. The SCCB bus is **I2C port 1** with the new `i2c_master` driver (`CONFIG_SCCB_HARDWARE_I2C_DRIVER_NEW`). Other code on that bus must borrow its handle (`i2c_master_get_bus_handle(1, ...)`): mixing in the legacy I2C driver aborts at boot.

**Not in the firmware yet** (from DFRobot examples and wiki) ⏳

| Function | GPIO | Notes |
|---|---|---|
| microSD (SPI) | CS 10 · MOSI 11 · MISO 13 · SCK 12 | Day 9 |
| Speaker amp MAX98357 (I2S) | BCLK 45 · WS 46 · DOUT 42 | Day 7; no amp-enable pin in any example |
| Microphone (PDM) | CLK 38 · DATA 39 | not planned |
| BOOT button / Gravity UART | 0 / TX 43, RX 44 | |

**Avoid:** 19, 20 (USB D−/D+, the flashing port) · 26–32 (SPI flash) · 33–37 (octal PSRAM) · 0, 3, 45, 46 are **strapping pins**: no extra pull-ups or pull-downs.

## Appendix B. Camera configuration

Every camera setting the project uses: what it means, what it does, the value chosen and why. Code: `ns_cam_init()` in `components/camera/camera.c` (applied at every boot) and `/control` in `components/debug_http/debug_http.c` (changed live, lost at reboot). ✅ = verified on this board.

### B.1 Driver start-up settings (`camera_config_t`)

Passed once to `esp_camera_init()`. Changing any of them needs a reflash.

| Field | Value | Meaning | Why this value |
|---|---|---|---|
| `pin_pwdn`, `pin_reset` | −1, −1 | Power-down and reset pins | Not wired on the DFR1154; the driver resets the sensor over I2C instead |
| `pin_xclk` | 5 | Clock the ESP32 sends to the sensor | DFRobot pin map ([Appendix A](#appendix-a-pins)) |
| `pin_sccb_sda`, `pin_sccb_scl` | 8, 9 | SCCB = the camera's I2C control bus (register reads/writes) | Pin map. The LTR-308 light sensor shares it |
| `pin_d0`–`pin_d7` | 16, 18, 21, 17, 14, 7, 6, 4 | 8-bit parallel pixel data | Pin map (`Y2`–`Y9` in Arduino sketches) |
| `pin_vsync`, `pin_href`, `pin_pclk` | 1, 2, 15 | Frame start, line valid, pixel clock | Pin map |
| `xclk_freq_hz` | 20 MHz | Input clock to the sensor | DFRobot's value. The sensor's own PLL then sets the real speed (B.2) |
| `ledc_timer`, `ledc_channel` | timer 0, channel 0 | ESP32 PWM unit that generates XCLK | Reserved for the camera: nothing else (e.g. IR dimming) may use them |
| `pixel_format` | `PIXFORMAT_RGB565` | 2 bytes per pixel, uncompressed colour | The pedestrian detector accepts RGB565 directly, so no conversion. Byte order is big-endian (`RRRRRGGG GGGBBBBB`) |
| `frame_size` | `FRAMESIZE_QVGA` | 320×240 = 153,600 bytes per frame | The model resizes to 224×224 anyway; bigger frames cost speed and memory |
| `jpeg_quality` | 12 | JPEG quality (0–63, lower = better) | Unused for RGB565; the driver needs a value. Snapshots/stream use quality 80 in `frame2jpg` |
| `fb_count` | 2 | Number of frame buffers | One fills while the other is processed |
| `fb_location` | `CAMERA_FB_IN_PSRAM` | Where frame buffers live | 2 × 150 KB doesn't fit in internal RAM |
| `grab_mode` | `CAMERA_GRAB_LATEST` | What `esp_camera_fb_get()` returns | Always the newest frame, never a stale queued one |

### B.2 Sensor settings applied at boot (`ns_cam_init()`)

Written to the OV3660 right after `esp_camera_init()`.

| Setting | Code / registers | What it does | Value and reason |
|---|---|---|---|
| Vertical flip | `s->set_vflip(s, 1)` | Flips the image upside down | The sensor is mounted upside down on this board ✅ |
| Sensor clocks | `s->set_pll(s, 0, 12, 1, 0, 0, 2, 1, 4)` | Sets the sensor's internal clock (SYSCLK) and pixel clock (PCLK). Args: `bypass, multiplier, sys_div, root_2x, pre_div, seld5, pclk_manual, pclk_div` | SYSCLK 30 / PCLK 15 MHz → **16.6 fps** ✅ (driver default 20/10 MHz = 5.5 fps; 40/20 drops half the frames; 40/10 gives none). One frame = 2300 × 783 clocks; one line = **76.7 µs** (`NS_CAM_LINE_US`) |
| Anti-flicker band | `0x3a08/09` = 130, `0x3a0e` = 6 (50 Hz); `0x3a0a/0b` = 109, `0x3a0d` = 7 (60 Hz) | Mains lights pulse at 100/120 Hz; exposure is kept to whole 10 ms (8.3 ms) "bands" so frames don't flicker | Band = 10 ms / 76.7 µs = 130 lines. **Recalculate if the clocks change** ✅ |
| Gain ceiling | `ns_cam_set_gain_ceiling(16)` → `0x3a18/19` = 256 | Highest amplification auto-exposure may use | **16×** (driver default 15×). Day 3: 32× and 63× made the picture no brighter, since the sensor's real gain tops out near 16× ✅. Don't use the driver's `set_gainceiling()`: on the OV3660 it writes the wrong units (16× request → 0.19×) |
| Night mode (AEC2) | `s->set_aec2(s, 0)` → `0x3a00` bit 2 | Lets the sensor stretch frames up to 3× (max in `0x3a14/15` = 2352 lines = 180 ms) when still too dark at full gain | **Off at boot.** It's the only setting that adds real detail at night (luma 21 → 43 at 0.1 lux), but it costs ~2/3 of the frame rate (5.5 fps), and with it on the sensor kept 180 ms even at 18 lux room light ✅. Turn it on with the IR LEDs when dark |

Defaults left from the driver: auto gain on, auto exposure on, AE level 0, brightness 0, contrast 0, no special effect, auto white balance on, 50 Hz anti-flicker mode.

### B.3 Live settings (`/control?var=NAME&val=N`)

Also on the page at `http://night-sentinel.local/`. Applied immediately, lost at reboot. Auto-exposure needs 1–2 s to settle after a change. Read the current state with `/status`.

| `var` | Range (boot value) | What it does | Day 3 night result (0.1 lux, IR on) |
|---|---|---|---|
| `ir` | 0/1 (0) | IR LEDs on GPIO47 (940 nm) | Needed at night; in room light it cut gain 8.6× → 1.25× |
| `gainceiling` | 1–63× (16) | Maximum auto gain (B.2) | 15× → 63×: luma 21 → 29 only. Keep 16 |
| `aec2` | 0/1 (0) | Night mode (B.2) | Luma 21 → 43, real detail, but 180 ms exposure ≈ 5.5 fps and motion blur |
| `ae_level` | −2..2 (0) | Brightness target auto-exposure aims for | No effect at night (AE already at its limits). Leave 0 |
| `brightness` | −2..2 (0) | Adds an offset to every pixel after capture | Luma 21 → 45, but it only lifts black to grey: no new detail, more visible noise. Leave 0 |
| `contrast` | −2..2 (0) | Stretches the dark/light difference after capture | Not tested |
| `special_effect` | 0–6 (0) | Image effect after capture; **2 = greyscale** (others: negative, sepia, …) | Greyscale removes the purple IR tint, slightly darker (luma 17). Whether the detector prefers it is a Day 4 question |
| `gain_ctrl` | 0/1 (1) | 1 = auto gain, 0 = fixed gain from `agc_gain` | Leave auto |
| `exposure_ctrl` | 0/1 (1) | 1 = auto exposure, 0 = fixed exposure from `aec_value` | Leave auto |
| `agc_gain` | 0–64 | Fixed gain, only with `gain_ctrl=0` | For experiments |
| `aec_value` | 0–1200 | Fixed exposure, only with `exposure_ctrl=0` | For experiments |

### B.4 Registers reference

Read or write any of these live with `GET /reg?addr=0x3a14[&val=0x06]` (debug endpoint, lost at reboot).

| Register | Meaning | Notes |
|---|---|---|
| `0x300a/0x300b` | Chip ID | Always 0x36 0x60; the bus test uses it to detect I2C corruption |
| `0x3008` | System control | Bit 7 software reset; bit 6 **software power-down** (0x42 = camera stopped, 0x02 = running) ✅ |
| `0x3500–0x3502` | Current exposure | Lines = value >> 4; × 76.7 µs = time. Read by `ns_cam_read_ae` |
| `0x350a/0x350b` | Current gain | 10 bits in 1/16 steps (16 = 1×). Read by `ns_cam_read_ae` |
| `0x3a00` | AEC control | Bit 2 = night mode (0x3a off, 0x3e on) ✅ |
| `0x3a02/03`, `0x3a14/15` | Night-mode max exposure, 60 / 50 Hz | Default 0x0930 = 2352 lines (3 frames, 180 ms) ✅. A 2-frame cap (0x061E) can be written; whether the sensor obeys it in the dark is untested |
| `0x3a08–0x3a0e` | Anti-flicker band | See B.2 |
| `0x3a18/19` | Gain ceiling | 1/16 steps (16× = 256). Driver default 0x00F8 = 15×; project sets 16× |
| `0x380e/0x380f` | Frame length (lines) | 783 at QVGA with these clocks |

### B.5 Light sensor (LTR-308, `components/camera/light.c`)

| Setting | Value | Meaning / reason |
|---|---|---|
| I2C address | 0x53 on the camera bus | Part ID register 0x06 reads 0xB1 ✅ |
| `ALS_MEAS_RATE` (0x04) | 0x22 | 18-bit result, 100 ms per measurement (chip default) |
| `ALS_GAIN` (0x05) | 3 = 9× | 0.067 lux per count, saturates near 17,000 lux: fine indoors and at night |
| `MAIN_CTRL` (0x00) | 0x02 | Start measuring |
| Lux formula | 0.6 × counts / (gain × integration factor) | From the datasheet |

It barely sees the 940 nm IR LEDs (34 → 37 lux when IR came on), so it's the day/night signal. Frame brightness can't be used for that: auto-exposure keeps it flat.

### B.6 Related `sdkconfig` options

| Option | Value | Meaning / reason |
|---|---|---|
| `CONFIG_CAMERA_CORE1` | y | The driver's frame-copy task runs on core 1, away from Wi-Fi on core 0 |
| `CONFIG_CAMERA_PSRAM_DMA` | **not set** | Would stop the `FB-SIZE` drops, but lets corrupted frames through ([issues log](issues_log.md)) |
| `CONFIG_SPIRAM_SPEED_80M` | y | PSRAM at 80 MHz halves frame drops while streaming |
| `CONFIG_SCCB_HARDWARE_I2C_DRIVER_NEW`, `..._PORT1` | y | Camera bus uses the new `i2c_master` driver on port 1. Other code on the bus must borrow its handle; mixing in the legacy driver aborts at boot |
| `CONFIG_SCCB_CLK_FREQ` | 100000 | Camera bus at 100 kHz |
| `CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX` | 32768 | DMA chunk size (driver default) |

### B.7 Measurements

- Capture: **16.6 fps**, 0 NULL and 0 short frames in 1,000 (Wi-Fi off, 2026-10-23); PSRAM free 8,077,692 bytes before and after. On 2026-11-06 at ~35 lux, only 11.4 fps; open, see the [issues log](issues_log.md).
- Night sweep and settings comparison: [notes.md](notes.md), `test-results/day3/tune-20261106-140725/`.

## Appendix C. Other firmware flashed to this board

| Firmware | How | Result |
|---|---|---|
| ESP-IDF `hello_world` (toolchain smoke test) | `Copy-Item "$env:IDF_PATH\examples\get-started\hello_world" D:\Projects\hello_world -Recurse`, then in it `idf.py set-target esp32s3`, `idf.py build`, `idf.py -p COM5 flash monitor` | ✅ `esp32s3 chip with 2 CPU core(s) ... silicon revision v0.2`. Its default 2 MB flash setting gives `Detected size(16384k) larger than ...`; set 16 MB in menuconfig. To enable PSRAM there, add `esp_psram` to `PRIV_REQUIRES` in its `main/CMakeLists.txt` first (`MINIMAL_BUILD` hides the menu) |
| esp-dl `pedestrian_detect` (decided ESP-IDF v6.1) | In a scratch folder: `idf.py create-project-from-example "espressif/esp-dl:pedestrian_detect"`, `cd pedestrian_detect`, `idf.py set-target esp32s3`, `idf.py build`, `idf.py -p COM5 flash monitor` | ✅ 2026-10-04: 3 boxes, scores 0.884 / 0.879 / 0.786 (reference 0.884 / 0.884 / 0.805), ~130 ms. Uses a built-in JPEG, not the camera. App ~2.7 MB. esp-dl 3.3.x, pedestrian_detect 0.3.2 |
| DFRobot CameraWebServer prebuilt `.bin` ([DFR1154_Examples](https://github.com/DFRobot/DFR1154_Examples)) | ⏳ flashing method and offsets weren't recorded | ✅ stream worked |

Flash the project again afterwards with [3.3](#33-flash-).

## Appendix D. C types on the ESP32-S3

Checked against the v6.1 headers and `xtensa-esp32s3-elf-gcc`. The points that matter in this project's code:

| Type | On this chip | Use in `ESP_LOGx` |
|---|---|---|
| `char` | **unsigned** by default; use `int8_t` for signed bytes | |
| `int`, `long`, pointer, `size_t` | all 4 bytes | `%d`, `%ld`, `%p`, `%zu` |
| `int32_t` / `uint32_t` | **`long`**, not `int` | `%ld` / `%lu`, or `"%" PRId32` / `"%" PRIu32` (`<inttypes.h>`) |
| `int64_t` (e.g. `esp_timer_get_time()`, µs since boot) | `long long` | `%lld` |
| `float` / `double` | hardware FPU is single-precision; `double` is software and slower | `%f` |
| `esp_err_t` | `int`; `ESP_OK` = 0 | `%s` with `esp_err_to_name(err)` |
| `TickType_t` | `uint32_t`, 1 tick = 10 ms (`CONFIG_FREERTOS_HZ=100`) | use `pdMS_TO_TICKS(ms)` |

Camera driver types (esp32-camera 2.1.8): `camera_config_t` (pins are `int`, −1 = not connected), `camera_fb_t` (`buf`, `len`, `width`, `height`, `format`, `timestamp`), `sensor_t` (function pointers, called as `s->set_vflip(s, 1)`). Unit-test note ⏳: `gpio_get_level()` reads 0 on an output-only pin, so use `GPIO_MODE_INPUT_OUTPUT` or `gpio_dump_io_configuration()`.

## Still to confirm ⏳

- Exact EIM GUI options; whether a fresh clone + setup reproduces everything above.
- How the DFRobot CameraWebServer `.bin` was flashed.
- `erase-flash`, `flash-id`, raw `esptool write-flash`, monitor reset shortcut, VIN power, VLC live view: never run here.
- Power consumption.
- Why capture runs at 11.4 fps at ~35 lux, versus 16.6 fps on Day 2 (Day 5).
