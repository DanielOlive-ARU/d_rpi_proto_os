# Benchmarking (future)

Planned comparisons for monolithic vs microkernel style:
- Boot time to first user process.
- IPC latency (call/reply round-trip).
- Context switch cost.
- System call overhead.
- IO throughput for UART and block devices (if added).

Current bring-up observables (M4):
- Timer heartbeat stability (`[tick]` once per 1000 ticks).
- Thread scheduling fairness via interleaved `A`/`B` markers.
- Flavor parity check: MONO and MICRO should differ only by boot banner at this stage.
