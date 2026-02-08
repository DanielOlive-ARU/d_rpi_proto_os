# Raspberry Pi 4 Deployment (future)

Not implemented yet. M0-M4 is validated on QEMU `virt`; Pi4 bring-up remains future work.
The build also produces `build/kernel8.img` for later Pi 4 work.

Planned steps (later):
- Copy `kernel8.img` to the Pi boot partition.
- Configure `config.txt` for 64-bit kernel boot.
- Switch UART driver to the Pi4 mini-UART implementation.
