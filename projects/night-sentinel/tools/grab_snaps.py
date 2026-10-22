"""Save JPEG snapshots from the board's /snap.jpg endpoint.

Images go to <repo>/test-results/day2 by default, whichever folder you run this from.

Usage:
    uv run grab_snaps.py 192.168.1.22                     # 10 images, 1 s apart
    uv run grab_snaps.py 192.168.1.22 -n 40 -t 2 -o ../test-results/day3
"""

import argparse
import time
import urllib.request
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "test-results" / "day2"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ip", help="board IP address from the serial log ('wifi: got IP ...')")
    ap.add_argument("-n", "--count", type=int, default=10, help="number of snapshots (default 10)")
    ap.add_argument("-t", "--interval", type=float, default=1.0, help="seconds between snapshots (default 1)")
    ap.add_argument("-o", "--out", type=Path, default=DEFAULT_OUT, help=f"output folder (default {DEFAULT_OUT})")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    url = f"http://{args.ip}/snap.jpg"
    print(f"Saving {args.count} snapshots from {url} to {args.out.resolve()}")

    for i in range(args.count):
        try:
            with urllib.request.urlopen(url, timeout=10) as resp:
                data = resp.read()
        except OSError as e:
            # A /stream open in a browser or VLC blocks /snap.jpg until it closes.
            print(f"[{i + 1}/{args.count}] failed: {e} (close any open /stream viewer)")
        else:
            name = args.out / f"snap_{datetime.now():%Y%m%d-%H%M%S}_{i:03d}.jpg"
            name.write_bytes(data)
            print(f"[{i + 1}/{args.count}] {name.name}  {len(data)} bytes")
        if i + 1 < args.count:
            time.sleep(args.interval)


if __name__ == "__main__":
    main()
