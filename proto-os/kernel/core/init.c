#include "kernel/config.h"
#include "kernel/arch.h"
#include "kernel/drivers.h"
#include "kernel/printk.h"
#include "kernel/syscall.h"

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

void kernel_main(void) {
  uint64_t sample_ticks;
  static const char svc_ok[] = "[svc] ok\n";

  uart_init();
  uart_puts("BOOT\n");

#if DEBUG_EARLY
  uart_puts("EARLY: boot.S reached EL1\n");
#endif

  uart_puts("[boot] proto-os (" KERNEL_FLAVOR_STR ")\n");
  uart_puts("[boot] entered kernel_main\n");

  vectors_init();
#if DEBUG_EARLY
  uart_puts("EARLY: vectors installed\n");
#endif

  (void)svc_write(svc_ok, sizeof(svc_ok) - 1);
  (void)svc_call2(SYS_yield, 0, 0);
  sample_ticks = svc_time_ticks();
  uart_puts("[svc] ticks ");
  printk_u64(sample_ticks);
  uart_puts("\n");

  gic_init();
  timer_init();
#if DEBUG_EARLY
  uart_puts("EARLY: timer configured\n");
#endif

  arch_enable_irq();

  for (;;) {
    asm volatile("wfi");
  }
}
