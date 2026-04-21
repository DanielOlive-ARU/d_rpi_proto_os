# Benchmarking Guide (M11)

This document defines the supported benchmark modes and the reproducible run flow
for MONO vs MICRO measurements on QEMU `virt`.

## Scope

M11 adds the minimum instrumentation needed to measure:

1. MONO direct `SYS_write` latency
2. MICRO routed `SYS_write` latency
3. IPC round-trip latency on `EP_BENCH`
4. MICRO external recovery interruption window

The core architecture does not change:

- `BENCH_MODE=OFF` preserves the normal M10 demo behavior
- timer IRQs stay enabled
- IPC remains static-endpoint, fixed-size, kernel-copy
- no per-task address spaces are introduced

## Benchmark Modes

- `BENCH_MODE=OFF`
  - normal M10 demo output
  - MICRO still injects the one-shot `task_b` crash and supervisor restart
- `BENCH_MODE=LATENCY`
  - `task_a` runs the latency benchmarks
  - `task_b` remains the UART server
  - `task_c` becomes the `EP_BENCH` echo server
  - the `task_b` crash is disabled
- `BENCH_MODE=RECOVERY`
  - `task_a` measures the external recovery interruption window
  - `task_b` remains the UART server
  - `task_c` remains the supervisor
  - the `task_b` crash remains enabled

## Timing Source

Fine-grained timing uses direct EL0 reads of `CNTVCT_EL0`.

- EL1 enables EL0 virtual counter access through `CNTKCTL_EL1.EL0VCTEN=1`
- EL0 reads use serialized boundaries around `CNTVCT_EL0`
- `cntfrq_hz` is emitted in metadata so cycle counts can be converted offline

## Fixed Benchmark Parameters

- `BENCH_ITERATIONS=1000`
- `SYS_write` payload size: 1 byte
- `EP_BENCH` IPC payload size: 1 byte

The routed `SYS_write` benchmark is intentionally end-to-end. It includes the
real UART service-path cost, including the UART emission itself. Use the IPC
round-trip metric to isolate the extra IPC/service overhead more directly.

## Structured Output

Benchmark modes emit only structured benchmark lines.

Latency result lines:

```text
BENCH test=<name> flavor=<MONO|MICRO> iter=<N> failures=<N> min_cycles=<N> median_cycles=<N> max_cycles=<N> total_cycles=<N>
```

Recovery result line:

```text
BENCH test=recovery_window flavor=MICRO iter=1 failures=<0|1> cycles=<N>
```

Metadata lines:

```text
BENCH_META schema=1 phase=<start|fault_injected|end> flavor=<...> mode=<...> cntfrq_hz=<...> iterations=<...>
BENCH_META schema=1 phase=fault_injected flavor=MICRO mode=RECOVERY task=task_b esr=0x<hex> elr=0x<hex> far=0x<hex>
```

## Named Run Targets

The supported benchmark targets clean, rebuild with the correct mode, and run QEMU:

```bash
make bench-mono-qemu
make bench-micro-qemu
make recovery-micro-qemu
```

Normal demo targets remain:

```bash
make mono-qemu
make micro-qemu
```

## Recommended Run Flow

Run sequentially and keep flavor/mode changes isolated:

```bash
make clean && timeout 16s make mono-qemu
make clean && timeout 16s make micro-qemu

timeout 16s make bench-mono-qemu | tee mono_latency.log
timeout 16s make bench-micro-qemu | tee micro_latency.log
timeout 16s make recovery-micro-qemu | tee micro_recovery.log
```

## Expected Output Shape

`bench-mono-qemu`:

- one `BENCH_META ... phase=start ... flavor=MONO mode=LATENCY ...`
- one `BENCH test=sys_write flavor=MONO ...`
- one `BENCH test=ipc_roundtrip flavor=MONO ...`
- one `BENCH_META ... phase=end ...`

`bench-micro-qemu`:

- one `BENCH_META ... phase=start ... flavor=MICRO mode=LATENCY ...`
- one `BENCH test=sys_write flavor=MICRO ...`
- one `BENCH test=ipc_roundtrip flavor=MICRO ...`
- one `BENCH_META ... phase=end ...`

`recovery-micro-qemu`:

- one `BENCH_META ... phase=start ... flavor=MICRO mode=RECOVERY ...`
- one `BENCH_META ... phase=fault_injected ... task=task_b ...`
- one `BENCH test=recovery_window flavor=MICRO iter=1 failures=<0|1> cycles=...`
- one `BENCH_META ... phase=end ...`

`recovery_window` measures the elapsed time of a client routed `SYS_write`
that spans the crash and supervised restart from the client perspective. In M11
it is intentionally one sample per boot/run because the injected crash is one-shot.

## Environment Note

For cleaner build behavior and more credible timing runs, prefer a Linux/WSL
filesystem path instead of `/mnt/*` when collecting evidence.
