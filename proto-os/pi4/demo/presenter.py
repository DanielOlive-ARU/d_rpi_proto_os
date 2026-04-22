#!/usr/bin/env python3
"""
presenter.py — expo presentation layer for the proto-os MICRO demo.

Reads the kernel serial stream on stdin (qemu's ``-serial stdio``) and
renders it inside a framed full-screen "poster" chrome: a persistent
title bar and a bordered log window that scrolls the most recent lines.
Colorizes known kernel markers so crash and recovery events pop from
across a table. Later commits add narrative slides, cross-iteration
counters, a benchmark sidebar, and an event-timeline rail.

Environment:
  DEMO_PLAIN       if '1', force plain pass-through (no ANSI, no chrome).
  NO_COLOR         any non-empty value disables color (still frames).
  DEMO_NO_FRAME    if '1', disable framing (still colorizes).

Exit codes:
  0  stdin reached EOF (expected — qemu exited or was killed by timeout)
  0  SIGTERM / SIGINT received (clean shutdown)
"""

import collections
import os
import re
import signal
import sys
from typing import Deque, List, Tuple


# Force UTF-8 stdout so box-drawing and em-dash characters render on
# any host (Raspberry Pi OS Lite is UTF-8 by default, but we also
# develop and smoke-test on hosts whose default C locale may fall back
# to cp1252 / ascii).
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
except (AttributeError, OSError):
    pass


# ----- ANSI helpers ---------------------------------------------------------

_RESET = "\x1b[0m"
_INVERSE = "\x1b[7m"
_HIDE_CURSOR = "\x1b[?25l"
_SHOW_CURSOR = "\x1b[?25h"
_ALT_SCREEN_ON = "\x1b[?1049h"
_ALT_SCREEN_OFF = "\x1b[?1049l"
_CLEAR = "\x1b[2J"


def _goto(row: int, col: int) -> str:
    return f"\x1b[{row};{col}H"


# Line colouring rules. Evaluated in order; first match wins. Colours are
# chosen to be legible on a 1080p HDMI default terminal font from across
# a table — crash pops, tick noise recedes.
_COLOR_RULES: List[Tuple[re.Pattern, str]] = [
    (re.compile(r"^\[fault\]"), "\x1b[1;31m"),          # bold red
    (re.compile(r"^\[panic\]"), "\x1b[1;31m"),
    (re.compile(r"^\[sync\]"), "\x1b[1;31m"),
    (re.compile(r"^\[sup\] restarted"), "\x1b[1;96m"),  # bold bright cyan
    (re.compile(r"^\[uart\] ready"), "\x1b[1;32m"),     # bold green
    (re.compile(r"^\[sup\] ready"), "\x1b[1;32m"),
    (re.compile(r"^A$"), "\x1b[32m"),                    # green heartbeat
    (re.compile(r"^BOOT$"), "\x1b[33m"),                 # yellow
    (re.compile(r"^\[boot\]"), "\x1b[33m"),
    (re.compile(r"^\[mmu\]"), "\x1b[33m"),
    (re.compile(r"^\[svc\]"), "\x1b[33m"),
    (re.compile(r"^\[tick\]"), "\x1b[2m"),               # dim
    (re.compile(r"^\[thread\]"), "\x1b[2m"),
    (re.compile(r"^\[task\]"), "\x1b[2m"),
    (re.compile(r"^BENCH_META"), "\x1b[2m"),
    (re.compile(r"^EARLY:"), "\x1b[2m"),
]


# ----- chrome strings -------------------------------------------------------

_TITLE = "  proto-os — microkernel fault isolation demo  "
_FRAME_TITLE = " live kernel log "


# ----- environment flags ----------------------------------------------------


def _plain_mode() -> bool:
    return os.environ.get("DEMO_PLAIN", "").strip() == "1"


def _color_enabled() -> bool:
    if _plain_mode():
        return False
    if os.environ.get("NO_COLOR", "").strip() != "":
        return False
    return sys.stdout.isatty()


def _frame_enabled() -> bool:
    if _plain_mode():
        return False
    if os.environ.get("DEMO_NO_FRAME", "").strip() == "1":
        return False
    return sys.stdout.isatty()


def _detect_size() -> Tuple[int, int]:
    try:
        ts = os.get_terminal_size(sys.stdout.fileno())
        return ts.lines, ts.columns
    except (OSError, ValueError):
        return 24, 80


# ----- line coloring (used in no-frame mode) --------------------------------


def _colorize_free(line: str) -> str:
    stripped = line.rstrip("\r\n")
    trailing = line[len(stripped):]
    for pattern, prefix in _COLOR_RULES:
        if pattern.match(stripped):
            return f"{prefix}{stripped}{_RESET}{trailing}"
    return line


# ----- framed presenter -----------------------------------------------------


class Chrome:
    """Manages the full-screen framed layout and scrolling log window.

    Layout (1-indexed rows, as ANSI expects):
      row 1:         inverse-video title bar
      row 2:         blank separator
      row 3:         frame top — ┌── live kernel log ──────┐
      rows 4..R-1:   │ <log content ...>                   │
      row R:         frame bottom — └────────────────────┘
    Content width = cols - 4 (two border chars + two padding columns).
    """

    def __init__(self, color: bool) -> None:
        self.color = color
        self.stdout = sys.stdout
        self.rows, self.cols = _detect_size()
        self._recompute_layout()
        self.log_lines: Deque[str] = collections.deque(maxlen=self.content_height)
        self._resize_pending = False

    def _recompute_layout(self) -> None:
        self.content_height = max(1, self.rows - 4)
        self.content_width = max(1, self.cols - 4)

    def on_sigwinch(self, _signum=None, _frame=None) -> None:  # type: ignore[no-untyped-def]
        # Just flag; the main loop re-applies on next line boundary to
        # avoid redrawing mid-write.
        self._resize_pending = True

    def _apply_resize(self) -> None:
        self.rows, self.cols = _detect_size()
        self._recompute_layout()
        keep = list(self.log_lines)[-self.content_height:]
        self.log_lines = collections.deque(keep, maxlen=self.content_height)
        self._erase_and_draw_static()
        self._draw_content()

    def setup(self) -> None:
        self.stdout.write(_ALT_SCREEN_ON)
        self.stdout.write(_HIDE_CURSOR)
        self._erase_and_draw_static()
        self.stdout.flush()

    def teardown(self) -> None:
        self.stdout.write(_SHOW_CURSOR)
        self.stdout.write(_ALT_SCREEN_OFF)
        self.stdout.flush()

    def _erase_and_draw_static(self) -> None:
        out = self.stdout
        out.write(_CLEAR)

        # Title bar (rows 1-2)
        title = _TITLE[: self.cols] if len(_TITLE) > self.cols else _TITLE
        pad_left = max(0, (self.cols - len(title)) // 2)
        pad_right = max(0, self.cols - pad_left - len(title))
        out.write(_goto(1, 1) + _INVERSE)
        out.write(" " * pad_left + title + " " * pad_right)
        out.write(_RESET)
        out.write(_goto(2, 1) + " " * self.cols)

        # Frame top with embedded title
        out.write(_goto(3, 1))
        top_label = _FRAME_TITLE
        prefix = "┌── " + top_label + " "
        if len(prefix) + 1 > self.cols:
            out.write("┌" + "─" * max(0, self.cols - 2) + "┐")
        else:
            dashes = "─" * (self.cols - len(prefix) - 1)
            out.write(prefix + dashes + "┐")

        # Frame bottom
        out.write(_goto(self.rows, 1))
        out.write("└" + "─" * max(0, self.cols - 2) + "┘")

        # Side borders + empty content rows
        for i in range(self.content_height):
            row = 4 + i
            out.write(_goto(row, 1) + "│")
            out.write(_goto(row, 2) + " " * self.content_width)
            out.write(_goto(row, self.cols) + "│")

    def _fit_content(self, raw: str) -> str:
        stripped = raw.rstrip("\r\n")
        sanitized = "".join(c if 0x20 <= ord(c) < 0x7F else "?" for c in stripped)
        width = self.content_width
        if len(sanitized) > width:
            sanitized = sanitized[: max(0, width - 3)] + "..."
        pad = " " * (width - len(sanitized))
        if self.color:
            for pattern, prefix in _COLOR_RULES:
                if pattern.match(sanitized):
                    return f"{prefix}{sanitized}{_RESET}{pad}"
        return sanitized + pad

    def _draw_content(self) -> None:
        out = self.stdout
        lines = list(self.log_lines)
        for i in range(self.content_height):
            row = 4 + i
            raw = lines[i] if i < len(lines) else ""
            out.write(_goto(row, 3))
            out.write(self._fit_content(raw))
        out.flush()

    def on_line(self, raw: str) -> None:
        if self._resize_pending:
            self._resize_pending = False
            self._apply_resize()
        self.log_lines.append(raw.rstrip("\r\n"))
        self._draw_content()


# ----- signal handling ------------------------------------------------------


def _install_signal_handlers(chrome: "Chrome | None") -> None:
    def _term_handler(_signum, _frame):  # type: ignore[no-untyped-def]
        try:
            if chrome is not None:
                chrome.teardown()
            sys.stdout.flush()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, _term_handler)
    signal.signal(signal.SIGINT, _term_handler)
    if chrome is not None and hasattr(signal, "SIGWINCH"):
        signal.signal(signal.SIGWINCH, chrome.on_sigwinch)  # type: ignore[arg-type]


# ----- main -----------------------------------------------------------------


def main() -> int:
    color = _color_enabled()
    framed = _frame_enabled()

    chrome = Chrome(color=color) if framed else None
    _install_signal_handlers(chrome)

    stdin_bin = sys.stdin.buffer
    stdout = sys.stdout

    if chrome is not None:
        chrome.setup()

    try:
        while True:
            raw = stdin_bin.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace")
            if chrome is not None:
                chrome.on_line(line)
            else:
                stdout.write(_colorize_free(line) if color else line)
                stdout.flush()
    finally:
        if chrome is not None:
            chrome.teardown()

    return 0


if __name__ == "__main__":
    sys.exit(main())
