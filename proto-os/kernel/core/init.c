#include "kernel/config.h"
#include "kernel/drivers.h"
#include "kernel/arch.h"

void kernel_main(void) {
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
