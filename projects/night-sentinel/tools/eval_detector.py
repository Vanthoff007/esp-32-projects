"""Day 4 offline scoring: send test images to the board's POST /detect and measure the detector.

Images go through exactly the live path (JPEG -> RGB565 big-endian -> pedestrian_detect at
score threshold 0.3), so near misses are visible too.

  uv run eval_detector.py                         # night set + 200 Roboflow NIR frames
  uv run eval_detector.py --host 192.168.1.22 --roboflow 0

Sets:
  testdata/night/            own night frames, labels in index.csv (label, dist_m, pose)
  testdata/roboflow/<set>/   Roboflow "person by infrared camera" (YOLO labels, 416x416 stretched);
                             resized back to 320x240, labels scaled to match

Output: test-results/day4/eval-<time>/ with results.csv, summary.md and annotated JPEGs.
Go/no-go rule (plan Day 4): night person frames at <= 4 m detected at score >= 0.5:
  >= 80 % go, 50-80 % gray zone, < 50 % no-go.
"""

import argparse
import csv
import json
import random
import time
import urllib.request
from collections import defaultdict
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np

REPO = Path(__file__).resolve().parent.parent
NIGHT = REPO / "testdata" / "night"
ROBOFLOW = REPO / "testdata" / "roboflow" / "person by infrared camera.v1i.yolov8"
THRESHOLDS = (0.3, 0.4, 0.5, 0.6, 0.7)

# The firmware's box filters (app/intrusion/include/intrusion_config.h): boxes smaller
# than 10 % of the frame height or covering > 80 % of it don't count as a person.
MIN_BOX_H, MAX_BOX_AREA = 0.10, 0.80


def passes(box, w=320, h=240):
    s, x1, y1, x2, y2 = box
    bh, bw = (y2 - y1) / h, (x2 - x1) / w
    return bh >= MIN_BOX_H and bw * bh <= MAX_BOX_AREA


def stretch(gray, lo_pct=1, hi_pct=99):
    lo, hi = np.percentile(gray, (lo_pct, hi_pct))
    return np.clip((gray.astype(np.float32) - lo) * 255.0 / max(hi - lo, 1), 0, 255).astype(np.uint8)


# Image fixes tried before the model (--pre). All give a 3-channel image again.
def preprocess(img, mode):
    if mode == "none":
        return img
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    if mode == "grey":
        out = gray
    elif mode == "stretch":
        out = stretch(gray)
    elif mode == "stretch_gamma":
        out = (255 * (stretch(gray) / 255.0) ** 0.7).astype(np.uint8)
    elif mode == "median_stretch":
        out = stretch(cv2.medianBlur(gray, 3))
    elif mode == "clahe":
        out = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(4, 4)).apply(stretch(gray))
    else:
        raise ValueError(mode)
    return cv2.cvtColor(out, cv2.COLOR_GRAY2BGR)


def detect(host: str, jpeg: bytes) -> dict:
    req = urllib.request.Request(f"http://{host}/detect", data=jpeg, method="POST",
                                 headers={"Content-Type": "image/jpeg"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read())


def iou(a, b) -> float:
    ix = max(0, min(a[2], b[2]) - max(a[0], b[0]))
    iy = max(0, min(a[3], b[3]) - max(a[1], b[1]))
    inter = ix * iy
    union = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / union if union > 0 else 0.0


def draw(img, boxes, truth, path: Path):
    out = img.copy()
    for t in truth:
        cv2.rectangle(out, (int(t[0]), int(t[1])), (int(t[2]), int(t[3])), (0, 255, 0), 1)
    for s, x1, y1, x2, y2 in boxes:
        colour = (0, 0, 255) if s >= 0.5 else (0, 200, 255)
        cv2.rectangle(out, (x1, y1), (x2, y2), colour, 2)
        cv2.putText(out, f"{s:.2f}", (x1 + 2, max(10, y1 + 12)), cv2.FONT_HERSHEY_SIMPLEX, 0.4, colour, 1)
    cv2.imwrite(str(path), out)


def load_night():
    rows = []
    with open(NIGHT / "index.csv", newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            p = NIGHT / r["file"]
            if p.exists():
                rows.append({"set": "night", "file": r["file"], "path": p, "label": r["label"],
                             "dist": r["dist_m"], "pose": r["pose"], "truth": []})
    return rows


def load_roboflow(n: int, seed: int = 4):
    imgs = sorted((ROBOFLOW / "test" / "images").glob("*.jpg")) + sorted((ROBOFLOW / "valid" / "images").glob("*.jpg"))
    random.Random(seed).shuffle(imgs)
    rows = []
    for p in (imgs if n < 0 else imgs[:n]):
        lbl = p.parent.parent / "labels" / (p.stem + ".txt")
        truth = []
        if lbl.exists():
            for line in lbl.read_text().split("\n"):
                parts = line.split()
                if len(parts) == 5:
                    _, cx, cy, w, h = map(float, parts)
                    # normalised centre/size -> pixel corners in the 320x240 resized image
                    truth.append([(cx - w / 2) * 320, (cy - h / 2) * 240, (cx + w / 2) * 320, (cy + h / 2) * 240])
        rows.append({"set": "roboflow", "file": p.name, "path": p, "label": "person" if truth else "empty",
                     "dist": "", "pose": "", "truth": truth})
    return rows


def rate(hits, total):
    return f"{hits}/{total} ({100 * hits / total:.0f} %)" if total else "-"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="night-sentinel.local")
    ap.add_argument("--roboflow", type=int, default=200, help="how many Roboflow frames (0 = skip, -1 = all test+valid)")
    ap.add_argument("--no-night", action="store_true", help="skip the own night frames")
    ap.add_argument("--no-images", action="store_true", help="don't save annotated images")
    ap.add_argument("--pre", default="none", help="image fix before the model: none, grey, stretch, "
                    "stretch_gamma, median_stretch, clahe (night set only)")
    args = ap.parse_args()

    out_dir = REPO / "test-results" / "day4" / f"eval-{datetime.now():%Y%m%d-%H%M%S}-{args.pre}"
    (out_dir / "img").mkdir(parents=True)

    rows = [] if args.no_night else load_night()
    if args.roboflow and ROBOFLOW.exists():
        rows += load_roboflow(args.roboflow)

    results = []
    t_start = time.time()
    for i, r in enumerate(rows):
        img = cv2.imread(str(r["path"]), cv2.IMREAD_COLOR)   # always 3 channels
        if r["set"] == "roboflow":
            img = cv2.resize(img, (320, 240), interpolation=cv2.INTER_AREA)   # undo the 416x416 stretch
            jpeg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 90])[1].tobytes()
        elif args.pre != "none":
            img = preprocess(img, args.pre)
            jpeg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 90])[1].tobytes()
        else:
            jpeg = r["path"].read_bytes()   # the board's own JPEG, unchanged
        try:
            res = detect(args.host, jpeg)
        except Exception as e:  # noqa: BLE001 - report and keep going
            print(f"{r['file']}: request failed: {e}")
            continue
        boxes = [tuple([b[0]] + [int(v) for v in b[1:]]) for b in res["boxes"]]
        best = max((b[0] for b in boxes), default=0.0)
        best_f = max((b[0] for b in boxes if passes(b)), default=0.0)   # what the firmware would count
        best_iou = 0.0
        hit_06 = 0   # a filtered box >= the firmware's accept score (0.6) that overlaps a labelled person
        for t in r["truth"]:
            for b in boxes:
                if b[0] >= 0.5:
                    best_iou = max(best_iou, iou(t, b[1:]))
                if b[0] >= 0.6 and passes(b) and iou(t, b[1:]) >= 0.3:
                    hit_06 = 1
        # tallest labelled person as a share of the frame height: a stand-in for distance
        truth_h = max(((t[3] - t[1]) / 240 for t in r["truth"]), default=0.0)
        results.append({**{k: r[k] for k in ("set", "file", "label", "dist", "pose")},
                        "n_boxes": len(boxes), "best_score": round(best, 3), "best_filtered": round(best_f, 3), "iou_at_0.5": round(best_iou, 3),
                        "truth_h": round(truth_h, 3), "hit_06": hit_06,
                        "ms": res["ms"], "boxes": json.dumps(boxes)})
        if not args.no_images and (r["set"] == "night" or i % 10 == 0):
            draw(img, boxes, r["truth"], out_dir / "img" / f"{best:.2f}_{r['file']}")
        print(f"[{i + 1}/{len(rows)}] {r['set']:8} {r['label']:6} {r['dist']:>4} best {best:.2f} "
              f"({len(boxes)} boxes, {res['ms']} ms)")

    with open(out_dir / "results.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(results[0].keys()))
        w.writeheader()
        w.writerows(results)

    # ---- summary ----
    lines = [f"# Detector evaluation {datetime.now():%Y-%m-%d %H:%M}", "",
             f"Board `{args.host}`, image fix `{args.pre}`, model threshold 0.3, {len(results)} images in {time.time() - t_start:.0f} s, "
             f"median {sorted(r['ms'] for r in results)[len(results) // 2]} ms per image.", ""]

    night = [r for r in results if r["set"] == "night"]
    persons = [r for r in night if r["label"] == "person"]
    empties = [r for r in night if r["label"] == "empty"]
    lines += ["## Own night frames (0.1 lux, IR on)", "",
              "| Distance | Pose | Frames | " + " | ".join(f"best >= {t}" for t in THRESHOLDS) + " | median best |",
              "|---|---|---|" + "---|" * len(THRESHOLDS) + "---|"]
    groups = defaultdict(list)
    for r in persons:
        groups[(float(r["dist"]), r["pose"])].append(r)
    for (d, pose), g in sorted(groups.items()):
        scores = sorted(r["best_score"] for r in g)
        lines.append(f"| {d:g} m | {pose or '-'} | {len(g)} | " +
                     " | ".join(rate(sum(s >= t for s in scores), len(g)) for t in THRESHOLDS) +
                     f" | {scores[len(scores) // 2]:.2f} |")
    lines += ["", "| Empty frames | Frames | " + " | ".join(f"any box >= {t}" for t in THRESHOLDS) + " |",
              "|---|---|" + "---|" * len(THRESHOLDS)]
    scenes = defaultdict(list)
    for r in empties:
        scenes[r["file"].split("_")[1]].append(r["best_score"])
    for scene, sc in sorted(scenes.items()):
        lines.append(f"| scene {scene} | {len(sc)} | " + " | ".join(rate(sum(s >= t for s in sc), len(sc)) for t in THRESHOLDS) + " |")
    all_empty = [r["best_score"] for r in empties]
    lines.append(f"| **all** | {len(all_empty)} | " + " | ".join(rate(sum(s >= t for s in all_empty), len(all_empty)) for t in THRESHOLDS) + " |")

    lines += ["", "The tables above count *any* box. Most high scores on dark frames are whole-frame boxes",
              "(0,0)-(319,239), which the firmware's area filter throws away. With the firmware's filters",
              f"(box height >= {MIN_BOX_H:.0%} of the frame, area <= {MAX_BOX_AREA:.0%}):", "",
              "| Night frames | Frames | " + " | ".join(f">= {t}" for t in THRESHOLDS) + " |",
              "|---|---|" + "---|" * len(THRESHOLDS)]
    for name, g in (("person <= 4 m", [r for r in persons if float(r["dist"]) <= 4]),
                    ("person > 4 m", [r for r in persons if float(r["dist"]) > 4]),
                    ("empty", empties)):
        lines.append(f"| {name} | {len(g)} | " + " | ".join(rate(sum(r["best_filtered"] >= t for r in g), len(g)) for t in THRESHOLDS) + " |")
    gate = [r for r in persons if float(r["dist"]) <= 4]
    gate_hits = sum(r["best_filtered"] >= 0.5 for r in gate)
    pct = 100 * gate_hits / len(gate) if gate else 0
    verdict = "GO" if pct >= 80 else "GRAY ZONE" if pct >= 50 else "NO-GO"
    false_06 = sum(r["best_filtered"] >= 0.6 for r in empties)
    lines += ["", f"**Go/no-go (night person frames at <= 4 m, filtered box >= 0.5): {gate_hits}/{len(gate)} = {pct:.0f} % -> {verdict}**"
              f" (empty frames with a filtered box >= 0.6: {false_06}/{len(empties)})", ""]

    rf = [r for r in results if r["set"] == "roboflow"]
    if rf:
        lines += ["## Roboflow NIR frames (resized to 320x240)", "",
                  "| Threshold | Detected (filtered box) | Detected with IoU >= 0.5 |", "|---|---|---|"]
        for t in THRESHOLDS:
            hit = sum(r["best_filtered"] >= t for r in rf if r["label"] == "person")
            n = sum(r["label"] == "person" for r in rf)
            line = f"| {t} | {rate(hit, n)} |"
            line += f" {rate(sum(r['iou_at_0.5'] >= 0.5 for r in rf if r['label'] == 'person'), n)} |" if t == 0.5 else " |"
            lines.append(line)

        # By person size. In this camera's frame a standing adult at ~1-1.5 m is >= 60 % of the
        # height; at 3 m ~35 %; at 5 m ~20 %.
        lines += ["", "By person size (tallest labelled person / frame height). Hit = a box that passes the "
                  "firmware's filters, score >= 0.6, overlapping the person (IoU >= 0.3):", "",
                  "| Person height | Images | Hit at 0.6 (one frame) | Filtered box >= 0.5 |", "|---|---|---|---|"]
        for lo, hi, name in ((0.0, 0.2, "< 20 % (far, > 5 m)"), (0.2, 0.4, "20-40 % (~3-5 m)"),
                             (0.4, 0.6, "40-60 % (~1.5-3 m)"), (0.6, 1.01, ">= 60 % (<= 1.5 m)")):
            g = [r for r in rf if r["label"] == "person" and lo <= r["truth_h"] < hi]
            lines.append(f"| {name} | {len(g)} | {rate(sum(r['hit_06'] for r in g), len(g))} | "
                         f"{rate(sum(r['best_filtered'] >= 0.5 for r in g), len(g))} |")
        emp = [r for r in rf if r["label"] == "empty"]
        if emp:
            lines.append(f"| empty images (false alarm) | {len(emp)} | "
                         f"{rate(sum(r['best_filtered'] >= 0.6 for r in emp), len(emp))} | "
                         f"{rate(sum(r['best_filtered'] >= 0.5 for r in emp), len(emp))} |")

    summary = "\n".join(lines) + "\n"
    (out_dir / "summary.md").write_text(summary, encoding="utf-8")
    print("\n" + summary)
    print(f"saved {out_dir}")


if __name__ == "__main__":
    main()
