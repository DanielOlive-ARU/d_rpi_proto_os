#include "kernel/config.h"
#include "kernel/arch.h"
#include "kernel/drivers.h"
#include "kernel/ipc.h"
#include "kernel/mmu.h"
#include "kernel/panic.h"
#include "kernel/printk.h"
#include "kernel/supervisor.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"

#ifdef BENCH_MODE_OFF
static uint64_t svc_call2(uint64_t nr, uint64_t arg0, uint64_t arg1) {
  register uint64_t x0 asm("x0") = arg0;
  register uint64_t x1 asm("x1") = arg1;
  register uint64_t x8 asm("x8") = nr;

  asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
  return x0;
}

static uint64_t svc_write(const char *buf, uint64_t len) {
  return svc_call2(SYS_write, (uint64_t)(uintptr_t)buf, len);
}

static uint64_t svc_time_ticks(void) {
  return svc_call2(SYS_time_ticks, 0, 0);
}
#endif

void kernel_main(void) {
#ifdef PI4_SMOKE
  /* Step 1 Pi4 bring-up: absolute minimum path. Firmware has enable_uart=1
     active, so the mini-UART is already initialised. Emit a deterministic
     marker and halt. No vectors, no MMU, no scheduler — we want to isolate
     serial verification from every other bring-up risk. */
  uart_init();
  uart_puts("PI4 BOOT\n");
  for (;;) {
    asm volatile("wfi");
  }
#else

#ifdef BENCH_MODE_OFF
  uint64_t sample_ticks;
  static const char svc_ok[] = "[svc] ok\n";
#endif

  uart_init();
#ifdef BENCH_MODE_OFF
  uart_puts("BOOT\n");

#if DEBUG_EARLY
  uart_puts("EARLY: boot.S reached EL1\n");
#endif

  uart_puts("[boot] proto-os (" KERNEL_FLAVOR_STR ")\n");
  uart_puts("[boot] entered kernel_main\n");
#endif

  vectors_init();
#if defined(BENCH_MODE_OFF) && DEBUG_EARLY
  uart_puts("EARLY: vectors installed\n");
#endif

#ifdef BENCH_MODE_OFF
  (void)svc_write(svc_ok, sizeof(svc_ok) - 1);
  (void)svc_call2(SYS_yield, 0, 0);
  sample_ticks = svc_time_ticks();
  uart_puts("[svc] ticks ");
  printk_u64(sample_ticks);
  uart_puts("\n");
#endif

  thread_system_init();
  ipc_init();
  supervisor_init();
  mmu_init();
#ifdef BENCH_MODE_OFF
  uart_puts("[mmu] enabled identity map\n");
  uart_puts("[mmu] caches on\n");
#endif

  gic_init();
  timer_init();
#if defined(BENCH_MODE_OFF) && DEBUG_EARLY
  uart_puts("EARLY: timer configured\n");
#endif
#if !defined(BENCH_MODE_OFF)
  uart_puts("BENCH_META schema=1 phase=start flavor=" KERNEL_FLAVOR_STR
            " mode=" BENCH_MODE_STR " cntfrq_hz=");
  printk_u64(timer_counter_freq_hz());
  uart_puts(" iterations=");
  printk_u64(BENCH_ITERATIONS);
  uart_puts("\n");
#endif

  arch_enable_irq();
  thread_start();
  panic("thread_start returned unexpectedly");
#endif /* PI4_SMOKE */
}
