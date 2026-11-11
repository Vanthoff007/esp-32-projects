"""Browse the board's SD card from the terminal, over Wi-Fi (the board's /sd web route).

  uv run sd.py ls                      # the card's root
  uv run sd.py ls ev                   # one folder
  uv run sd.py tree                    # every file on the card
  uv run sd.py pull events.csv         # download one file into the current folder
  uv run sd.py pull ev ..\\sd_copy     # download a whole folder (and its sub-folders)
  uv run sd.py push my_model.espdl models/     # upload into a folder (created if missing)
  uv run sd.py push-model pedestrian_pico      # upload models/<name>/weights/* to the card's models/
  uv run sd.py rm rec/20261111-221501_V.avi    # delete a file (or an empty folder)
  uv run sd.py rm -r "My music"        # delete a folder and everything in it
  uv run sd.py clean                   # list what is not the project's; add --yes to delete it
  uv run sd.py df                      # card size and free space

Needs: the board on the same Wi-Fi, the web server enabled (menuconfig -> Night Sentinel
build) and a FAT32 card in the slot. Standard library only, so `uv sync` isn't needed.
The board address defaults to night-sentinel.local; pass --host <IP> if that name is slow
or doesn't resolve (the IP is in the serial log: "wifi: got IP ...").
"""

import argparse
import http.client
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


class Card:
    def __init__(self, host: str):
        self.base = f"http://{host}"

    def _url(self, path: str) -> str:
        return f"{self.base}/sd?path={urllib.parse.quote(path.strip('/'), safe='/')}"

    def _open(self, req, timeout=60):
        # The board has few connections to give out: when a browser has its page open, it may
        # drop ours. So try a few times before giving up.
        for attempt in range(5):
            try:
                return urllib.request.urlopen(req, timeout=timeout)
            except urllib.error.HTTPError as e:
                sys.exit(f"board says {e.code}: {e.read().decode(errors='replace').strip()}")
            except (OSError, http.client.HTTPException) as e:
                last = e
                time.sleep(1.5)
        sys.exit(f"can't reach {self.base} ({last}). Is the board on, and on this Wi-Fi? Try --host <IP>.")

    def listing(self, path: str):
        """[(name, size)] for a folder; size is None for sub-folders. None if path is a file."""
        with self._open(self._url(path)) as r:
            if not r.headers.get("Content-Type", "").startswith("text/plain"):
                return None
            text = r.read().decode(errors="replace")
        out = []
        for line in text.splitlines():
            name, _, size = line.partition("\t")
            if not _:
                return None   # a plain-text file, not a listing
            out.append((name.rstrip("/"), None if name.endswith("/") else int(size)))
        return out

    def download(self, path: str, dest: Path) -> int:
        dest.parent.mkdir(parents=True, exist_ok=True)
        n = 0
        with self._open(self._url(path)) as r, open(dest, "wb") as f:
            while chunk := r.read(65536):
                f.write(chunk)
                n += len(chunk)
        return n

    def upload(self, local: Path, path: str) -> str:
        data = local.read_bytes()
        req = urllib.request.Request(self._url(path), data=data, method="POST",
                                     headers={"Content-Type": "application/octet-stream"})
        with self._open(req, timeout=300) as r:
            return r.read().decode().strip()

    def delete(self, path: str) -> str:
        with self._open(urllib.request.Request(self._url(path), method="DELETE")) as r:
            return r.read().decode().strip()

    def status(self) -> dict:
        with self._open(f"{self.base}/status", timeout=15) as r:
            return json.loads(r.read())


def human(n: int) -> str:
    return f"{n} B" if n < 1024 else f"{n / 1024:.1f} KB" if n < 1024 * 1024 else f"{n / 1024 / 1024:.1f} MB"


def cmd_ls(card: Card, args):
    items = card.listing(args.path)
    if items is None:
        sys.exit(f"{args.path} is a file (use: pull {args.path})")
    for name, size in sorted(items):
        print(f"{'<dir>' if size is None else human(size):>10}  {name}{'/' if size is None else ''}")
    print(f"{len(items)} item(s) in /{args.path.strip('/')}")


def walk(card: Card, path: str):
    """Yield (remote_path, size) for every file under path."""
    for name, size in sorted(card.listing(path) or []):
        full = f"{path.strip('/')}/{name}".strip("/")
        if size is None:
            yield from walk(card, full)
        else:
            yield full, size


def cmd_tree(card: Card, args):
    total = 0
    for full, size in walk(card, args.path):
        print(f"{human(size):>10}  {full}")
        total += size
    print(f"total {human(total)}")


def cmd_pull(card: Card, args):
    remote = args.remote.strip("/")
    dest = Path(args.dest)
    items = card.listing(remote) if remote else card.listing("")
    if items is None:   # a single file
        target = dest / Path(remote).name if dest.is_dir() or args.dest in (".", "") else dest
        print(f"{remote} -> {target} ({human(card.download(remote, target))})")
        return
    files = list(walk(card, remote))
    for i, (full, size) in enumerate(files, 1):
        rel = full[len(remote):].lstrip("/") if remote else full
        target = dest / (Path(remote).name if remote else "sdcard") / rel
        if target.exists() and target.stat().st_size == size and not args.force:
            print(f"[{i}/{len(files)}] {full}: already here, skipped")
            continue
        card.download(full, target)
        print(f"[{i}/{len(files)}] {full} -> {target} ({human(size)})")


def cmd_push(card: Card, args):
    local = Path(args.local)
    if not local.is_file():
        sys.exit(f"{local} is not a file")
    remote = args.remote
    if remote.endswith("/") or remote == "":
        remote += local.name
    print(card.upload(local, remote))


def cmd_push_model(card: Card, args):
    weights = REPO / "models" / args.name / "weights"
    files = [f for f in sorted(weights.glob("*")) if f.is_file()] if weights.is_dir() else []
    if not files:
        sys.exit(f"no files in {weights}")
    for f in files:
        print(card.upload(f, f"models/{f.name}"))
    print("The board loads the file within 10 s if inference is switched on (or press Start on the page).")


def rm_tree(card: Card, path: str) -> int:
    """Delete a folder and everything in it. Returns the number of files deleted."""
    n = 0
    for name, size in card.listing(path) or []:
        full = f"{path.strip('/')}/{name}"
        if size is None:
            n += rm_tree(card, full)
        else:
            card.delete(full)
            n += 1
    card.delete(path)   # the folder itself, now empty
    return n


def cmd_rm(card: Card, args):
    if args.recursive and card.listing(args.path) is not None:
        print(f"deleted {args.path}/ and the {rm_tree(card, args.path)} file(s) in it")
    else:
        print(card.delete(args.path))


# What the firmware itself keeps on the card. `clean` deletes everything else.
PROJECT_ITEMS = {"models", "ev", "rec", "events.csv", "heartbeat.csv", "boot.csv"}


def cmd_clean(card: Card, args):
    items = [(n, s) for n, s in sorted(card.listing("") or []) if n not in PROJECT_ITEMS]
    if not items:
        print("nothing to delete: only the project's own files are on the card")
        return
    print(f"{len(items)} item(s) in the card's root do not belong to the project:")
    for name, size in items:
        print(f"  {name}{'/' if size is None else ''}")
    if not args.yes:
        print("Nothing deleted. Run again with --yes to delete them for good (this can't be undone).")
        return
    for i, (name, size) in enumerate(items, 1):
        try:
            if size is None:
                print(f"[{i}/{len(items)}] {name}/: {rm_tree(card, name)} file(s) deleted")
            else:
                card.delete(name)
                print(f"[{i}/{len(items)}] {name}: deleted")
        except SystemExit as e:   # one stubborn file must not stop the rest
            print(f"[{i}/{len(items)}] {name}: NOT deleted ({e})")


def cmd_df(card: Card, args):
    s = card.status()
    if not s.get("sd"):
        sys.exit("no SD card mounted")
    total, free = s["sd_total_mb"], s["sd_free_mb"]
    print(f"{free} MB free of {total} MB ({100 * (total - free) // max(total, 1)} % used)")


def main():
    ap = argparse.ArgumentParser(description="Browse the board's SD card over Wi-Fi.")
    ap.add_argument("--host", default="night-sentinel.local", help="board address (default: night-sentinel.local)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("ls", help="list a folder"); p.add_argument("path", nargs="?", default=""); p.set_defaults(fn=cmd_ls)
    p = sub.add_parser("tree", help="list every file under a folder"); p.add_argument("path", nargs="?", default=""); p.set_defaults(fn=cmd_tree)
    p = sub.add_parser("pull", help="download a file or a whole folder")
    p.add_argument("remote"); p.add_argument("dest", nargs="?", default="."); p.add_argument("--force", action="store_true", help="download again even if the file is already here")
    p.set_defaults(fn=cmd_pull)
    p = sub.add_parser("push", help="upload a file"); p.add_argument("local"); p.add_argument("remote", help="folder ending in / or a full file path on the card"); p.set_defaults(fn=cmd_push)
    p = sub.add_parser("push-model", help="upload models/<name>/weights/* to the card's models/ folder"); p.add_argument("name"); p.set_defaults(fn=cmd_push_model)
    p = sub.add_parser("rm", help="delete a file or an empty folder (-r: a folder and everything in it)")
    p.add_argument("path"); p.add_argument("-r", "--recursive", action="store_true"); p.set_defaults(fn=cmd_rm)
    p = sub.add_parser("clean", help="delete everything that is not the project's (keeps models/, ev/, rec/ and the CSV logs)")
    p.add_argument("--yes", action="store_true", help="really delete; without it the items are only listed"); p.set_defaults(fn=cmd_clean)
    p = sub.add_parser("df", help="card size and free space"); p.set_defaults(fn=cmd_df)
    args = ap.parse_args()
    args.fn(Card(args.host), args)


if __name__ == "__main__":
    main()
