#include "kernel/config.h"
#include "kernel/drivers.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"

static int el0_write_range_ok(const char *buf, uint64_t len) {
  uintptr_t start;
  uintptr_t sandbox_end = EL0_SANDBOX_END;

  if (buf == 0) {
    return 0;
  }

  start = (uintptr_t)buf;

  if (len == 0) {
    return start >= EL0_SANDBOX_BASE && start < sandbox_end;
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

uint64_t syscall_dispatch(struct trap_frame *tf, enum syscall_origin origin) {
  uint64_t i;
  const char *buf;
  uint64_t len;

  switch (tf->x[8]) {
    case SYS_yield:
      if (origin == SYSCALL_ORIGIN_EL0) {
        thread_user_trap_redirect(tf, TASK_RETURN_YIELD);
        return 0;
      }
      asm volatile("yield");
      return 0;
    case SYS_time_ticks:
      return thread_ticks_now();
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
      if (origin != SYSCALL_ORIGIN_EL0) {
        return (uint64_t)-1;
      }
      thread_user_trap_redirect(tf, TASK_RETURN_EXIT);
      return 0;
    default:
      return (uint64_t)-1;
  }
}
