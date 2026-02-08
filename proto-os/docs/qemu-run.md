# QEMU Run

Recommended on Windows hosts: run from WSL Linux filesystem, for example `~/src/proto-os`.
Avoid `/mnt/c/...` builds for regular development.

From `~/src/proto-os`:

```bash
make clean
make build
```

Run without rebuilding:

```bash
./scripts/run_qemu.sh
```

Underlying QEMU command:

```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -nographic -serial stdio -monitor none -kernel build/kernel.elf
```

Exit with Ctrl+C.
