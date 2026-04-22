#!/usr/bin/env python3
"""
presenter.py — expo presentation layer for the proto-os MICRO demo.

Reads the kernel serial stream on stdin (qemu's ``-serial stdio``) and
emits it on stdout. Later commits add coloring, framing, an event
timeline, counters, and a benchmark sidebar; this scaffold is an
unbuffered, signal-safe pass-through so we can land the pipeline
plumbing first and verify that wrapping qemu through a filter does not
lose output or break the ``timeout`` contract in ``run_demo.sh``.

Exit codes:
  0  stdin reached EOF (expected — qemu exited or was killed by timeout)
  0  SIGTERM / SIGINT received (clean shutdown)
"""

import signal
import sys


def _install_signal_handlers() -> None:
    def _handler(_signum, _frame):  # type: ignore[no-untyped-def]
        try:
            sys.stdout.flush()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, _handler)
    signal.signal(signal.SIGINT, _handler)


def main() -> int:
    _install_signal_handlers()

    # Binary read + explicit decode keeps us immune to Python's text-mode
    # buffering when stdin is a pipe. qemu's PL011 serial path flushes
    # per character, so readline() returns promptly as lines arrive.
    stdin_bin = sys.stdin.buffer
    stdout = sys.stdout
    while True:
        raw = stdin_bin.readline()
        if not raw:
            break
        stdout.write(raw.decode("utf-8", errors="replace"))
        stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
