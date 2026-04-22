#!/usr/bin/env python3
"""
presenter.py — expo presentation layer for the proto-os MICRO demo.

Reads the kernel serial stream on stdin (qemu's ``-serial stdio``) and
emits a colorized rendering on stdout. Later commits add framing, an
event timeline, counters, and a benchmark sidebar on top of this base.

Environment:
  DEMO_PLAIN   if set to '1', force plain pass-through (disables color).
  NO_COLOR     standard no-color flag; any non-empty value disables color.

Exit codes:
  0  stdin reached EOF (expected — qemu exited or was killed by timeout)
  0  SIGTERM / SIGINT received (clean shutdown)
"""

import os
import re
import signal
import sys
from typing import List, Tuple


_RESET = "\x1b[0m"

# (regex, ANSI prefix). Rules evaluated in order; first match wins.
# Colors are chosen to be readable on a 1080p HDMI default terminal font
# from across a table — the crash line needs to pop, the tick noise
# needs to recede.
_COLOR_RULES: List[Tuple[re.Pattern, str]] = [
    (re.compile(r"^\[fault\]"), "\x1b[1;31m"),        # bold red
    (re.compile(r"^\[panic\]"), "\x1b[1;31m"),
    (re.compile(r"^\[sync\]"), "\x1b[1;31m"),
    (re.compile(r"^\[sup\] restarted"), "\x1b[1;96m"),  # bold bright cyan
    (re.compile(r"^\[uart\] ready"), "\x1b[1;32m"),   # bold green
    (re.compile(r"^\[sup\] ready"), "\x1b[1;32m"),
    (re.compile(r"^A$"), "\x1b[32m"),                  # green (task_a heartbeat)
    (re.compile(r"^BOOT$"), "\x1b[33m"),               # yellow
    (re.compile(r"^\[boot\]"), "\x1b[33m"),
    (re.compile(r"^\[mmu\]"), "\x1b[33m"),
    (re.compile(r"^\[svc\]"), "\x1b[33m"),
    (re.compile(r"^\[tick\]"), "\x1b[2m"),             # dim
    (re.compile(r"^\[thread\]"), "\x1b[2m"),
    (re.compile(r"^\[task\]"), "\x1b[2m"),
    (re.compile(r"^BENCH_META"), "\x1b[2m"),
    (re.compile(r"^EARLY:"), "\x1b[2m"),
]


def _color_enabled() -> bool:
    if os.environ.get("DEMO_PLAIN", "").strip() == "1":
        return False
    if os.environ.get("NO_COLOR", "").strip() != "":
        return False
    return sys.stdout.isatty()


def _colorize(line: str) -> str:
    stripped = line.rstrip("\r\n")
    trailing = line[len(stripped):]
    for pattern, prefix in _COLOR_RULES:
        if pattern.match(stripped):
            return f"{prefix}{stripped}{_RESET}{trailing}"
    return line


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
    color = _color_enabled()

    # Binary read + explicit decode keeps us immune to Python's text-mode
    # buffering when stdin is a pipe. qemu's PL011 serial path flushes
    # per character, so readline() returns promptly as lines arrive.
    stdin_bin = sys.stdin.buffer
    stdout = sys.stdout
    while True:
        raw = stdin_bin.readline()
        if not raw:
            break
        line = raw.decode("utf-8", errors="replace")
        stdout.write(_colorize(line) if color else line)
        stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
