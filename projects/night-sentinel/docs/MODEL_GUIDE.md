# Model guide: add a model, build it, test it on your own pictures

How to try a new model on the board without touching the rest of the firmware. Everything you edit is inside [`models/`](../models/). Commands are PowerShell.

Two terminals are used:

- **IDF terminal:** a PowerShell with ESP-IDF loaded, in `D:\Projects\esp32-projects\projects\night-sentinel` ([setup handbook, step 1.4](ESP32_SETUP_AND_COMMANDS.md#14-open-an-esp-idf-terminal)). For `idf.py`.
- **Tools terminal:** a normal PowerShell in `...\night-sentinel\tools`. For `uv run ...`.

## The idea in one picture

```text
camera frame ──▶ your model's run() ──▶ list of {label, score, box} ──▶ the app (alerts)
 320x240 RGB565      models/<name>/          at most 10 items
```

- A **model** is one folder in `models/`. The build compiles exactly one of them, the one picked in menuconfig.
- The model's **weights file** is not in the firmware. It sits on the SD card in `models/`, and the board reads it at start-up.
- The one header a model includes is [`platform/infer/include/infer.h`](../platform/infer/include/infer.h). It describes the frame going in and the results coming out.

## What you need before you start

| Thing | Why |
|---|---|
| A weights file the board can run | For ESP-DL that is an `.espdl` file (int8). See [Getting an .espdl file](#getting-an-espdl-file). |
| The model's input size and how its output is laid out | You write the code that turns its output into boxes or labels |
| A FAT32 SD card in the board | The weights are loaded from it |
| The board on your Wi-Fi | For uploading weights and for testing pictures |

Models that fit: small int8 CNNs with inputs up to about 224–320 pixels. Anything the size of YOLO11n takes seconds per frame, and transformer models are out of reach.

## Part 1: add a model

The example adds a model called `my_det`. Use lower-case letters, digits and `_`.

### 1. Copy a starting folder

| Your model is | Copy this folder |
|---|---|
| An ESP-DL detector with the same output layout as Espressif's Pico detectors | `models/pedestrian_pico` |
| Anything else | `models/dummy` (no network inside, all the plumbing in place) |

```powershell
Copy-Item -Recurse models\pedestrian_pico models\my_det
Remove-Item models\my_det\weights\*          # the old weights don't belong to the new model
```

### 2. Rename it in three files

In `models/my_det/`, replace `pedestrian_pico` with `my_det` and `PEDESTRIAN_PICO` with `MY_DET`:

| File | What to change |
|---|---|
| `CMakeLists.txt` | `CONFIG_NS_MODEL_PEDESTRIAN_PICO` → `CONFIG_NS_MODEL_MY_DET` |
| `Kconfig` | `NS_MODEL_PEDESTRIAN_PICO` → `NS_MODEL_MY_DET`, the text shown in the menu, and the names of the model's own settings (`NS_MY_DET_...`) |
| `model.cpp` | the settings names, the `ns_model_t` variable and `NS_MODEL_REGISTER(my_det)` at the bottom |

Every name must be different from the other models' names, or the build stops with a "redefinition" error.

### 3. Describe the model

At the bottom of `model.cpp` is the model's description. Set each line:

```cpp
const char *const s_labels[] = {"person"};          // class names, in the model's own order

const ns_model_t my_det = {
    .name = "my_det",                               // shown on the web page
    .task = NS_TASK_DETECT,                         // or NS_TASK_CLASSIFY (no boxes)
    .labels = s_labels,
    .n_labels = 1,
    .weights_file = "my_det_224_int8.espdl",        // file name on the SD card, in models/
    .init = model_init,
    .run = model_run,
    .set_param = model_set_param,                   // may be NULL
    .deinit = model_deinit,                         // may be NULL
};
```

Night Sentinel raises an alert only for a label called `person` (menuconfig → Night Sentinel app → "Model label that counts as a person"). A model without that label still runs and shows its results on the page; it never raises an alert.

### 4. Write the three functions

| Function | Called | What it must do |
|---|---|---|
| `model_init(weights, len)` | once at start-up | Build the network from the weights. `weights` points at the whole file in memory (16-byte aligned, stays valid). Return `ESP_OK`, or an error if the file is not usable. |
| `model_run(frame, out)` | once per frame, about 8 times a second | Preprocess the frame, run the network, fill `out->count` and `out->item[]` (label index, score 0–1, box corners in pixels of the frame). Best score first, at most 10. |
| `model_set_param(key, value)` | when the app or the page changes a setting | Optional. Night Sentinel sends `"score_thr"` = 0.30 at start-up. Return `ESP_ERR_NOT_FOUND` for keys you don't know. |

The frame is `frame->width` × `frame->height` pixels of RGB565 (2 bytes per pixel). `frame->format` says which byte comes first; camera frames are `NS_PIX_RGB565_BE`. Don't assume 320×240: pictures sent for testing can be any size.

In `pedestrian_pico/model.cpp` the three parts of `model_run` are done by ESP-DL objects set up in `load()`:

| Part | Object | What to change for another model |
|---|---|---|
| Resize and normalise | `dl::image::ImagePreprocessor(model, mean, std)` | `mean` and `std` to what the model was trained with (Pico uses 0 and 1) |
| The network | `dl::Model(...)` | nothing: it reads the input size from the `.espdl` file |
| Output → boxes | `dl::detect::PicoPostprocessor(...)` | A different detector family needs its own postprocessor class from ESP-DL's `vision/detect/`, with that model's strides |

### 5. Name the runtime

If the model needs a library, name it in two places in `models/my_det/`:

- `CMakeLists.txt`, in both `PRIV_REQUIRES` lines: `infer esp-dl`
- `idf_component.yml`: `espressif/esp-dl: "~3.3.14"`

A model that needs no library (like `dummy`) has `PRIV_REQUIRES infer` and no `idf_component.yml`.

### 6. Add the weights

Put the weights file in `models/my_det/weights/`, with exactly the name given in `.weights_file`.

To remove a model later, delete its folder. Nothing else refers to it.

## Part 2: build and flash

In the IDF terminal:

```powershell
idf.py menuconfig
```

1. Go to **Platform: inference → Model** and pick `my_det`.
2. The model's own settings are in a new menu, **Model: my_det**.
3. Press `S` to save, `Esc` to leave.

```powershell
idf.py build                       # changing the model rebuilds everything: about 10 minutes
idf.py -p COM5 flash monitor       # Ctrl+] leaves the monitor
```

Changing which model is built changes `sdkconfig`, so that first build is a full one. Later edits to `model.cpp` rebuild in under a minute.

To make the choice permanent for fresh clones, also change the `CONFIG_NS_MODEL_...=y` line in [`sdkconfig.defaults`](../sdkconfig.defaults).

### Put the weights on the SD card

In the tools terminal, with the board online:

```powershell
uv run sd.py push-model my_det     # copies models/my_det/weights/* to the card's models/ folder
uv run sd.py ls models             # check the name and size
```

Then **restart the board** (web page → Board → Restart the board, or unplug it). The model is loaded at start-up, before Wi-Fi, because that is when there is enough memory.

Without Wi-Fi: put the card in the PC and copy the file into a folder called `models`.

### Check that it loaded

| Where | What you should see |
|---|---|
| Serial monitor | `infer: model my_det (detector, 1 label), ...` then `infer: model my_det loaded in ... ms` |
| Web page, Inference | "**my_det** is loaded but stopped" and a Start button |
| LED | A short flash every 2 s. A fast blink means the model is switched on but could not be loaded |

Press **Start**. The page shows the speed (frames per second) and the model time in ms; the serial monitor prints one `T,` line per 50 frames.

### When it goes wrong

| What you see | Cause and fix |
|---|---|
| Model missing from the menuconfig list | The folder has no `Kconfig`, or its `choice NS_MODEL` block was changed. Compare with `models/dummy/Kconfig` |
| Build error `redefinition of ...` or `multiple definition` | A name in the new folder is still the old model's name (step 2) |
| Build error `No such file or directory` for a library header | The library is missing from `PRIV_REQUIRES` or `idf_component.yml` (step 5) |
| Serial: `no model in this build` | Model is set to "none" in menuconfig, or the `CMakeLists.txt` condition still has the old `CONFIG_...` name |
| Page: "weights file ... not found on the SD card" | The file name on the card is not exactly `.weights_file`, or there is no card. `uv run sd.py ls models` |
| Page: "model failed to load" | The file is not a model the runtime understands (wrong format, damaged upload). Push it again |
| Board restarts when the model loads | Not enough internal memory. Restart the board so the model loads at start-up, before Wi-Fi ([issues log, 2026-11-11](issues_log.md)) |
| Runs, but 1 fps or slower | The model is too big for this chip. Use a smaller input or a smaller network |
| Runs, never raises an alert | No label called `person`, or scores stay under the accept score (web page → Alarm) |

## Part 3: test it on your own pictures

All three ways below send pictures through the same code as a live camera frame, so the scores are the real on-device scores. The model has to be loaded; inference does not have to be switched on.

### A. Any pictures you have: `try_model.py`

In the tools terminal:

```powershell
uv run try_model.py C:\pics\door1.jpg                 # one picture
uv run try_model.py C:\pics\door1.jpg C:\pics\walk.png  # several
uv run try_model.py C:\pics\test_set                  # every picture in a folder
uv run try_model.py C:\pics\test_set --min-score 0.5
uv run try_model.py C:\pics\test_set --camera-size    # squeeze each one to 320x240 first
uv run try_model.py C:\pics\test_set --host 192.168.1.22
```

- **Formats:** jpg, png, bmp, webp, any size. Pictures are shrunk to at most 640 pixels on the long side and sent as JPEG.
- **Output:** a new folder `test-results\try\<date-time>\` with each picture redrawn with its boxes (green = counts, amber = does not), and `results.csv` with one row per box: file, label, score, corners, counts, model time.
- **On screen:** one line per picture ending in `FOUND` or `nothing`, then how many pictures had a box that counts.
- **What counts:** a score at or above `--min-score` (default 0.6, the alarm's accept score) and the alarm's size rules: the box is at least 10 % of the picture's height and at most 80 % of its area. `pedestrian_pico` often draws a box around the whole picture on dark, empty frames; the alarm ignores those and so does the tool. `--any-size` turns the size rules off, which you want for a model that is not a person detector.

For a fair test, use pictures that look like what the camera sees: same height, angle, distance and light. A sharp daylight phone photo tells you little about a dim 320×240 infrared frame.

### B. Pictures taken by the board's own camera

Take them through the board, then run A on the folder:

```powershell
uv run grab_snaps.py night-sentinel.local -n 20 -t 2 -o ..\test-results\myset   # 20 snapshots, 2 s apart
uv run try_model.py ..\test-results\myset
```

For a labelled set (person or empty, distance, pose) use `capture_set.py snap`, which adds a row per picture to `testdata\<set>\index.csv`:

```powershell
uv run capture_set.py snap --set night --label person --dist 2 --pose facing
uv run capture_set.py snap --set night --label empty -n 10
```

### C. Scored against labels: `eval_detector.py`

Counts how many labelled "person" pictures were found and how many "empty" ones gave a false box, at several score levels:

```powershell
uv run eval_detector.py --roboflow 0          # the own night set in testdata\night
uv run eval_detector.py                       # plus 200 pictures of the Roboflow infrared set, if downloaded
```

Output goes to `test-results\day4\eval-<date-time>\`: `summary.md` (the tables), `results.csv`, and the pictures with boxes. It applies the firmware's size filters, so "filtered" numbers are what the alarm would see. It expects a detector with a `person` label.

### D. Live

Web page → Inference → **Start**, camera view on "1 per second". The best box is drawn on the picture: green when it counts as a person, amber when it is below the accept score or the wrong size. "Score now" under the picture is the best score of the newest frame.

To record what the model saw over time, run `uv run serial_log.py --name mytest` (USB cable needed). It saves one line per frame with every box.

### Comparing two models

Run the same pictures through each and keep the output folders:

```powershell
uv run try_model.py ..\testdata\night --out ..\test-results\try\pedestrian_pico
# pick the other model in menuconfig, build, flash, push its weights, restart
uv run try_model.py ..\testdata\night --out ..\test-results\try\my_det
```

Then compare the two `results.csv` files, and the model time printed at the end of each run.

## Getting an .espdl file

The board cannot run a PyTorch, ONNX or Hugging Face checkpoint directly. It has to be converted to ESP-DL's int8 format first, on the PC.

| Source | What to do |
|---|---|
| Espressif's ready-made models (pedestrian, face, cat, ESPDet-Pico, …) | They ship as `.espdl` inside the ESP-DL model components. `pedestrian_pico` came from `espressif/pedestrian_detect`, folder `models/s3/` |
| Your own or a downloaded model (ONNX or PyTorch) | Quantize it with Espressif's **ESP-PPQ** tool, which writes the `.espdl` file. It needs a few hundred sample pictures to calibrate on; use frames from this camera (`testdata/`), not daylight photos |

Quantizing with ESP-PPQ has not been done in this project yet, so there is no tested script for it here. Espressif's instructions are in the [ESP-DL documentation](https://docs.espressif.com/projects/esp-dl/en/latest/), under "How to quantize model". Calibrating is not training: no weights are learned, the tool only measures value ranges.

Not every model converts. Layers ESP-DL does not have stop the conversion, and some models lose too much accuracy in int8. Check the converted model on the PC before building firmware around it.
