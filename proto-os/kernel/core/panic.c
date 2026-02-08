#include "kernel/panic.h"
#include "kernel/drivers.h"

__attribute__((noreturn))
void panic(const char *msg) {
  uart_puts("[panic] ");
  uart_puts(msg);
  uart_puts("\n");

  for (;;) {
    asm volatile("wfi");
  }
}
