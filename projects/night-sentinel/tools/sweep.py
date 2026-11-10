"""Day 11 threshold sweep: replay recorded frame logs through the state machine for every
parameter combination, and pick the setting with no false alerts and the best recall.

Record the logs first (tools/serial_log.py writes logs/<name>_frames.csv):
  empty scene:  uv run serial_log.py --quiet --name empty_dark_01      (Ctrl+C after 60 min)
  walk-through: uv run serial_log.py --quiet --name walk_3m_01         (one walk per file)

Then:
  uv run sweep.py --empty "../logs/empty_*_frames.csv" --walk "../logs/walk_*_frames.csv"

A walk file counts as detected if it produced at least one alert. Writes docs/tuning_sweep.md
(and prints it). Grid from the plan: ACCEPT_SCORE x K/N x MIN_BOX_H.
"""

import argparse
import glob
import itertools
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REPLAY = REPO / "build_host" / ("replay.exe" if sys.platform == "win32" else "replay")

ACCEPT = (0.5, 0.55, 0.6, 0.65, 0.7)
KN = ((2, 4), (3, 5), (4, 6))
MIN_H = (0.08, 0.10, 0.14)


def run(path: str, accept, k, n, min_h):
    out = subprocess.run([str(REPLAY), "--accept", str(accept), "--k", str(k), "--n", str(n),
                          "--min-h", str(min_h), path], capture_output=True, text=True, check=True).stdout
    alerts = [int(m.group(1)) for m in re.finditer(r"^E,(\d+),", out, re.M)]
    s = re.search(r"SUMMARY,frames=(\d+),duration_s=([\d.]+)", out)
    first_t = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.split(",")
            if parts[0] == "F" and len(parts) > 1 and parts[1].isdigit():   # skips the header row
                first_t = int(parts[1])
                break
    return {"alerts": alerts, "frames": int(s.group(1)), "secs": float(s.group(2)), "first_t": first_t}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--empty", default="", help="glob of empty-scene frame logs")
    ap.add_argument("--walk", default="", help="glob of walk-through frame logs (one walk each)")
    ap.add_argument("--out", default=str(REPO / "docs" / "tuning_sweep.md"))
    args = ap.parse_args()

    if not REPLAY.exists():
        sys.exit("build the replay tool first: uv run host_test.py --build")
    empty = sorted(glob.glob(args.empty)) if args.empty else []
    walks = sorted(glob.glob(args.walk)) if args.walk else []
    if not empty and not walks:
        sys.exit("no logs matched --empty / --walk")
    empty_hours = sum(run(p, 0.6, 3, 5, 0.1)["secs"] for p in empty) / 3600

    rows = []
    for accept, (k, n), min_h in itertools.product(ACCEPT, KN, MIN_H):
        false_alerts = sum(len(run(p, accept, k, n, min_h)["alerts"]) for p in empty)
        detected, delays = 0, []
        for p in walks:
            r = run(p, accept, k, n, min_h)
            if r["alerts"]:
                detected += 1
                delays.append((r["alerts"][0] - r["first_t"]) / 1000)
        rows.append({"accept": accept, "k": k, "n": n, "min_h": min_h, "false": false_alerts,
                     "fph": false_alerts / empty_hours if empty_hours else 0,
                     "recall": detected / len(walks) if walks else 0, "detected": detected,
                     "delay": sorted(delays)[len(delays) // 2] if delays else None})

    # Zero false alerts first, then recall, then the fastest confirmation (smaller K), then the
    # highest score threshold (most margin against false alerts).
    rows.sort(key=lambda r: (r["false"], -r["recall"], r["k"], -r["accept"]))
    ok = [r for r in rows if r["false"] == 0 and r["recall"] >= 0.9]
    best = ok[0] if ok else rows[0]

    lines = ["# Threshold sweep", "",
             f"Empty-scene logs: {len(empty)} ({empty_hours:.2f} h). Walk logs: {len(walks)}.", "",
             "| accept | K/N | min box h | false alerts | per hour | walks detected | median time to alert |",
             "|---|---|---|---|---|---|---|"]
    for r in rows:
        mark = " **<- chosen**" if r is best else ""
        delay = f"{r['delay']:.1f} s" if r["delay"] is not None else "-"
        lines.append(f"| {r['accept']} | {r['k']}/{r['n']} | {r['min_h']} | {r['false']} | {r['fph']:.2f} | "
                     f"{r['detected']}/{len(walks)} | {delay} |{mark}")
    lines += ["", f"Chosen: accept {best['accept']}, K/N {best['k']}/{best['n']}, min box h {best['min_h']}"
              + ("" if ok else "  (NOTE: no setting reached 0 false alerts with >= 90 % recall)"), ""]
    text = "\n".join(lines)
    Path(args.out).write_text(text, encoding="utf-8")
    print(text)
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
