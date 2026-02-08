#include "kernel/config.h"
#include "kernel/drivers.h"
#include "kernel/syscall.h"

static volatile uint64_t g_el0_exit_target = 0;

static inline uint64_t read_cntvct(void) {
  uint64_t v;
  asm volatile("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

static int el0_write_range_ok(const char *buf, uint64_t len) {
  uintptr_t start;
  uintptr_t sandbox_end = EL0_SANDBOX_END;

  if (buf == 0) {
    return 0;
  }

  start = (uintptr_t)buf;

  if (len == 0) {
    return start >= EL0_SANDBOX_BASE && start <= sandbox_end;
  }

  if (start < EL0_SANDBOX_BASE || start >= sandbox_end) {
    return 0;
  }

  /* Overflow-safe bounds check: validate len against remaining range. */
  if (len > (uint64_t)(sandbox_end - start)) {
    return 0;
  }

  return 1;
}

void syscall_set_el0_exit_target(uint64_t elr) {
  g_el0_exit_target = elr;
}

void syscall_clear_el0_exit_target(void) {
  g_el0_exit_target = 0;
}

uint64_t syscall_dispatch(struct trap_frame *tf, enum syscall_origin origin) {
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
      if (origin == SYSCALL_ORIGIN_EL0 && !el0_write_range_ok(buf, len)) {
        return (uint64_t)-1;
      }
      for (i = 0; i < len; i++) {
        uart_putc(buf[i]);
      }
      return i;
    case SYS_exit:
      if (origin != SYSCALL_ORIGIN_EL0 || g_el0_exit_target == 0) {
        return (uint64_t)-1;
      }
      tf->elr = g_el0_exit_target;
      tf->spsr = SPSR_EL1H_MASKED;
      return 0;
    default:
      return (uint64_t)-1;
  }
}
