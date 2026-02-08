#include "kernel/drivers.h"
#include "kernel/syscall.h"

static inline uint64_t read_cntvct(void) {
  uint64_t v;
  asm volatile("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

uint64_t syscall_dispatch(struct trap_frame *tf) {
  uint64_t i;
  const char *buf;
  uint64_t len;

  switch (tf->x[8]) {
    case SYS_yield:
      asm volatile("yield");
      return 0;
    case SYS_time_ticks:
      return read_cntvct();
    case SYS_write:
      buf = (const char *)(uintptr_t)tf->x[0];
      len = tf->x[1];
      if (buf == 0) {
        return (uint64_t)-1;
      }
      for (i = 0; i < len; i++) {
        uart_putc(buf[i]);
      }
      return i;
    default:
      return (uint64_t)-1;
  }
}
