# Raspberry Pi 4 Deployment (future)

M0-M10 is validated on QEMU `virt`. Raspberry Pi 4 hardware bring-up and
validation remain future work.

The build currently produces `build/kernel8.img`, which is the artifact planned
for upcoming Pi 4 boot tests.

Planned steps (later):
- Copy `kernel8.img` to the Pi boot partition.
- Configure `config.txt` for 64-bit kernel boot.
- Switch UART driver to the Pi4 mini-UART implementation.
