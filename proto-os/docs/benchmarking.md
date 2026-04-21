# Benchmarking Plan (M10 baseline)

This document defines the measurement plan for MONO vs MICRO comparisons. It does
not imply that benchmark instrumentation is fully implemented yet.

## Metrics to Measure
1. Syscall latency:
   - MONO: direct `SYS_write`
   - MICRO: routed `SYS_write` via `EP_UART` service path
   - Compare per-call round-trip cost with identical payload sizes.
2. IPC round-trip latency:
   - `ipc_call -> server recv -> server reply -> caller wake`.
3. Service overhead:
   - Extra cost of MICRO `uart_server` hop(s) versus MONO direct kernel path.
4. Fault/restart recovery time (MICRO):
   - Time from injected `brk #0` in `uart_server` to first successful routed write
     after supervisor restart.
5. Recovery interruption window (MICRO):
   - Number of delayed or missed writer cycles while service is down.

## Current vs Future Instrumentation
Current available measurements:
- Coarse timing via `SYS_time_ticks` (1 ms scheduler tick resolution).
- Marker-based event observation from serial output logs.

Future instrumentation:
- Fine-grained timing via `CNTVCT_EL0` cycle counter reads from EL0.
- Quieter benchmark mode (reduced console noise, e.g. `DEBUG_TICK=0`) for cleaner
  latency signal.

## Methodology
- Primary environment: QEMU `virt` (`cortex-a57`) for repeatable baseline runs.
- Secondary environment (future): Raspberry Pi 4 (`cortex-a72`) once hardware path
  is validated.
- Keep benchmark payload sizes fixed per run.
- Capture complete serial logs for offline parsing and result extraction.

## Reproducibility Commands
Clean smoke checks before benchmark runs:
```bash
make clean && timeout 16s make mono-qemu
make clean && timeout 16s make micro-qemu
```

Build-only flavor switch sanity check:
```bash
make KERNEL_FLAVOR=MONO build
make KERNEL_FLAVOR=MICRO build
```

Benchmark capture runs:
```bash
make clean && make KERNEL_FLAVOR=MONO build
./scripts/run_qemu.sh 2>&1 | tee mono_bench.log

make clean && make KERNEL_FLAVOR=MICRO build
./scripts/run_qemu.sh 2>&1 | tee micro_bench.log
```

## Status
Instrumentation for formal benchmark output is not fully implemented yet. This
document specifies what will be measured and how runs should be captured.
