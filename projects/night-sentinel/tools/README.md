# tools

PC-side Python tools for night-sentinel. Not built into the firmware.

## detect.py

YOLO object detection on images, video files, or a live webcam, using the `ultralytics` package. In this project it serves as a reference detector: run it on the night/day JPEGs captured from the board (`/snap.jpg`) and compare its boxes with what the on-device `pedestrian_detect` model finds.

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
