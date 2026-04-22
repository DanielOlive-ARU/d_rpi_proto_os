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
  DEMO_PLAIN            if '1', force plain pass-through (no ANSI, no chrome).
                        slide modes become no-ops when plain.
  NO_COLOR              any non-empty value disables color.
  DEMO_NO_FRAME         if '1', disable framing in live mode (still colorizes).
  DEMO_PREROLL          integer seconds for preroll slide (default 3).
  DEMO_POSTROLL         integer seconds for postroll slide (default 5).
  DEMO_STATE_FILE       path to the cross-iteration counter file
                        (default ``<presenter_dir>/.state/counters.json``).
  DEMO_RESET_COUNTERS   if '1', ignore any existing state and start counting
                        from zero for this invocation.
  DEMO_BENCH_FILE       path to the frozen benchmark summary file
                        (default ``<presenter_dir>/assets/bench_baseline.txt``).

Exit codes:
  0  stdin EOF (live), slide hold elapsed (preroll/postroll),
     SIGTERM / SIGINT clean shutdown.
  2  unknown subcommand.
"""

import collections
import datetime
import json
import os
import re
import signal
import sys
import time
from pathlib import Path
from typing import Any, Deque, Dict, List, Optional, Tuple


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


# ----- cross-iteration counter state ----------------------------------------

# All three subcommands read this state file; live mode is the only
# writer. The schema is intentionally minimal — kept JSON so it's
# inspectable by hand during the expo.
#
#   schema: int (always 1)
#   started_at_iso: ISO 8601 UTC — when counting began
#   iterations: int — completed live-mode runs (qemu actually emitted output)
#   faults_seen: int — ``[fault]`` kernel lines observed across all iterations
#   restarts_seen: int — ``[sup] restarted`` lines observed across all iterations
#   last_iteration_iso: ISO 8601 UTC or null — timestamp of most recent EOF

_STATE_SCHEMA = 1


def _iso_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _default_state() -> Dict[str, Any]:
    return {
        "schema": _STATE_SCHEMA,
        "started_at_iso": _iso_now(),
        "iterations": 0,
        "faults_seen": 0,
        "restarts_seen": 0,
        "last_iteration_iso": None,
    }


def _state_file() -> Path:
    override = os.environ.get("DEMO_STATE_FILE", "").strip()
    if override:
        return Path(override)
    return Path(__file__).resolve().parent / ".state" / "counters.json"


def _load_state() -> Dict[str, Any]:
    if os.environ.get("DEMO_RESET_COUNTERS", "").strip() == "1":
        return _default_state()
    path = _state_file()
    try:
        raw = path.read_text(encoding="utf-8")
        parsed = json.loads(raw)
        if not isinstance(parsed, dict):
            raise ValueError("state file is not a JSON object")
        merged = _default_state()
        for k in ("iterations", "faults_seen", "restarts_seen"):
            if k in parsed:
                merged[k] = int(parsed[k])
        for k in ("started_at_iso", "last_iteration_iso"):
            if k in parsed and parsed[k]:
                merged[k] = str(parsed[k])
        merged["schema"] = _STATE_SCHEMA
        return merged
    except (FileNotFoundError, OSError, ValueError, json.JSONDecodeError):
        return _default_state()


def _save_state(state: Dict[str, Any]) -> None:
    path = _state_file()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(state, indent=2), encoding="utf-8")
        os.replace(tmp, path)
    except OSError:
        # Non-fatal: the demo still runs, stats just don't persist
        # across invocations. Silent to avoid cluttering the display.
        pass


def _format_counters(state: Optional[Dict[str, Any]]) -> str:
    if state is None:
        return ""
    it = int(state.get("iterations", 0))
    rs = int(state.get("restarts_seen", 0))
    if it <= 0 and rs <= 0:
        return ""
    return f"iter {it} \u00b7 restarts {rs}"


def _format_counters_long(state: Optional[Dict[str, Any]]) -> str:
    if state is None:
        return ""
    it = int(state.get("iterations", 0))
    fs = int(state.get("faults_seen", 0))
    rs = int(state.get("restarts_seen", 0))
    return (
        f"Session totals: {it} iteration{'' if it == 1 else 's'} \u00b7 "
        f"{fs} fault{'' if fs == 1 else 's'} caught \u00b7 "
        f"{rs} restart{'' if rs == 1 else 's'} successful"
    )


# ----- frozen benchmark baseline --------------------------------------------

# The presenter does not measure at runtime (BENCH_MODE=OFF kernel).
# Instead it reads a small key=value snapshot extracted from the frozen
# benchmark session at commit d154dcb so reviewers see quantitative
# evidence alongside the qualitative demo. See
# ``assets/bench_baseline.txt`` for the source and caveats.


def _bench_file() -> Path:
    override = os.environ.get("DEMO_BENCH_FILE", "").strip()
    if override:
        return Path(override)
    return Path(__file__).resolve().parent / "assets" / "bench_baseline.txt"


def _load_bench() -> Optional[Dict[str, str]]:
    path = _bench_file()
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, OSError):
        return None
    parsed: Dict[str, str] = {}
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        parsed[key.strip()] = value.strip()
    return parsed or None


def _format_bench_summary(bench: Optional[Dict[str, str]]) -> str:
    if not bench:
        return ""
    mono = bench.get("mono_sys_write_cycles")
    micro = bench.get("micro_sys_write_cycles")
    recov = bench.get("recovery_window_cycles")
    if not (mono and micro and recov):
        return ""
    try:
        recov_k = int(recov) // 1000
    except ValueError:
        return ""
    return (
        f"sys_write: MONO {mono} \u00b7 MICRO {micro} cyc \u00b7 "
        f"recovery ~{recov_k}k cyc"
    )


def _format_bench_long(bench: Optional[Dict[str, str]]) -> str:
    """Single-line form suitable for the postroll tail. Sized to fit the
    76-char content width of a 24x80 terminal; wider terminals pad the
    slack with frame whitespace."""
    if not bench:
        return ""
    mono = bench.get("mono_sys_write_cycles")
    micro = bench.get("micro_sys_write_cycles")
    recov = bench.get("recovery_window_cycles")
    commit = bench.get("source_commit", "?")
    if not (mono and micro and recov):
        return ""
    try:
        recov_k = int(recov) // 1000
    except ValueError:
        return ""
    return (
        f"Frozen baseline ({commit}): MONO {mono} cyc \u00b7 "
        f"MICRO {micro} cyc \u00b7 recovery ~{recov_k}k cyc"
    )


# ----- shared chrome drawing ------------------------------------------------


def _draw_title_bar(
    out,
    cols: int,
    state: Optional[Dict[str, Any]] = None,
    bench: Optional[Dict[str, str]] = None,
) -> None:
    # Row 1 — centered inverse-video title
    title = _TITLE[:cols] if len(_TITLE) > cols else _TITLE
    pad_left = max(0, (cols - len(title)) // 2)
    pad_right = max(0, cols - pad_left - len(title))
    out.write(_goto(1, 1) + _INVERSE)
    out.write(" " * pad_left + title + " " * pad_right)
    out.write(_RESET)

    # Row 2 — compact dim stats row. Frozen baseline on the left,
    # session counters on the right. Either can be dropped if the row
    # is too narrow to fit both cleanly.
    out.write(_goto(2, 1) + " " * cols)
    bench_text = _format_bench_summary(bench)
    counters_text = _format_counters(state)

    min_gap = 4
    can_fit_both = (
        bench_text
        and counters_text
        and (len(bench_text) + len(counters_text) + min_gap + 4) <= cols
    )
    if can_fit_both:
        out.write(_goto(2, 3) + _DIM + bench_text + _RESET)
        right_start = cols - len(counters_text) - 1
        out.write(_goto(2, right_start) + _DIM + counters_text + _RESET)
    elif counters_text and (len(counters_text) + 4) <= cols:
        right_start = cols - len(counters_text) - 1
        out.write(_goto(2, right_start) + _DIM + counters_text + _RESET)
    elif bench_text and (len(bench_text) + 4) <= cols:
        out.write(_goto(2, 3) + _DIM + bench_text + _RESET)


def _draw_outer_frame(
    out,
    rows: int,
    cols: int,
    embedded_title: str = "",
    top_row: int = 3,
) -> None:
    """Top border (``top_row``), bottom border (row `rows`), side
    borders on all rows between them. If `embedded_title` is non-empty
    it is rendered inside the top border like ``┌── title ──────┐``.

    ``top_row`` is parameterised so the live-mode chrome can reserve
    row 3 for its event-timeline rail and push the frame down by one."""
    # Top border
    out.write(_goto(top_row, 1))
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

    # Side borders + blank content rows between top_row and rows (exclusive)
    for row in range(top_row + 1, rows):
        out.write(_goto(row, 1) + "│")
        if cols > 2:
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


_FAULT_LINE = re.compile(r"^\[fault\]")
_RESTART_LINE = re.compile(r"^\[sup\] restarted")


# ----- event timeline rail (live mode only) ---------------------------------

# Six-step rail that mirrors the M10 recovery arc. Each step is a
# predicate over a log line; when first satisfied the dot fills and is
# painted in its "lit" color for the remainder of the iteration.

_TIMELINE_BOOT = re.compile(r"^(?:BOOT$|\[boot\])")
_TIMELINE_UART_READY = re.compile(r"^\[uart\] ready")
_TIMELINE_SUP_READY = re.compile(r"^\[sup\] ready")

# (key, label, lit-color). The ok step reuses the [uart] ready pattern
# but is only eligible after the fault has been seen — handled in the
# update function below rather than as a standalone regex.
_TIMELINE_STEPS: List[Tuple[str, str, str]] = [
    ("boot",    "boot",    "\x1b[32m"),       # green
    ("uart",    "uart",    "\x1b[32m"),
    ("sup",     "sup",     "\x1b[32m"),
    ("fault",   "FAULT",   "\x1b[1;31m"),     # bold red
    ("restart", "restart", "\x1b[1;96m"),     # bold bright cyan
    ("ok",      "OK",      "\x1b[1;32m"),     # bold green — service resumed
]


def _initial_timeline_hits() -> Dict[str, bool]:
    return {key: False for key, _, _ in _TIMELINE_STEPS}


def _update_timeline_hits(hits: Dict[str, bool], line: str) -> bool:
    """Return True if ``line`` advanced any step."""
    changed = False
    if not hits["boot"] and _TIMELINE_BOOT.match(line):
        hits["boot"] = True; changed = True
    if not hits["uart"] and _TIMELINE_UART_READY.match(line):
        hits["uart"] = True; changed = True
    if not hits["sup"] and _TIMELINE_SUP_READY.match(line):
        hits["sup"] = True; changed = True
    if not hits["fault"] and _FAULT_LINE.match(line):
        hits["fault"] = True; changed = True
    if not hits["restart"] and _RESTART_LINE.match(line):
        hits["restart"] = True; changed = True
    # "ok" = any [uart] ready observed AFTER the fault has been hit,
    # i.e. the service came back online.
    if hits["fault"] and not hits["ok"] and _TIMELINE_UART_READY.match(line):
        hits["ok"] = True; changed = True
    return changed


def _render_timeline_rail(out, row: int, cols: int, hits: Dict[str, bool]) -> None:
    """Draw the six-step rail centered on ``row``. Unlit steps are dim
    empty circles; lit steps are filled circles in their step's color."""
    pieces: List[Tuple[str, int]] = []  # (styled_text, visible_width)
    for key, label, color in _TIMELINE_STEPS:
        if hits.get(key, False):
            glyph = "\u25CF"  # filled circle
            styled = f"{color}{glyph} {label}{_RESET}"
        else:
            glyph = "\u25CB"  # empty circle
            styled = f"{_DIM}{glyph} {label}{_RESET}"
        visible = 1 + 1 + len(label)  # glyph + space + label
        pieces.append((styled, visible))

    separator = " \u2500\u2500 "  # `─` dashes as rail between stations
    sep_len = len(separator)
    total_visible = sum(v for _, v in pieces) + sep_len * (len(pieces) - 1)

    out.write(_goto(row, 1) + " " * cols)  # clear the rail line
    if total_visible >= cols:
        # Degenerate narrow-terminal case: fall back to key-only glyphs
        # (drop labels) to preserve the story.
        minimal_visible = len(_TIMELINE_STEPS) + sep_len * (len(_TIMELINE_STEPS) - 1)
        if minimal_visible >= cols:
            return
        pad_left = max(0, (cols - minimal_visible) // 2)
        out.write(_goto(row, 1 + pad_left))
        for i, (key, _, color) in enumerate(_TIMELINE_STEPS):
            glyph = "\u25CF" if hits.get(key, False) else "\u25CB"
            col_on = color if hits.get(key, False) else _DIM
            out.write(f"{col_on}{glyph}{_RESET}")
            if i < len(_TIMELINE_STEPS) - 1:
                out.write(separator)
        return

    pad_left = max(0, (cols - total_visible) // 2)
    out.write(_goto(row, 1 + pad_left))
    for i, (styled, _) in enumerate(pieces):
        out.write(styled)
        if i < len(pieces) - 1:
            out.write(separator)


class Chrome:
    """Full-screen framed layout for the live qemu log.

    Layout (1-indexed, as ANSI expects):
      row 1:         inverse-video title bar
      row 2:         right-aligned dim stats subtitle (or blank)
      row 3:         frame top with embedded "live kernel log" title
      rows 4..R-1:   framed log content
      row R:         frame bottom

    Also maintains the cross-iteration counter state. The constructor
    loads the state file; ``on_line`` detects fault / restart markers
    and bumps the counters in memory; ``teardown`` writes the updated
    state back if the iteration actually produced output.
    """

    # Row assignments for live-mode chrome:
    #   row 1             title bar
    #   row 2             stats subtitle (bench + counters)
    #   row 3             event timeline rail
    #   row 4             frame top border
    #   rows 5..rows-1    framed log content
    #   row rows          frame bottom border
    _TIMELINE_ROW = 3
    _FRAME_TOP_ROW = 4

    def __init__(
        self,
        color: bool,
        state: Dict[str, Any],
        bench: Optional[Dict[str, str]] = None,
    ) -> None:
        self.color = color
        self.stdout = sys.stdout
        self.rows, self.cols = _detect_size()
        self._recompute_layout()
        self.log_lines: Deque[str] = collections.deque(maxlen=self.content_height)
        self._resize_pending = False
        self.state = state
        self.bench = bench
        self.timeline_hits = _initial_timeline_hits()
        self._saw_any_line = False

    def _recompute_layout(self) -> None:
        # Content spans rows (FRAME_TOP_ROW + 1)..(rows - 1) inclusive.
        self.content_height = max(1, self.rows - self._FRAME_TOP_ROW - 1)
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
        # Persist cross-iteration counters if this iteration actually ran.
        if self._saw_any_line:
            self.state["iterations"] = int(self.state.get("iterations", 0)) + 1
            self.state["last_iteration_iso"] = _iso_now()
            _save_state(self.state)
        self.stdout.write(_SHOW_CURSOR)
        self.stdout.write(_ALT_SCREEN_OFF)
        self.stdout.flush()

    def _erase_and_draw_static(self) -> None:
        out = self.stdout
        out.write(_CLEAR)
        _draw_title_bar(out, self.cols, self.state, self.bench)
        _render_timeline_rail(out, self._TIMELINE_ROW, self.cols, self.timeline_hits)
        _draw_outer_frame(
            out,
            self.rows,
            self.cols,
            embedded_title=_FRAME_TITLE_LIVE,
            top_row=self._FRAME_TOP_ROW,
        )

    def _redraw_title(self) -> None:
        _draw_title_bar(self.stdout, self.cols, self.state, self.bench)

    def _redraw_timeline(self) -> None:
        _render_timeline_rail(self.stdout, self._TIMELINE_ROW, self.cols, self.timeline_hits)

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
            row = self._FRAME_TOP_ROW + 1 + i
            raw = lines[i] if i < len(lines) else ""
            out.write(_goto(row, 3))
            out.write(self._fit_content(raw))
        out.flush()

    def on_line(self, raw: str) -> None:
        if self._resize_pending:
            self._resize_pending = False
            self._apply_resize()

        stripped = raw.rstrip("\r\n")
        state_changed = False
        if _FAULT_LINE.match(stripped):
            self.state["faults_seen"] = int(self.state.get("faults_seen", 0)) + 1
            state_changed = True
        if _RESTART_LINE.match(stripped):
            self.state["restarts_seen"] = int(self.state.get("restarts_seen", 0)) + 1
            state_changed = True

        timeline_changed = _update_timeline_hits(self.timeline_hits, stripped)

        self._saw_any_line = True
        self.log_lines.append(stripped)
        self._draw_content()
        if state_changed:
            # Bump the visible subtitle; cheap — 2 rows of redraw.
            self._redraw_title()
        if timeline_changed:
            self._redraw_timeline()
        if state_changed or timeline_changed:
            self.stdout.flush()


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
    "      BOOT                       kernel started",
    "      [uart] ready               UART service up (user space)",
    "      [sup] ready                supervisor up (user space)",
    "      [fault] task_b dead        uart_server faulted (on purpose)",
    "      [sup] restarted uart       supervisor restarted the service",
    "      [uart] ready (again)       service resumed — no kernel reboot",
    "      A (continues)              writer task never stopped",
    "",
    "",
    "  The microkernel contained the fault in user space.",
    "  A monolithic equivalent would panic or reboot on the same crash.",
]


def _visible_len(s: str) -> int:
    """Strip ANSI SGR sequences and return visible character count.
    Kernel output uses ASCII + Unicode BMP characters that are all
    single-cell on the terminals we target, so character count ≈ cell
    count."""
    return len(re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", s))


def _render_slide(
    body: List[str],
    footer_template: str,
    duration: int,
    color: bool,
    state: Optional[Dict[str, Any]] = None,
    bench: Optional[Dict[str, str]] = None,
    tail_lines: Optional[List[str]] = None,
) -> int:
    """Draw the title bar + outer frame, then render `body` centered
    inside the content area, with a live countdown footer. Holds for
    `duration` seconds. Restores cursor and clears the screen on exit.

    If ``state`` or ``bench`` is provided, the stats subtitle row is
    rendered on row 2. If ``tail_lines`` is non-empty, each is rendered
    on its own row below the body (after a spacer row), centered on
    its own visible length."""
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
    _draw_title_bar(out, cols, state, bench)
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
        out.write(_goto(row, 3 + block_left_pad) + line)

    # Optional tail lines rendered below the body, each centered.
    if tail_lines:
        start_row = content_top + top_pad + len(visible_body) + 1
        for i, line in enumerate(tail_lines):
            row = start_row + i
            if row >= content_bottom:
                break
            visible = _visible_len(line)
            if visible > content_width:
                line = line[:content_width]
                visible = content_width
            pad_left = max(0, (content_width - visible) // 2)
            out.write(_goto(row, 3 + pad_left) + _DIM + line + _RESET)

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
    state = _load_state()
    bench = _load_bench()

    chrome = Chrome(color=color, state=state, bench=bench) if framed else None
    _install_signal_handlers(chrome)

    stdin_bin = sys.stdin.buffer
    stdout = sys.stdout
    saw_any_line = False

    if chrome is not None:
        chrome.setup()

    try:
        while True:
            raw = stdin_bin.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace")
            saw_any_line = True
            if chrome is not None:
                chrome.on_line(line)
            else:
                stripped = line.rstrip("\r\n")
                if _FAULT_LINE.match(stripped):
                    state["faults_seen"] = int(state.get("faults_seen", 0)) + 1
                if _RESTART_LINE.match(stripped):
                    state["restarts_seen"] = int(state.get("restarts_seen", 0)) + 1
                stdout.write(_colorize_free(line) if color else line)
                stdout.flush()
    finally:
        if chrome is not None:
            chrome.teardown()
        elif saw_any_line:
            # Non-framed path: still persist counter increments.
            state["iterations"] = int(state.get("iterations", 0)) + 1
            state["last_iteration_iso"] = _iso_now()
            _save_state(state)
    return 0


def cmd_preroll() -> int:
    if _plain_mode() or not sys.stdout.isatty():
        return 0
    _install_signal_handlers(None)
    duration = _env_int("DEMO_PREROLL", 3)
    state = _load_state()
    bench = _load_bench()
    return _render_slide(
        body=_PREROLL_BODY,
        footer_template="starting in {n} second{s}...",
        duration=duration,
        color=_color_enabled(),
        state=state,
        bench=bench,
    )


def cmd_postroll() -> int:
    if _plain_mode() or not sys.stdout.isatty():
        return 0
    _install_signal_handlers(None)
    duration = _env_int("DEMO_POSTROLL", 5)
    state = _load_state()
    bench = _load_bench()
    # Build tail lines: session totals (once we've done any iterations)
    # plus the frozen benchmark baseline as quantitative grounding for
    # the qualitative recap above.
    tail_lines: List[str] = []
    if int(state.get("iterations", 0)) > 0:
        tail_lines.append(_format_counters_long(state))
    bench_line = _format_bench_long(bench)
    if bench_line:
        tail_lines.append(bench_line)
    return _render_slide(
        body=_POSTROLL_BODY,
        footer_template="next run in {n} second{s}...",
        duration=duration,
        color=_color_enabled(),
        state=state,
        bench=bench,
        tail_lines=tail_lines or None,
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
