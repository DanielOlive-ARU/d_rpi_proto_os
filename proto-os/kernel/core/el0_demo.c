#include "kernel/arch.h"
#include "kernel/config.h"
#include "kernel/drivers.h"
#include "kernel/el0.h"
#include "kernel/panic.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"

#define EL0_DEMO_STACK_SIZE 4096UL

extern void el0_enter(uint64_t entry, uint64_t sp_el0, uint64_t spsr);
extern char __el0_demo_entry;
extern char __el0_demo_msg;
extern char __el0_demo_msg_end;

__attribute__((section(".el0_sandbox.data"), aligned(16)))
static uint8_t g_el0_demo_stack[EL0_DEMO_STACK_SIZE];

static void validate_el0_sandbox_placement(void) {
  uintptr_t entry_addr = (uintptr_t)&__el0_demo_entry;
  uintptr_t msg_addr = (uintptr_t)&__el0_demo_msg;
  uint64_t msg_len = (uint64_t)((uintptr_t)&__el0_demo_msg_end - msg_addr);
  uintptr_t stack_bottom = (uintptr_t)g_el0_demo_stack;
  uintptr_t stack_top = stack_bottom + sizeof(g_el0_demo_stack);

  if (entry_addr < EL0_SANDBOX_BASE || entry_addr >= EL0_SANDBOX_END) {
    panic("EL0 entry outside sandbox");
  }

  if (msg_addr < EL0_SANDBOX_BASE || msg_addr >= EL0_SANDBOX_END ||
      msg_len > (uint64_t)(EL0_SANDBOX_END - msg_addr)) {
    panic("EL0 message outside sandbox");
  }

  if (stack_bottom < EL0_SANDBOX_BASE || stack_bottom >= EL0_SANDBOX_END ||
      stack_top > EL0_SANDBOX_END) {
    panic("EL0 stack outside sandbox");
  }
}

static __attribute__((noreturn)) void el0_return_handler(void) {
  /*
   * Entered via exception return (eret) in EL1h, not via a normal call path.
   * Continue boot from here so we never depend on LR/x30 from EL0 state.
   */
  syscall_clear_el0_exit_target();
  uart_puts("[el0] returned to el1\n");

  gic_init();
  timer_init();
#if DEBUG_EARLY
  uart_puts("EARLY: timer configured\n");
#endif
  arch_enable_irq();
  thread_start();
  panic("thread_start returned");
}

void el0_demo_run_once(void) {
  uint64_t user_sp;

  validate_el0_sandbox_placement();

  syscall_set_el0_exit_target((uint64_t)(uintptr_t)el0_return_handler);
  user_sp = (uint64_t)(uintptr_t)(g_el0_demo_stack + sizeof(g_el0_demo_stack));
  el0_enter((uint64_t)(uintptr_t)&__el0_demo_entry, user_sp, SPSR_EL0T_MASKED);

  syscall_clear_el0_exit_target();
  panic("EL0 demo returned unexpectedly");
}
