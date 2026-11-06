"""Day 3 capture tool: labelled test images and night-setting sweeps from the board.

Every image is saved together with the board's /status (IR state, frame brightness, sensor
gain, exposure, settings) as one row in a CSV, so later days can reuse the numbers.

  snap   Labelled test set for Day 4 -> <repo>/testdata/<set>/, rows in testdata/<set>/index.csv
           uv run capture_set.py snap --set night --label person --dist 3 --pose facing --lux 0.5
           uv run capture_set.py snap --set night --label empty -n 10 --lux 0.5
  tune   Try each night setting on its own, one snapshot each -> <repo>/test-results/day3/tune-<time>/
           uv run capture_set.py tune --ir on
  light  Log /status every few seconds (Ctrl+C to stop) -> test-results/day3/light.csv
           uv run capture_set.py light --lux 120

The board address defaults to night-sentinel.local; use --host 192.168.x.y if that doesn't resolve.
"""

import argparse
import csv
import json
import socket
import time
import urllib.request
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Each sweep step is applied on its own, starting from the settings the board had at the start.
TUNE_STEPS = [
    ("baseline", {}),
    ("ceiling_16x", {"gainceiling": 16}),
    ("ceiling_32x", {"gainceiling": 32}),
    ("ceiling_63x", {"gainceiling": 63}),
    ("aec2_on", {"aec2": 1}),
    ("ae_level_+1", {"ae_level": 1}),
    ("ae_level_+2", {"ae_level": 2}),
    ("brightness_+1", {"brightness": 1}),
    ("grey", {"special_effect": 2}),
    ("combo", {"gainceiling": 32, "aec2": 1, "ae_level": 1, "brightness": 1}),
]
RESTORE_KEYS = ["gainceiling", "aec2", "ae_level", "brightness", "special_effect"]

STATUS_FIELDS = ["ir", "als_lux", "luma", "gain", "exposure_ms", "exposure_lines", "gainceiling", "gain_ctrl",
                 "exposure_ctrl", "aec2", "ae_level", "brightness", "contrast", "special_effect"]


def resolve(host: str) -> str:
    """Look the name up once. On Windows, every .local lookup can take 10+ s, which made
    each request slow enough to time out (2026-11-06), so all requests use the IP."""
    t = time.time()
    ip = socket.gethostbyname(host)
    if ip != host:
        print(f"{host} -> {ip} ({time.time() - t:.1f} s to resolve; using the IP from now on)")
    return ip


def get(host: str, path: str, timeout: float = 10, tries: int = 3) -> bytes:
    for attempt in range(1, tries + 1):
        try:
            with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as resp:
                return resp.read()
        except OSError as e:
            if attempt == tries:
                raise
            print(f"  {path} failed ({e}), retrying ({attempt}/{tries - 1})")
            time.sleep(1)


def status(host: str) -> dict:
    return json.loads(get(host, "/status"))


def control(host: str, var: str, val: int) -> None:
    get(host, f"/control?var={var}&val={val}")


def set_ir(host: str, ir: str | None, settle: float) -> None:
    if ir is not None:
        control(host, "ir", 1 if ir == "on" else 0)
        time.sleep(settle)   # give auto-exposure time to adjust to the new light


def append_rows(csv_path: Path, rows: list[dict]) -> None:
    new = not csv_path.exists()
    with csv_path.open("a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        if new:
            w.writeheader()
        w.writerows(rows)


def cmd_snap(a: argparse.Namespace) -> None:
    out = REPO_ROOT / "testdata" / a.set
    out.mkdir(parents=True, exist_ok=True)
    set_ir(a.host, a.ir, a.settle)
    dist = f"_{a.dist:g}m" if a.dist is not None else ""
    pose = f"_{a.pose}" if a.pose else ""
    stem = f"{a.label}{dist}{pose}_{datetime.now():%Y%m%d-%H%M%S}"
    rows = []
    for i in range(a.count):
        st = status(a.host)
        jpg = get(a.host, "/snap.jpg")
        name = f"{stem}_{i:02d}.jpg"
        (out / name).write_bytes(jpg)
        rows.append({"file": name, "set": a.set, "label": a.label, "dist_m": a.dist or "",
                     "pose": a.pose or "", "lux": a.lux if a.lux is not None else "",
                     "time": datetime.now().isoformat(timespec="seconds"),
                     **{k: st.get(k, "") for k in STATUS_FIELDS}})
        print(f"[{i + 1}/{a.count}] {name}  luma {st['luma']}  gain {st['gain']}x  exp {st['exposure_ms']} ms")
        if i + 1 < a.count:
            time.sleep(a.interval)
    append_rows(out / "index.csv", rows)
    print(f"{len(rows)} images in {out}  (index.csv updated)")


def cmd_tune(a: argparse.Namespace) -> None:
    out = REPO_ROOT / "test-results" / "day3" / f"tune-{datetime.now():%Y%m%d-%H%M%S}"
    out.mkdir(parents=True, exist_ok=True)
    set_ir(a.host, a.ir, a.settle)
    start = status(a.host)
    base = {k: start[k] for k in RESTORE_KEYS}
    print(f"starting settings: {base}")
    rows = []
    try:
        for name, changes in TUNE_STEPS:
            for k, v in base.items():   # back to the starting point first
                control(a.host, k, v)
            for k, v in changes.items():
                control(a.host, k, v)
            time.sleep(a.settle)
            st = status(a.host)
            (out / f"{name}.jpg").write_bytes(get(a.host, "/snap.jpg"))
            rows.append({"step": name, "changes": json.dumps(changes),
                         **{k: st.get(k, "") for k in STATUS_FIELDS}})
            print(f"{name:14s} luma {st['luma']:6}  gain {st['gain']:6}x  exp {st['exposure_ms']:6} ms")
    finally:
        # Save what we have and leave the board as we found it, even after Ctrl+C or an error.
        if rows:
            append_rows(out / "tune.csv", rows)
        for k, v in base.items():
            try:
                control(a.host, k, v)
            except OSError as e:
                print(f"couldn't restore {k}={v}: {e} (a reboot resets all settings)")
    print(f"Compare the JPEGs side by side in {out}")


def cmd_light(a: argparse.Namespace) -> None:
    out = REPO_ROOT / "test-results" / "day3"
    out.mkdir(parents=True, exist_ok=True)
    print("Logging /status, Ctrl+C to stop")
    try:
        while True:
            st = status(a.host)
            row = {"time": datetime.now().isoformat(timespec="seconds"),
                   "lux": a.lux if a.lux is not None else "",
                   **{k: st.get(k, "") for k in STATUS_FIELDS}}
            append_rows(out / "light.csv", [row])
            print(f"{row['time']}  lux {row['lux']}  IR {st['ir']}  luma {st['luma']}  "
                  f"gain {st['gain']}x  exp {st['exposure_ms']} ms")
            time.sleep(a.interval)
    except KeyboardInterrupt:
        print(f"\nSaved to {out / 'light.csv'}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="night-sentinel.local", help="board name or IP (default night-sentinel.local)")
    ap.add_argument("--settle", type=float, default=2.0, help="seconds to wait after a setting change (default 2)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("snap", help="labelled test images for Day 4")
    s.add_argument("--set", choices=["day", "night"], required=True, help="testdata/<set>/")
    s.add_argument("--label", choices=["person", "empty"], required=True)
    s.add_argument("--dist", type=float, help="person's distance from the camera in metres")
    s.add_argument("--pose", choices=["facing", "side", "walking", "sitting"], help="what the person is doing")
    s.add_argument("--lux", type=float, help="phone lux-meter reading at the camera")
    s.add_argument("--ir", choices=["on", "off"], help="switch IR first (default: leave as is)")
    s.add_argument("-n", "--count", type=int, default=3, help="images to take (default 3)")
    s.add_argument("-t", "--interval", type=float, default=1.0, help="seconds between images (default 1)")
    s.set_defaults(func=cmd_snap)

    t = sub.add_parser("tune", help="one snapshot per night setting, to compare")
    t.add_argument("--ir", choices=["on", "off"], help="switch IR first (default: leave as is)")
    t.set_defaults(func=cmd_tune)

    lg = sub.add_parser("light", help="log brightness/gain/exposure over time")
    lg.add_argument("--lux", type=float, help="phone lux-meter reading, stored in every row")
    lg.add_argument("-t", "--interval", type=float, default=3.0, help="seconds between readings (default 3)")
    lg.set_defaults(func=cmd_light)

    a = ap.parse_args()
    a.host = resolve(a.host)
    a.func(a)


if __name__ == "__main__":
    main()
