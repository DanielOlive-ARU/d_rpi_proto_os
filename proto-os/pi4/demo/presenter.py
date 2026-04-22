#!/usr/bin/env python3
"""
presenter.py — expo presentation layer for the proto-os MICRO demo.

Three subcommands share chrome, color rules, and signal handling:

  presenter.py live        (default) — render qemu's -serial stdio as a
                            framed, colorized full-screen kiosk view.
  presenter.py preroll     — hold an architecture-preview slide with
                            a countdown before the next qemu launch.
  presenter.py postroll    — hold a "what you just watched" recap slide
                            with a countdown before looping.

Environment:
  DEMO_PLAIN       if '1', force plain pass-through (no ANSI, no chrome).
                   slide modes become no-ops when plain.
  NO_COLOR         any non-empty value disables color.
  DEMO_NO_FRAME    if '1', disable framing in live mode (still colorizes).
  DEMO_PREROLL     integer seconds for preroll slide (default 3).
  DEMO_POSTROLL    integer seconds for postroll slide (default 5).

Exit codes:
  0  stdin EOF (live), slide hold elapsed (preroll/postroll),
     SIGTERM / SIGINT clean shutdown.
  2  unknown subcommand.
"""

import collections
import os
import re
import signal
import sys
import time
from typing import Deque, List, Optional, Tuple


# Force UTF-8 stdout so box-drawing, em-dash, and arrow characters render
# on any host. Raspberry Pi OS Lite is UTF-8 by default, but dev hosts
# with a cp1252 or ascii default codec would otherwise crash on output.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # type: ignore[attr-defined]
except (AttributeError, OSError):
    pass


# ----- ANSI helpers ---------------------------------------------------------

_RESET = "\x1b[0m"
_INVERSE = "\x1b[7m"
_DIM = "\x1b[2m"
_HIDE_CURSOR = "\x1b[?25l"
_SHOW_CURSOR = "\x1b[?25h"
_ALT_SCREEN_ON = "\x1b[?1049h"
_ALT_SCREEN_OFF = "\x1b[?1049l"
_CLEAR = "\x1b[2J"

_FG_GREEN = "\x1b[32m"
_FG_RED = "\x1b[31m"


def _goto(row: int, col: int) -> str:
    return f"\x1b[{row};{col}H"


# ----- line colouring rules (used by live mode) -----------------------------

# Evaluated in order; first match wins. Colours are chosen to be legible
# on a 1080p HDMI default font from across a table — crash pops, tick
# noise recedes.
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
_FRAME_TITLE_LIVE = " live kernel log "


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


def _env_int(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    try:
        v = int(raw)
        return v if v >= 0 else default
    except ValueError:
        return default


def _detect_size() -> Tuple[int, int]:
    try:
        ts = os.get_terminal_size(sys.stdout.fileno())
        return ts.lines, ts.columns
    except (OSError, ValueError):
        return 24, 80


# ----- shared chrome drawing ------------------------------------------------


def _draw_title_bar(out, cols: int) -> None:
    title = _TITLE[:cols] if len(_TITLE) > cols else _TITLE
    pad_left = max(0, (cols - len(title)) // 2)
    pad_right = max(0, cols - pad_left - len(title))
    out.write(_goto(1, 1) + _INVERSE)
    out.write(" " * pad_left + title + " " * pad_right)
    out.write(_RESET)
    out.write(_goto(2, 1) + " " * cols)


def _draw_outer_frame(out, rows: int, cols: int, embedded_title: str = "") -> None:
    """Top border (row 3), bottom border (row `rows`), side borders on
    all content rows. If `embedded_title` is non-empty it is rendered
    inside the top border like ``┌── title ──────┐``."""
    # Top border
    out.write(_goto(3, 1))
    if embedded_title:
        prefix = "┌── " + embedded_title + " "
        if len(prefix) + 1 > cols:
            out.write("┌" + "─" * max(0, cols - 2) + "┐")
        else:
            out.write(prefix + "─" * (cols - len(prefix) - 1) + "┐")
    else:
        out.write("┌" + "─" * max(0, cols - 2) + "┐")

    # Bottom border
    out.write(_goto(rows, 1))
    out.write("└" + "─" * max(0, cols - 2) + "┘")

    # Side borders + blank content rows
    content_height = max(0, rows - 4)
    content_width = max(0, cols - 4)
    for i in range(content_height):
        row = 4 + i
        out.write(_goto(row, 1) + "│")
        if content_width > 0:
            out.write(_goto(row, 2) + " " * (cols - 2))
        out.write(_goto(row, cols) + "│")


# ----- free-form colorization (used in no-frame live mode) ------------------


def _colorize_free(line: str) -> str:
    stripped = line.rstrip("\r\n")
    trailing = line[len(stripped):]
    for pattern, prefix in _COLOR_RULES:
        if pattern.match(stripped):
            return f"{prefix}{stripped}{_RESET}{trailing}"
    return line


# ----- live mode chrome -----------------------------------------------------


class Chrome:
    """Full-screen framed layout for the live qemu log.

    Layout (1-indexed, as ANSI expects):
      row 1:         inverse-video title bar
      row 2:         blank separator
      row 3:         frame top with embedded "live kernel log" title
      rows 4..R-1:   framed log content
      row R:         frame bottom
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
        # Flag only; main loop re-applies at next line boundary.
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
        _draw_title_bar(out, self.cols)
        _draw_outer_frame(out, self.rows, self.cols, embedded_title=_FRAME_TITLE_LIVE)

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


# ----- slide content --------------------------------------------------------

# Slide bodies are pre-formatted ASCII/Unicode lines. They are rendered
# left-padded to horizontally centre inside the content area based on
# the longest line in the block. Vertical centering is applied by the
# render helper.

_PREROLL_BODY: List[str] = [
    "    ┌──────────┐       IPC         ┌──────────────────┐",
    "    │  task_a  │  ──────────────▶  │   uart_server    │",
    "    │  writer  │                   │     (task_b)     │",
    "    └──────────┘                   └──────────────────┘",
    "                                            ↑",
    "                                            │  supervises",
    "                                            │",
    "                                     ┌────────────┐",
    "                                     │ supervisor │",
    "                                     │  (task_c)  │",
    "                                     └────────────┘",
    "",
    "  In the next run:",
    "    • task_b (uart_server) will crash on purpose",
    "    • task_c (supervisor) catches the fault",
    "    • task_b is restarted — kernel keeps running",
    "    • task_a continues, uninterrupted",
]

_POSTROLL_BODY: List[str] = [
    "",
    "  A user-space service just crashed — and the system kept running.",
    "",
    "    ✔ BOOT                       kernel started",
    "    ✔ [uart] ready               UART service up (user space)",
    "    ✔ [sup] ready                supervisor up (user space)",
    "    ✖ [fault] task_b dead        uart_server faulted (on purpose)",
    "    ✔ [sup] restarted uart       supervisor restarted the service",
    "    ✔ [uart] ready (again)       service resumed — no kernel reboot",
    "    ✔ A (continues)              writer task never stopped",
    "",
    "",
    "  The microkernel contained the fault in user space.",
    "  A monolithic equivalent would panic or reboot on the same crash.",
]


def _colorize_slide_line(line: str, color: bool) -> str:
    """Apply minimal color to ✔/✖ glyphs on the recap slide."""
    if not color:
        return line
    out = line
    if "\u2714" in out:
        out = out.replace("\u2714", f"{_FG_GREEN}\u2714{_RESET}")
    if "\u2716" in out:
        out = out.replace("\u2716", f"{_FG_RED}\u2716{_RESET}")
    return out


def _visible_len(s: str) -> int:
    """Strip ANSI SGR sequences and return visible character count.
    Kernel output uses ASCII + Unicode BMP characters that are all
    single-cell on the terminals we target, so character count ≈ cell
    count."""
    return len(re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", s))


def _render_slide(body: List[str], footer_template: str, duration: int, color: bool) -> int:
    """Draw the title bar + outer frame, then render `body` centered
    inside the content area, with a live countdown footer. Holds for
    `duration` seconds. Restores cursor and clears the screen on exit."""
    rows, cols = _detect_size()
    out = sys.stdout

    # Pre-compute layout
    content_top = 4
    content_bottom = rows - 1
    content_height = max(1, content_bottom - content_top + 1)
    content_width = max(1, cols - 4)

    # Vertical centering: leave footer row at the bottom (content_bottom)
    # for the countdown; body uses the rows above.
    body_area = max(1, content_height - 2)
    visible_body = body[:body_area]
    top_pad = max(0, (body_area - len(visible_body)) // 2)

    out.write(_HIDE_CURSOR)
    out.write(_CLEAR)
    _draw_title_bar(out, cols)
    _draw_outer_frame(out, rows, cols)

    # Block-level horizontal centering — use the widest body line so that
    # diagrams preserve their internal relative positioning (per-line
    # centering would misalign arrows vs. boxes).
    max_body_width = max((_visible_len(l) for l in visible_body), default=0)
    block_left_pad = max(0, (content_width - max_body_width) // 2)

    for i, line in enumerate(visible_body):
        row = content_top + top_pad + i
        if row >= content_bottom:
            break
        visible = _visible_len(line)
        if visible > content_width:
            line = line[:content_width]
        colored = _colorize_slide_line(line, color)
        out.write(_goto(row, 3 + block_left_pad) + colored)

    # Footer countdown
    footer_row = content_bottom
    out.flush()
    try:
        for remaining in range(max(1, duration), 0, -1):
            text = footer_template.format(n=remaining, s="" if remaining == 1 else "s")
            visible = _visible_len(text)
            pad_left = max(0, (content_width - visible) // 2)
            # Clear the footer row inside the frame, then write.
            out.write(_goto(footer_row, 3) + " " * content_width)
            out.write(_goto(footer_row, 3 + pad_left) + _DIM + text + _RESET)
            out.flush()
            time.sleep(1)
    finally:
        out.write(_goto(rows, 1) + _SHOW_CURSOR + _RESET)
        out.write(_CLEAR + _goto(1, 1))
        out.flush()
    return 0


# ----- signal handling ------------------------------------------------------


def _install_signal_handlers(chrome: Optional[Chrome]) -> None:
    def _term_handler(_signum, _frame):  # type: ignore[no-untyped-def]
        try:
            if chrome is not None:
                chrome.teardown()
            else:
                sys.stdout.write(_SHOW_CURSOR + _RESET)
                sys.stdout.flush()
        except Exception:
            pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, _term_handler)
    signal.signal(signal.SIGINT, _term_handler)
    if chrome is not None and hasattr(signal, "SIGWINCH"):
        signal.signal(signal.SIGWINCH, chrome.on_sigwinch)  # type: ignore[arg-type]


# ----- subcommands ----------------------------------------------------------


def cmd_live() -> int:
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


def cmd_preroll() -> int:
    if _plain_mode() or not sys.stdout.isatty():
        return 0
    _install_signal_handlers(None)
    duration = _env_int("DEMO_PREROLL", 3)
    return _render_slide(
        body=_PREROLL_BODY,
        footer_template="starting in {n} second{s}...",
        duration=duration,
        color=_color_enabled(),
    )


def cmd_postroll() -> int:
    if _plain_mode() or not sys.stdout.isatty():
        return 0
    _install_signal_handlers(None)
    duration = _env_int("DEMO_POSTROLL", 5)
    return _render_slide(
        body=_POSTROLL_BODY,
        footer_template="next run in {n} second{s}...",
        duration=duration,
        color=_color_enabled(),
    )


# ----- entry point ----------------------------------------------------------


def main() -> int:
    mode = sys.argv[1] if len(sys.argv) > 1 else "live"
    if mode == "live":
        return cmd_live()
    if mode == "preroll":
        return cmd_preroll()
    if mode == "postroll":
        return cmd_postroll()
    print(f"presenter.py: unknown subcommand '{mode}'", file=sys.stderr)
    print("usage: presenter.py [live|preroll|postroll]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
