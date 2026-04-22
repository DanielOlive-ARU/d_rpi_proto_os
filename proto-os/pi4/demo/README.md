# proto-os Pi 4 expo demo

Self-contained scripts for running the proto-os MICRO demo on a
Raspberry Pi 4. The demo is the QEMU `virt` cortex-a57 build of the
MICRO kernel, run inside `qemu-system-aarch64` on top of Raspberry Pi
OS Lite. The visitor sees a real Pi 4 booting the prototype OS,
including the supervisor crash/restart sequence (M10).

This is **not** the bare-metal Pi 4 port. That work lives on the
`pi4_demo_smoke` branch and is documented as future work.

## What's in this folder

| File                          | Purpose                                                                 |
| ----------------------------- | ----------------------------------------------------------------------- |
| `setup_pi.sh`                 | One-time, online: installs QEMU + cross-named toolchain, builds kernel. |
| `run_demo.sh`                 | Repeatable, offline: loops the MICRO demo. Calls the presenter. |
| `presenter.py`                | Python 3 stdlib-only presentation layer — frames, colorizes, and narrates the demo. Runs in three modes (`live`, `preroll`, `postroll`). |
| `assets/bench_baseline.txt`   | Frozen MONO/MICRO benchmark medians (from commit `d154dcb`) used by the postroll slide and title subtitle for quantitative grounding. |
| `README.md`                   | This file.                                                              |

## Hardware required

- Raspberry Pi 4 (any RAM size; 2 GB+ comfortable)
- microSD card with Raspberry Pi OS Lite (64-bit) freshly flashed
- Power supply (official 5V 3A USB-C recommended)
- HDMI cable + monitor (visitor-facing display)
- USB keyboard (for log-in if not using SSH)
- Ethernet cable (for setup; also useful for peer-to-peer SSH at the
  expo if your Pi is offline)

## Quick start (TL;DR)

On a Pi with internet (one-time):

```bash
sudo apt install -y git
git clone https://github.com/DanielOlive-ARU/d_rpi_proto_os.git
cd d_rpi_proto_os
git checkout pi4_demo_polish
cd proto-os/pi4/demo
./setup_pi.sh
```

Run the demo (any time after setup, no internet needed):

```bash
./run_demo.sh
```

Stop with **Ctrl+C**.

## Detailed setup procedure

Steps 1-3 require internet and should be done **before** the expo. Step 4
needs no internet.

### 1. Flash a fresh SD card

Use Raspberry Pi Imager. OS: **Raspberry Pi OS Lite (64-bit)**.

In the Imager Edit Settings dialog (gear icon), set at minimum:

- Hostname: pick something memorable
- Username + password
- Wireless LAN: home Wi-Fi credentials (only needed for setup; Wi-Fi
  is irrelevant at the expo if the demo is offline)
- Services tab: **enable SSH**, password authentication

Write the card. Eject. Insert into the Pi.

### 2. First boot, log in

Plug HDMI + keyboard + Ethernet (or rely on Wi-Fi from Imager
config) + power. Wait ~60 seconds for first-boot expansion.

From your laptop:

```bash
ssh <user>@<hostname>.local
# or, if .local resolution fails on your network:
ssh <user>@<pi-ip-address>
```

### 3. Run setup_pi.sh

```bash
sudo apt install -y git
git clone https://github.com/DanielOlive-ARU/d_rpi_proto_os.git
cd d_rpi_proto_os
git checkout pi4_demo_polish
cd proto-os/pi4/demo
./setup_pi.sh
```

This installs:

- `qemu-system-arm` (provides `qemu-system-aarch64`)
- `gcc-aarch64-linux-gnu` and `binutils-aarch64-linux-gnu`
  (cross-named toolchain — produces aarch64 binaries on aarch64
  hosts; what the proto-os Makefile expects)

…then builds the MICRO kernel as `build/kernel.elf`.

After it completes, the Pi has everything it needs to run the demo
without further network access.

### 4. Run the demo

```bash
./run_demo.sh
```

Each iteration goes through three framed screens:

1. **Pre-roll slide** — ASCII architecture diagram of `task_a` /
   `uart_server` / `supervisor` with a bullet list of what's about to
   happen; holds for ~3 seconds with a live countdown.
2. **Live view** — the MICRO kernel runs under qemu for
   `DEMO_DURATION` seconds. The visitor sees a framed, colorized
   kernel log with a six-station event-timeline rail
   (`boot → uart → sup → FAULT → restart → OK`) above it and a dim
   stats subtitle showing the frozen MONO-vs-MICRO baseline cycles
   and the session restart counter.
3. **Post-roll slide** — a ✔/✖ recap of the arc that just played,
   plus two dim tail lines showing session totals and the frozen
   benchmark baseline. Holds for `DEMO_POSTROLL` seconds (defaults
   to `PAUSE_BETWEEN`).

The transitions stay framed with the same title bar throughout, so
the visitor never sees a bare shell prompt between iterations.

Stop with **Ctrl+C**.

To skip to the next loop iteration without stopping: **Ctrl+A then X**
(the QEMU exit chord) — the wrapping loop restarts QEMU.

## Manual auto-login (optional, do after setup_pi.sh)

To make the Pi drop straight into a logged-in shell on tty1 at boot
(useful for an expo where the visitor shouldn't see a login prompt):

```bash
sudo systemctl edit getty@tty1.service
```

In the editor, add:

```
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin <YOUR_USERNAME> --noclear %I $TERM
```

Save. Reboot:

```bash
sudo reboot
```

The Pi will now boot directly into a logged-in shell on tty1.

To revert: `sudo systemctl revert getty@tty1.service && sudo reboot`.

## Running offline at the expo

After `setup_pi.sh` has completed once, the Pi does not need network
access to run the demo. It only needs:

- power
- HDMI + monitor for the visitor display
- (optional) keyboard if you want to invoke `run_demo.sh` manually
- (optional) Ethernet cable for laptop-to-Pi SSH if you want to
  monitor / restart the demo without a keyboard at the table

For peer-to-peer SSH over a direct Ethernet cable: most modern
Linux/macOS laptops will negotiate link-local IPv4 (169.254.x.x) when
both ends have no DHCP server. From the laptop:

```bash
ssh <user>@<pi-hostname>.local
```

If `.local` doesn't resolve, on the laptop run `arp -a` after a few
seconds of plugging in the cable to find the Pi's link-local IP.

## Troubleshooting

| Symptom | Likely cause | Action |
| --- | --- | --- |
| `setup_pi.sh` fails on `apt update` | No internet | Confirm Wi-Fi or Ethernet is connected; `ping 8.8.8.8` |
| `setup_pi.sh` fails on `make` | Build environment incomplete | Re-check the `aarch64-linux-gnu-gcc --version` line earlier in the script's output; reinstall `gcc-aarch64-linux-gnu` |
| `run_demo.sh` says "kernel not found" | `setup_pi.sh` not yet run, or run from wrong checkout | Run `setup_pi.sh` from the same checkout |
| QEMU starts but immediately exits | Toolchain issue produced an unbootable kernel | `make clean` then re-run `setup_pi.sh` |
| Tick output very slow (one tick every several seconds) | TCG emulation overhead — expected | This is normal for QEMU-on-Pi. Demo cadence is still meaningful. |
| Banner garbled / wrong size | Terminal too small | Resize the SSH client or use a larger HDMI resolution |
| Timeline rail squashes to glyphs only | Terminal < ~60 cols wide | Expected — labels drop so the rail still fits |
| Screen looks scrambled in a minimal terminal | Terminal does not support the alt-screen / cursor-positioning codes | `DEMO_PLAIN=1 ./run_demo.sh` falls back to plain colorized output |
| Unicode box / check / arrow glyphs show as `?` | Terminal not UTF-8 | Set `LANG=C.UTF-8` (or `en_GB.UTF-8`); Pi OS Lite defaults are fine |
| Stats / counters stuck at zero | Permission error writing `.state/counters.json` | Check `ls -la pi4/demo/.state/` and fix perms, or override `DEMO_STATE_FILE` to a writable path |

## Customising the demo

`run_demo.sh` and `presenter.py` honour these environment variables:

| Variable              | Default       | Purpose |
| --------------------- | ------------- | ------- |
| `DEMO_DURATION`       | `20`          | Seconds each QEMU iteration runs before being killed. |
| `PAUSE_BETWEEN`       | `5`           | Legacy inter-iteration pause; maps to `DEMO_POSTROLL` when the latter is unset. |
| `DEMO_PREROLL`        | `3`           | Seconds to hold the pre-run architecture slide. |
| `DEMO_POSTROLL`       | `PAUSE_BETWEEN` | Seconds to hold the post-run recap slide. |
| `ACCEL`               | unset         | QEMU accelerator. Set to `kvm` to try hardware acceleration if it becomes available. |
| `CPU`                 | `cortex-a57`  | Guest CPU model. Use `host` only with `ACCEL=kvm`. |
| `DEMO_PLAIN`          | unset         | `1` to force raw pass-through (no ANSI, no chrome, no slides). Useful when running inside pipelines or older terminals. |
| `NO_COLOR`            | unset         | Standard convention — any non-empty value disables color (framing stays). |
| `DEMO_NO_FRAME`       | unset         | `1` to keep colorization but drop the framed chrome + timeline + slides. |
| `DEMO_RESET_COUNTERS` | unset         | `1` to reset the cross-iteration counters at the top of the loop and then continue normally. |
| `DEMO_STATE_FILE`     | `.state/counters.json` (next to `presenter.py`) | Where cross-iteration counters are persisted. |
| `DEMO_BENCH_FILE`     | `assets/bench_baseline.txt` (next to `presenter.py`) | Frozen MONO/MICRO benchmark summary read by the presenter. |

Examples:

```bash
DEMO_DURATION=25 ./run_demo.sh
DEMO_RESET_COUNTERS=1 ./run_demo.sh        # zero the session counters
DEMO_PLAIN=1 ./run_demo.sh                 # fallback on narrow / non-ANSI terminals
ACCEL=kvm CPU=host ./run_demo.sh           # if KVM ever works on this Pi
```

### Cross-iteration counters

`presenter.py` maintains a small JSON state file at
`.state/counters.json` (gitignored) that tracks the number of
iterations completed, `[fault]` lines observed, and `[sup] restarted`
lines observed. The title-bar subtitle and the post-roll tail surface
these so a visitor arriving mid-day sees evidence of sustained
stability. Delete the file (or set `DEMO_RESET_COUNTERS=1`) to start
fresh.

### Frozen benchmark numbers

The kernel is built with `BENCH_MODE=OFF` so no live measurements are
emitted — the wow is behavioural, not numeric. To ground reviewers in
actual measurements, `presenter.py` loads a small snapshot of the
M11 benchmark session from `assets/bench_baseline.txt`
(median-of-run-medians at commit `d154dcb`, tagged
`bench_baseline_qemu`). See that file's header comment for the exact
source and caveats.

## What's NOT in this branch

- **Bare-metal Pi 4 port.** Lives on the `pi4_demo_smoke` branch as
  in-progress / deferred work.
- **Auto-login configuration.** Documented above for manual setup;
  intentionally not scripted.
- **Auto-launch on boot.** Run `run_demo.sh` manually each session.
