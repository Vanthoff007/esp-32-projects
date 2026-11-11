# models

One folder per model. This is the only place you work in when you try a new model: the firmware in `platform/` and the app in `app/` never change.

| Folder | What it is | Runtime | Weights file |
|---|---|---|---|
| [pedestrian_pico](pedestrian_pico/) | Espressif's pre-trained person detector (ESPDet-Pico, 224×224, int8). 125 ms per frame, 8 fps. | ESP-DL | `pedestrian_detect_pico_s8_v1.espdl` (435 KB) |
| [dummy](dummy/) | No neural network. Reports the brighter half of the picture. For testing the pipeline, and the template for new models. | none | none |

## How a model plugs in

A model is three small files plus its weights:

```text
models/my_model/
├── CMakeLists.txt       builds the model only when it is the chosen one
├── Kconfig              adds it to the "Model" list in menuconfig + its own settings
├── idf_component.yml    the runtime it needs (e.g. esp-dl); leave out if none
├── model.cpp (or .c)    init / run / set_param, and NS_MODEL_REGISTER(my_model)
└── weights/             the weights file(s) that go on the SD card
```

The whole agreement between firmware and model is one header: [platform/infer/include/infer.h](../platform/infer/include/infer.h).

- **In:** one camera frame (`ns_frame_t`: 320×240 RGB565 pixels).
- **Out:** up to 10 items (`ns_result_t`), each a label index, a score from 0 to 1 and, for detectors, a box.
- **Inside the model, and nobody else's business:** resizing, normalising, the network, decoding its output, which runtime it uses.

The firmware reads the weights file from the SD card into PSRAM and hands the model a pointer to the bytes. Both ESP-DL (`.espdl`) and TensorFlow Lite Micro (`.tflite`) load a model from memory like that.

## Choosing the model for a build

One model is built into the firmware at a time. Only that model's code is compiled.

```powershell
idf.py menuconfig        # Platform: inference -> Model -> pick one
idf.py build
idf.py -p COM5 flash monitor
```

The chosen model's own settings appear in menuconfig as a menu named "Model: <name>".

## Putting weights on the SD card

Weights live on the card in `models/`, not in the firmware. From `tools/`:

```powershell
uv run sd.py push-model pedestrian_pico      # copies models/pedestrian_pico/weights/* to the card
uv run sd.py ls models                       # check
```

Or put the card in a PC and copy the file into a folder called `models`.

At boot the firmware loads the file before Wi-Fi starts. If the file isn't there, everything else still works; the web page says which file is missing, and the board tries again every 10 seconds while inference is switched on. Swapping weights of the **same** model needs no rebuild: push the new file under the same name and restart the board.

## Adding a model

1. Copy `models/dummy` to `models/my_model`.
2. In its three files, replace `dummy` with `my_model` and `DUMMY` with `MY_MODEL`.
3. In `model.c`: set `.labels`, `.task` and `.weights_file`, then write `model_init` (build the network from the weights) and `model_run` (frame in, items out). [pedestrian_pico/model.cpp](pedestrian_pico/model.cpp) shows a real ESP-DL detector in about 100 lines.
4. If it needs a runtime, name it in `PRIV_REQUIRES` in `CMakeLists.txt` and in an `idf_component.yml`.
5. Put the weights in `weights/`, push them to the card, pick the model in menuconfig, build and flash.

To remove a model, delete its folder.

Night Sentinel raises an alert for the label called `person` (menuconfig → Night Sentinel app). A model without that label still runs and shows its results; it just never raises an alert.

## Testing a model

| Check | How |
|---|---|
| It loads and runs | The web page shows the model name, fps and model time. Serial prints one `T,` line per 50 frames. |
| Accuracy on saved pictures | `uv run eval_detector.py` sends test images to the board's `POST /detect` and scores the boxes. Same code path as live frames. |
| Memory | Web page → Board: internal memory "lowest" should stay above about 10 KB. |
| Without a camera scene | Build with the `dummy` model: everything except the network is exercised. |

## What fits on this board

ESP32-S3, 240 MHz, 8 MB PSRAM, no GPU. Models must be int8 (quantized) and small.

- **Works:** small CNN detectors and classifiers at inputs up to about 224–320 px: ESPDet-Pico, the ESP-DL model zoo, MobileNetV2/V3-small, the 96×96 Visual Wake Words person model.
- **Too slow to be useful:** YOLO11n-size detectors (seconds per frame), anything transformer-based.

Converting and quantizing a pre-trained model (PyTorch/ONNX → `.espdl`, calibrated on the pictures in `testdata/`) is the next piece of work; it is not in this folder yet.
