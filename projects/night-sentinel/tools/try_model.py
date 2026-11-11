"""Run the board's model on your own pictures and see what it finds.

  uv run try_model.py photo.jpg                     # one picture
  uv run try_model.py C:\\pics\\walk1.png door.jpg    # several
  uv run try_model.py ..\\testdata\\night             # every picture in a folder
  uv run try_model.py ..\\testdata\\night --min-score 0.5 --host 192.168.1.22

Each picture is sent to the board (POST /detect) and goes through exactly the code a camera
frame goes through, so the scores are the real on-device scores. Inference does not have to
be switched on for this; the model only has to be loaded (the web page shows that).

Any format OpenCV can read works (jpg, png, bmp, webp), at any size: pictures are shrunk to
at most 640 pixels on the long side and sent as JPEG. The camera itself is 320x240, so for a
fair test use pictures that look like what the camera sees. Add --camera-size to squeeze
every picture to exactly 320x240 first.

Output, in test-results/try/<date-time>/:
  <name>.jpg     the picture with the boxes drawn (green = counts, amber = does not)
  results.csv    one row per box: file, label, score, corners, counts (1/0), model time

A box "counts" when its score is at or above --min-score and it passes Night Sentinel's size
rules (app/intrusion/include/intrusion_config.h): at least 10 % of the picture's height and at
most 80 % of its area. This model often puts a box around the whole picture on dark, empty
frames; the alarm ignores those, so this tool does too. --any-size switches the size rules off.
"""

import argparse
import csv
import json
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

import cv2

REPO = Path(__file__).resolve().parent.parent
KINDS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
MAX_SIDE = 640            # longer pictures are shrunk before sending
MAX_BYTES = 380 * 1024    # the board accepts JPEGs up to 400 KB
MIN_BOX_H, MAX_BOX_AREA = 0.10, 0.80   # the firmware's box-size rules (intrusion_config.h)


def find_pictures(args):
    out = []
    for a in args:
        p = Path(a)
        if p.is_dir():
            out += sorted(f for f in p.iterdir() if f.suffix.lower() in KINDS)
        elif p.is_file():
            out.append(p)
        else:
            print(f"skipped {a}: not a file or folder")
    return out


def to_jpeg(img, camera_size: bool):
    """Returns (the picture as sent, its JPEG bytes)."""
    if camera_size:
        img = cv2.resize(img, (320, 240), interpolation=cv2.INTER_AREA)
    else:
        h, w = img.shape[:2]
        k = MAX_SIDE / max(h, w)
        if k < 1:
            img = cv2.resize(img, (round(w * k), round(h * k)), interpolation=cv2.INTER_AREA)
    for quality in (92, 85, 75, 60):
        ok, buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, quality])
        if ok and len(buf) <= MAX_BYTES:
            return img, buf.tobytes()
    sys.exit("could not make the picture small enough to send")


def detect(host: str, jpeg: bytes) -> dict:
    req = urllib.request.Request(f"http://{host}/detect", data=jpeg, method="POST",
                                 headers={"Content-Type": "image/jpeg"})
    for attempt in range(4):   # the board drops connections when a browser has its page open
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            sys.exit(f"board says {e.code}: {e.read().decode(errors='replace').strip()}")
        except OSError as e:
            last = e
            time.sleep(1.5)
    sys.exit(f"can't reach http://{host} ({last}). Is the board on, and on this Wi-Fi? Try --host <IP>.")


def main():
    ap = argparse.ArgumentParser(description="Run the board's model on your own pictures.")
    ap.add_argument("pictures", nargs="+", help="picture files and/or folders")
    ap.add_argument("--host", default="night-sentinel.local", help="board address (default: night-sentinel.local)")
    ap.add_argument("--min-score", type=float, default=0.6,
                    help="boxes at or above this are drawn green and counted as found (default 0.6, the firmware's accept score)")
    ap.add_argument("--any-size", action="store_true", help="count boxes of any size (skip the alarm's box-size rules)")
    ap.add_argument("--camera-size", action="store_true", help="squeeze every picture to 320x240 first, like a camera frame")
    ap.add_argument("--out", default=None, help="output folder (default: test-results/try/<date-time>)")
    args = ap.parse_args()

    pics = find_pictures(args.pictures)
    if not pics:
        sys.exit("no pictures found")
    out = Path(args.out) if args.out else REPO / "test-results" / "try" / datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)

    rows, found, model, times = [], 0, "?", []
    for i, p in enumerate(pics, 1):
        img = cv2.imread(str(p))
        if img is None:
            print(f"[{i}/{len(pics)}] {p.name}: can't be read as a picture, skipped")
            continue
        sent, jpeg = to_jpeg(img, args.camera_size)
        r = detect(args.host, jpeg)
        model, labels = r.get("model", "?"), r.get("labels", [])
        times.append(r["ms"])
        boxes = r["boxes"]
        classes = r.get("cls", [0] * len(boxes))
        best, ignored = 0.0, 0
        for (score, x1, y1, x2, y2), c in zip(boxes, classes):
            label = labels[c] if c < len(labels) else f"class {c}"
            bh, bw = (y2 - y1) / r["h"], (x2 - x1) / r["w"]
            size_ok = args.any_size or (bh >= MIN_BOX_H and bh * bw <= MAX_BOX_AREA)
            good = score >= args.min_score and size_ok
            ignored += not size_ok
            colour = (120, 220, 90) if good else (65, 180, 242)
            cv2.rectangle(sent, (x1, y1), (x2, y2), colour, 2)
            cv2.putText(sent, f"{label} {score:.2f}", (x1 + 3, max(12, y1 + 14)), cv2.FONT_HERSHEY_SIMPLEX, 0.45, colour, 1, cv2.LINE_AA)
            rows.append([p.name, label, f"{score:.3f}", x1, y1, x2, y2, int(good), r["w"], r["h"], r["ms"]])
            if size_ok:
                best = max(best, score)
        if not boxes:
            rows.append([p.name, "", "", "", "", "", "", 0, r["w"], r["h"], r["ms"]])
        found += best >= args.min_score
        cv2.imwrite(str(out / (p.stem + ".jpg")), sent)
        print(f"[{i}/{len(pics)}] {p.name}: {len(boxes)} box(es), best counted score {best:.2f}, {r['ms']} ms"
              + (f", {ignored} ignored for size" if ignored else "")
              + ("  -> FOUND" if best >= args.min_score else "  -> nothing"))

    with open(out / "results.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "label", "score", "x1", "y1", "x2", "y2", "counts", "sent_w", "sent_h", "model_ms"])
        w.writerows(rows)
    if times:
        print(f"\nmodel {model}: {found} of {len(times)} picture(s) have a box that counts (score {args.min_score} or more); "
              f"model time {sum(times) / len(times):.0f} ms on average")
    print(f"pictures with boxes and results.csv are in {out}")


if __name__ == "__main__":
    main()
