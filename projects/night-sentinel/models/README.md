# models

One folder per model. This is the only place you work in when you try a new model: the firmware in `platform/` and the app in `app/` never change.

**Full manual (add a model, build it, test it on your own pictures): [docs/MODEL_GUIDE.md](../docs/MODEL_GUIDE.md).**

| Folder | What it is | Runtime | Weights file |
|---|---|---|---|
| [pedestrian_pico](pedestrian_pico/) | Espressif's pre-trained person detector (ESPDet-Pico, 224×224, int8). 125 ms per frame, 8 fps. | ESP-DL | `pedestrian_detect_pico_s8_v1.espdl` (435 KB) |
| [dummy](dummy/) | No neural network. Reports the brighter half of the picture. For testing the pipeline, and the starting point for a model that isn't an ESP-DL detector. | none | none |

## What is in a model folder

```text
models/my_model/
├── CMakeLists.txt       builds the model only when it is the chosen one
├── Kconfig              adds it to the "Model" list in menuconfig + its own settings
├── idf_component.yml    the runtime it needs (e.g. esp-dl); leave out if none
├── model.cpp (or .c)    init / run / set_param, and NS_MODEL_REGISTER(my_model)
└── weights/             the weights file(s) that go on the SD card
```

The whole agreement between firmware and model is one header: [platform/infer/include/infer.h](../platform/infer/include/infer.h). A camera frame goes in; up to 10 items come out, each a label, a score and (for detectors) a box.

## The short version

```powershell
idf.py menuconfig                      # Platform: inference -> Model -> pick one
idf.py build
idf.py -p COM5 flash monitor
cd tools
uv run sd.py push-model <name>         # weights to the SD card, then restart the board
uv run try_model.py <pictures or a folder>
```

One model is built into the firmware at a time, and only that model's code is compiled. Weights live on the SD card in `models/`, so new weights for the same model need no rebuild: push the file under the same name and restart the board.

To remove a model, delete its folder.
