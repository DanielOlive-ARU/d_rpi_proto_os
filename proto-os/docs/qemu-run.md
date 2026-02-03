# QEMU Run

From `proto-os/`:

```
make mono-qemu
```

This builds `build/kernel.elf` and runs:

```
qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -nographic -serial stdio -monitor none -kernel build/kernel.elf
```

Exit with Ctrl+C.
