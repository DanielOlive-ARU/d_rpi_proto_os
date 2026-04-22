# proto-os Pi 4 expo demo

Self-contained scripts for running the proto-os MICRO demo on a
Raspberry Pi 4. The demo is the QEMU `virt` cortex-a57 build of the
MICRO kernel, run inside `qemu-system-aarch64` on top of Raspberry Pi
OS Lite. The visitor sees a real Pi 4 booting the prototype OS,
including the supervisor crash/restart sequence (M10).

This is **not** the bare-metal Pi 4 port. That work lives on the
`pi4_demo_smoke` branch and is documented as future work.

## What's in this folder

| File           | Purpose                                                                 |
| -------------- | ----------------------------------------------------------------------- |
| `setup_pi.sh`  | One-time, online: installs QEMU + cross-named toolchain, builds kernel. |
| `run_demo.sh`  | Repeatable, offline: loops the MICRO demo on a banner-clear-restart cycle. |
| `README.md`    | This file.                                                              |

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

The screen clears, a banner explains what to look for, then QEMU
boots the MICRO kernel. Each cycle runs ~15 seconds (enough for the
crash + restart sequence and a few `[tick]` markers), pauses ~5
seconds, then restarts.

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

## Customising the demo

`run_demo.sh` honours four environment variables:

| Variable        | Default      | Purpose |
| --------------- | ------------ | ------- |
| `DEMO_DURATION` | `15`         | Seconds each QEMU iteration runs before being killed. |
| `PAUSE_BETWEEN` | `5`          | Seconds between iterations (loop pause). |
| `ACCEL`         | `tcg`        | QEMU accelerator. Set to `kvm` to try hardware acceleration if it becomes available. |
| `CPU`           | `cortex-a57` | Guest CPU model. Use `host` only with `ACCEL=kvm`. |

Examples:

```bash
DEMO_DURATION=20 ./run_demo.sh
ACCEL=kvm CPU=host ./run_demo.sh   # if KVM ever works on this Pi
```

## What's NOT in this branch

- **Bare-metal Pi 4 port.** Lives on the `pi4_demo_smoke` branch as
  in-progress / deferred work.
- **Auto-login configuration.** Documented above for manual setup;
  intentionally not scripted.
- **Auto-launch on boot.** Run `run_demo.sh` manually each session.
