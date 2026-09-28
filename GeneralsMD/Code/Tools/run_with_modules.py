#!/usr/bin/env python3
"""Run a Python tool's check only where the modules it imports are installed.

    python run_with_modules.py numpy,PIL Tools/order_cameos.py selfcheck

A check whose script needs third-party modules (numpy, Pillow) would fail with an ImportError on a
machine that lacks them, which says nothing about the code under test.  This looks for each module
first.  If any is missing it says which, on stdout so ctest's log keeps it, and exits 77, which the
test registers as its skip code: the run shows the test as skipped, by name, with the reason.  If all
are there it runs the script as __main__ with the remaining arguments, exactly as `python script ...`
would.
"""

import importlib.util
import runpy
import sys

SKIP = 77


def main():
    if len(sys.argv) < 3:
        print("usage: run_with_modules.py <module,module,...> <script> [arguments...]", file=sys.stderr)
        return 2
    modules = [name for name in sys.argv[1].split(",") if name]
    missing = [name for name in modules if importlib.util.find_spec(name) is None]
    script = sys.argv[2]
    if missing:
        print("skip: %s needs %s, which %s lacks" % (script, " and ".join(missing), sys.executable))
        return SKIP
    sys.argv = sys.argv[2:]
    runpy.run_path(script, run_name="__main__")
    return 0


if __name__ == "__main__":
    sys.exit(main())
