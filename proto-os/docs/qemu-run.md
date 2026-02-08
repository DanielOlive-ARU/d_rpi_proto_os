# QEMU Run

Recommended on Windows hosts: run from WSL Linux filesystem, for example `~/src/proto-os`.
Avoid `/mnt/c/...` builds for regular development.

From `~/src/proto-os`:

```bash
make clean
make build
```

Or run a flavor build + launch in one command:

```bash
make mono-qemu
make micro-qemu
```

Run without rebuilding:

```bash
./scripts/run_qemu.sh
```

Expected runtime output includes:
- boot banners
- `[svc] ok` and `[svc] ticks ...`
- `[mmu] enabled identity map`
- `[mmu] caches on`
- one-time `hello from el0`
- one-time `[el0] returned to el1`
- `[tick] 1000` heartbeat
- interleaved `A` / `B` markers from M4 scheduler threads

Underlying QEMU command:

```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -nographic -serial stdio -monitor none -kernel build/kernel.elf
```

Exit with Ctrl+C.
