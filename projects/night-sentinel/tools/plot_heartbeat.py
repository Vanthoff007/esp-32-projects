"""Day 13 soak analysis: plot the heartbeat (free memory, fps) over time and check the trend.

Input: heartbeat.csv from the SD card, or a serial log's *_heartbeat.csv (lines "H,t_ms,...").
  uv run plot_heartbeat.py E:/heartbeat.csv
  uv run plot_heartbeat.py ../logs/soak_heartbeat.csv

Writes <input>.png next to the input and prints the pass/fail checks from the plan:
  minimum free internal heap flat within +-5 % after the first hour; fps steady.
"""

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

COLS = ["uptime_s", "fps", "infer_ms", "free_int", "min_int", "free_psram", "min_psram", "rssi", "events",
        "state", "ir", "lux", "sd_free_mb", "null_frames"]


def load(path: Path):
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for rec in csv.reader(f):
            if not rec or not rec[0][:1].isdigit() and rec[0] != "H":
                continue
            if rec[0] == "H":
                rec = rec[2:]   # serial form: H,t_ms,<fields>
            if len(rec) < len(COLS) or not rec[0].isdigit():
                continue
            rows.append(dict(zip(COLS, rec)))
    # Several boots may be in one file (uptime restarts): keep the last run.
    start = 0
    for i in range(1, len(rows)):
        if int(rows[i]["uptime_s"]) < int(rows[i - 1]["uptime_s"]):
            start = i
    if start:
        print(f"note: {start} rows from earlier boots skipped")
    return rows[start:]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = Path(sys.argv[1])
    rows = load(path)
    if len(rows) < 3:
        sys.exit("not enough heartbeat rows")
    hours = [int(r["uptime_s"]) / 3600 for r in rows]
    series = {k: [float(r[k]) for r in rows] for k in ("fps", "free_int", "min_int", "free_psram", "rssi")}

    fig, ax = plt.subplots(4, 1, figsize=(10, 9), sharex=True)
    ax[0].plot(hours, [v / 1024 for v in series["free_int"]], label="free internal")
    ax[0].plot(hours, [v / 1024 for v in series["min_int"]], label="minimum ever")
    ax[0].set_ylabel("internal KB"); ax[0].legend(); ax[0].axhline(40, color="r", lw=0.8, ls="--")
    ax[1].plot(hours, [v / 1024 / 1024 for v in series["free_psram"]]); ax[1].set_ylabel("PSRAM free MB")
    ax[2].plot(hours, series["fps"]); ax[2].set_ylabel("fps"); ax[2].axhline(6, color="r", lw=0.8, ls="--")
    ax[3].plot(hours, series["rssi"]); ax[3].set_ylabel("Wi-Fi dBm"); ax[3].set_xlabel("hours since boot")
    fig.suptitle(f"Heartbeat: {path.name}")
    fig.tight_layout()
    png = path.with_suffix(".png")
    fig.savefig(png, dpi=110)

    after = [r for r, h in zip(rows, hours) if h >= 1] or rows
    mins = [float(r["min_int"]) for r in after]
    drift = (max(mins) - min(mins)) / max(mins) * 100
    fps = [float(r["fps"]) for r in rows if float(r["fps"]) > 0]
    print(f"duration {hours[-1]:.2f} h, {len(rows)} heartbeats, events {rows[-1]['events']}")
    print(f"min free internal after 1 h: {min(mins):.0f}..{max(mins):.0f} B, drift {drift:.1f} % "
          f"-> {'PASS' if drift <= 5 else 'FAIL'} (<= 5 %)")
    print(f"lowest min free internal: {min(series['min_int']):.0f} B -> {'PASS' if min(series['min_int']) >= 40960 else 'below 40 KB target'}")
    print(f"fps {min(fps):.2f}..{max(fps):.2f}, mean {sum(fps) / len(fps):.2f} -> {'PASS' if min(fps) >= 6 else 'below 6'}")
    print(f"saved {png}")


if __name__ == "__main__":
    main()
