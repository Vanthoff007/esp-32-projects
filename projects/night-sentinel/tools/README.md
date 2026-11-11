# tools

PC-side Python tools for night-sentinel. Not built into the firmware.

| Tool | Use it to |
|---|---|
| `sd.py` | browse, download, upload and delete files on the board's SD card |
| `try_model.py` | run the board's model on your own pictures |
| `eval_detector.py` | score the model against labelled picture sets |
| `grab_snaps.py`, `capture_set.py` | take snapshots and labelled test pictures with the board's camera |
| `serial_log.py` | record the serial output and per-frame results |
| `host_test.py`, `sweep.py` | test and tune the intrusion state machine on the PC |
| `plot_heartbeat.py` | check a long run for memory or speed drift |
| `make_audio.py` | make the spoken warning clip |
| `detect.py` | PC reference detector (YOLO), for comparison |

Run them from this folder with `uv run <tool> ...` in a normal terminal (not the ESP-IDF one). The board address defaults to `night-sentinel.local`; add `--host <IP>` where a tool talks to the board and that name is slow.

## detect.py

YOLO object detection on images, video files, or a live webcam, using the `ultralytics` package. In this project it serves as a reference detector: run it on the night/day JPEGs captured from the board (`/snap.jpg`) and compare its boxes with what the on-device model finds.

### Setup

```bash
cd tools
uv sync
```

The nano checkpoint (`yolo26n.pt`) downloads automatically on first run if not already present.

### Usage

```bash
# Run on an image
uv run detect.py -i path/to/image.jpg

# Run on a video file
uv run detect.py -v path/to/video.mp4

# Run on the default webcam (omit -i and -v)
uv run detect.py
```

`-i/--image_path` and `-v/--video_path` are mutually exclusive. If neither is given, the script opens the default webcam (`cv2.VideoCapture(0)`).

| Flag | Description | Default |
|------|-------------|---------|
| `-w, --weights` | Path to model weights (`.pt`) | `yolo26n.pt` |
| `-c, --confidence` | Minimum probability to filter weak detections | `0.5` |
| `-t, --threshold` | IoU threshold for Non-Max Suppression | `0.3` |
| `-u, --use_gpu` | Use GPU for inference | `False` |
| `-s, --save` | Save output to the `output/` directory | `False` |
| `-sh, --show` | Show output window | `True` |

During video/webcam playback, press `q` or `Ctrl+C` to exit.

## grab_snaps.py

Saves JPEG snapshots from the board's debug web server (`http://<board-ip>/snap.jpg`). It uses only the Python standard library. The board's IP is printed in the serial log as `wifi: got IP ...`.

```bash
# 10 snapshots, 1 s apart, into <repo>/test-results/day2/ (the default)
uv run grab_snaps.py 192.168.1.22

# 40 snapshots, 2 s apart, into another folder
uv run grab_snaps.py 192.168.1.22 -n 40 -t 2 -o ../test-results/day3
```

While a `/stream` viewer (browser or VLC) is open, the board can't serve `/snap.jpg`, so close the viewer first.

## capture_set.py (Day 3)

Pulls snapshots from the board and saves each one together with the board's `/status`: IR state, frame brightness (`luma`, 0–255), sensor gain, exposure in ms, and the current settings. Standard library only, so `uv sync` isn't needed. The board address defaults to `night-sentinel.local`; pass `--host <IP>` if that name doesn't resolve.

```bash
# Night settings: one snapshot per setting, so you can compare them side by side
uv run capture_set.py tune --ir on            # -> test-results/day3/tune-<time>/ + tune.csv

# Labelled test set for Day 4 (3 images per call by default)
uv run capture_set.py snap --set night --ir on --label person --dist 3 --pose facing --lux 0.5
uv run capture_set.py snap --set night --label empty -n 10 --lux 0.5
                                              # -> testdata/night/*.jpg + index.csv

# Day/night signal: log brightness/gain/exposure next to a phone lux reading
uv run capture_set.py light --lux 120         # -> test-results/day3/light.csv, Ctrl+C to stop
```

Settings can also be changed by hand from the page at `http://night-sentinel.local/`, or as `GET /control?var=NAME&val=N`. The names are `ir`, `gainceiling` (1–63×), `ae_level`, `brightness`, `contrast` (−2..2), `aec2`, `gain_ctrl`, `exposure_ctrl` (0/1), `special_effect` (2 = greyscale), `agc_gain`, and `aec_value`.

## sd.py

Browse the board's SD card over Wi-Fi: list, download, upload and delete files. Standard library only.

```bash
uv run sd.py ls                      # the card's root; `ls ev` for one folder
uv run sd.py tree rec                # every file under a folder
uv run sd.py pull events.csv         # download a file; `pull ev ..\sd_copy` for a whole folder
uv run sd.py push my.espdl models/   # upload a file
uv run sd.py push-model pedestrian_pico   # upload models/<name>/weights/* to the card's models/
uv run sd.py rm rec/old.avi          # delete a file; `rm -r Photos` for a folder and its contents
uv run sd.py clean                   # list what is not the project's; `clean --yes` deletes it
uv run sd.py df                      # free space
```

Add `--host <IP>` if `night-sentinel.local` is slow to resolve. The tool retries when the board drops the connection, which happens while the web page is open in a browser.

## try_model.py

Run the board's model on your own pictures: any files or folders, any size, jpg/png/bmp/webp.

```bash
uv run try_model.py C:\pics\door1.jpg
uv run try_model.py ..\testdata\night --min-score 0.5
uv run try_model.py C:\pics\set1 --camera-size        # squeeze each picture to 320x240 first
```

Output goes to `test-results/try/<date-time>/`: each picture with its boxes drawn, and `results.csv` (one row per box). The pictures go through the same code as live camera frames. Full walk-through: [docs/MODEL_GUIDE.md](../docs/MODEL_GUIDE.md#part-3-test-it-on-your-own-pictures).

## eval_detector.py

Score the model against labelled sets: how many "person" pictures were found, how many "empty" ones gave a false box, at score levels 0.3 to 0.7, with and without the firmware's box-size filters.

```bash
uv run eval_detector.py --roboflow 0     # the own night set (testdata/night, labels in index.csv)
uv run eval_detector.py                  # plus 200 pictures of the Roboflow infrared set, if downloaded
```

Output: `test-results/day4/eval-<date-time>/` with `summary.md`, `results.csv` and the pictures with boxes.

## serial_log.py

Record the board's serial output (USB cable) to `logs/<name>.log`. Lines starting with `F,` (one per frame, every box), `S,` (state changes) and `H,` (heartbeats) also go to their own CSV files.

```bash
uv run serial_log.py --name walk01             # until Ctrl+C
uv run serial_log.py --seconds 60 --reset      # reset the board first, stop after 60 s
```

Close `idf.py monitor` first: only one program can open the COM port.

## host_test.py

Build and run the PC-side unit tests of the intrusion state machine (`app/intrusion/intrusion_fsm.c`, compiled unchanged with zig cc). No board needed.

```bash
uv run host_test.py            # build + run: expect "12 tests, 33 checks, 0 failed"
```

## sweep.py

Replay recorded frame logs through the state machine for a grid of settings (accept score, K of N, minimum box height) and pick the one with no false alerts and the best recall. Writes `docs/tuning_sweep.md`.

```bash
uv run sweep.py --empty "../logs/empty_*_frames.csv" --walk "../logs/walk_*_frames.csv"
```

## plot_heartbeat.py

Plot free memory, fps and Wi-Fi signal over time from a heartbeat file, and check the soak-test rules.

```bash
uv run plot_heartbeat.py E:/heartbeat.csv           # from the SD card
uv run plot_heartbeat.py ../logs/soak_heartbeat.csv # from a serial log
```

## make_audio.py

Make the spoken warning clip with Windows' built-in speech engine and write it to `platform/alerts/assets/warn.pcm` (embedded in the firmware at the next build).

```bash
uv run make_audio.py --text "Warning. Person detected." --rate 0
```
