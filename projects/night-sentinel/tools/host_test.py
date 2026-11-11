"""Build and run the PC-side C programs in host_test/ (Day 6).

Compiles app/intrusion/intrusion_fsm.c unchanged together with
  host_test/test_fsm.c  -> build_host/test_fsm(.exe)  unit tests, run straight away
  host_test/replay.c    -> build_host/replay(.exe)    log replay, used by sweep.py

The C compiler comes from the `ziglang` Python package (zig cc), so nothing else needs
installing on Windows:
  uv run host_test.py            # build + run the tests
  uv run host_test.py --build    # build only
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC_FSM = REPO / "app" / "intrusion" / "intrusion_fsm.c"
INC = REPO / "app" / "intrusion" / "include"
OUT = REPO / "build_host"
EXE = ".exe" if sys.platform == "win32" else ""


def cc(sources, out):
    cmd = [sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O2", "-Wall", "-Wextra",
           "-Werror", "-I", str(INC), *map(str, sources), "-o", str(out)]
    subprocess.run(cmd, check=True)


def build():
    OUT.mkdir(exist_ok=True)
    cc([REPO / "host_test" / "test_fsm.c", SRC_FSM], OUT / f"test_fsm{EXE}")
    cc([REPO / "host_test" / "replay.c", SRC_FSM], OUT / f"replay{EXE}")
    return OUT / f"test_fsm{EXE}", OUT / f"replay{EXE}"


def main():
    test_exe, replay_exe = build()
    print(f"built {test_exe.name}, {replay_exe.name} in {OUT}")
    if "--build" in sys.argv:
        return 0
    return subprocess.run([str(test_exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
