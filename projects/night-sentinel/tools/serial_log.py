"""Record the board's serial log to a file (Day 4+).

Every line is printed and saved. Lines starting with "F," (per-frame detections), "S,"
(state changes) and "H," (heartbeats) are also written to their own CSV files next to the log,
ready for tools/replay and sweep.py.

  uv run serial_log.py                       # until Ctrl+C -> <repo>/logs/<time>.log
  uv run serial_log.py --seconds 60 --reset  # reset the board first, stop after 60 s
  uv run serial_log.py --name walk01         # -> logs/walk01.log, walk01_frames.csv, ...

Close idf.py monitor first: only one program can open the COM port.
"""

import argparse
import sys
import time
from datetime import datetime
from pathlib import Path

import serial  # pyserial

REPO_ROOT = Path(__file__).resolve().parent.parent

# Line prefix -> (file suffix, CSV header)
CSV_STREAMS = {
    "F,": ("frames", "F,t_ms,n,score,x1,y1,x2,y2,...more boxes"),
    "S,": ("states", "S,t_ms,from,to,event_id"),
    "H,": ("heartbeat", "H,t_ms,uptime_s,fps,infer_ms,free_int,min_int,free_psram,min_psram,rssi,events,state,ir,lux"),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = until Ctrl+C)")
    ap.add_argument("--reset", action="store_true", help="reset the board before recording")
    ap.add_argument("--name", default=None, help="file name stem (default: date-time)")
    ap.add_argument("--quiet", action="store_true", help="don't echo F, lines (they're ~7 per second)")
    args = ap.parse_args()

    out_dir = REPO_ROOT / "logs"
    out_dir.mkdir(exist_ok=True)
    stem = args.name or datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = out_dir / f"{stem}.log"

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.timeout = 0.5
    # Keep both control lines released while opening: on the ESP32-S3's USB serial they are
    # wired to EN (reset) and GPIO0 (boot mode).
    port.dtr = False
    port.rts = False
    port.open()
    if args.reset:
        # Same sequence as esptool's hard reset. On Windows (usbser.sys) a new RTS level is only
        # sent together with a DTR write, hence the dummy "dtr = dtr" after each change.
        port.rts = True   # EN low = hold in reset
        port.dtr = port.dtr
        time.sleep(0.2)
        port.rts = False  # release: normal boot (GPIO0 stays high)
        port.dtr = port.dtr
        port.reset_input_buffer()   # drop anything printed before the reset

    csv_files = {}
    start = time.time()
    with open(log_path, "w", encoding="utf-8", newline="") as log:
        try:
            buf = b""
            while not args.seconds or time.time() - start < args.seconds:
                buf += port.read(4096)
                *lines, buf = buf.split(b"\n")
                for raw in lines:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r")
                    log.write(line + "\n")
                    prefix = line[:2]
                    if prefix in CSV_STREAMS:
                        if prefix not in csv_files:
                            suffix, header = CSV_STREAMS[prefix]
                            f = open(out_dir / f"{stem}_{suffix}.csv", "w", encoding="utf-8", newline="")
                            f.write(header + "\n")
                            csv_files[prefix] = f
                        csv_files[prefix].write(line + "\n")
                        if prefix == "F," and args.quiet:
                            continue
                    print(line)
                log.flush()
        except KeyboardInterrupt:
            pass
        finally:
            for f in csv_files.values():
                f.close()
            port.close()
    print(f"\nsaved {log_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
