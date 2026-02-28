#include "kernel/arch.h"
#include "kernel/drivers.h"
#include "kernel/panic.h"
#include "kernel/printk.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"

void arch_enable_irq(void) {
  asm volatile("msr daifclr, #2" ::: "memory");
}

void arch_disable_irq(void) {
  asm volatile("msr daifset, #2" ::: "memory");
}

void exception_sync_el1(struct trap_frame *tf) {
  uint64_t ec = (tf->esr >> 26) & 0x3Fu;

  if (ec == 0x15u) {
    tf->x[0] = syscall_dispatch(tf, SYSCALL_ORIGIN_EL1);
    return;
  }

  uart_puts("[sync] el1 esr=0x");
  printk_hex_u64(tf->esr);
  uart_puts(" elr=0x");
  printk_hex_u64(tf->elr);
  uart_puts("\n");
  panic("EL1 sync exception");
}

void exception_sync_el0(struct trap_frame *tf) {
  uint64_t ec = (tf->esr >> 26) & 0x3Fu;

  if (ec == 0x15u) {
    tf->x[0] = syscall_dispatch(tf, SYSCALL_ORIGIN_EL0);
    return;
  }

  thread_user_fault(tf);
}

void exception_irq(struct trap_frame *tf) {
  (void)tf;
  gic_handle_irq();
}
