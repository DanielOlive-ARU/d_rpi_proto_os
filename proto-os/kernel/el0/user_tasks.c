#include "kernel/syscall.h"
#include "kernel/types.h"

#define EL0_TEXT __attribute__((section(".el0_sandbox.text"), used))
#define EL0_RODATA __attribute__((section(".el0_sandbox.rodata"), used))

static const char g_task_a_msg[] EL0_RODATA = "A\n";
static const char g_task_b_msg[] EL0_RODATA = "B\n";

static inline uint64_t el0_syscall2(uint64_t nr, uint64_t arg0, uint64_t arg1) {
  register uint64_t x0 asm("x0") = arg0;
  register uint64_t x1 asm("x1") = arg1;
  register uint64_t x8 asm("x8") = nr;

  asm volatile("svc #0"
               : "+r"(x0)
               : "r"(x1), "r"(x8)
               : "x2", "x3", "x4", "x5", "x6", "x7",
                 "x9", "x10", "x11", "x12", "x13", "x14",
                 "x15", "x16", "x17", "x18", "cc", "memory");
  return x0;
}

static inline uint64_t el0_time_ticks(void) {
  return el0_syscall2(SYS_time_ticks, 0, 0);
}

static inline void el0_write(const char *buf, uint64_t len) {
  (void)el0_syscall2(SYS_write, (uint64_t)(uintptr_t)buf, len);
}

static inline void el0_yield(void) {
  (void)el0_syscall2(SYS_yield, 0, 0);
}

static inline int tick_reached(uint64_t now, uint64_t target) {
  return (int64_t)(now - target) >= 0;
}

void __el0_task_a_entry(void) EL0_TEXT __attribute__((noreturn));
void __el0_task_b_entry(void) EL0_TEXT __attribute__((noreturn));

void __el0_task_a_entry(void) {
  uint64_t next = el0_time_ticks() + 200ULL;

  for (;;) {
    uint64_t now = el0_time_ticks();
    if (tick_reached(now, next)) {
      el0_write(g_task_a_msg, sizeof(g_task_a_msg) - 1);
      next += 200ULL;
    }
    el0_yield();
  }
}

void __el0_task_b_entry(void) {
  uint64_t next = el0_time_ticks() + 200ULL;

  for (;;) {
    uint64_t now = el0_time_ticks();
    if (tick_reached(now, next)) {
      el0_write(g_task_b_msg, sizeof(g_task_b_msg) - 1);
      next += 200ULL;
    }
    el0_yield();
  }
}
