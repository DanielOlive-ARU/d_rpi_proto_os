#include "kernel/arch.h"
#include "kernel/panic.h"

void arch_enable_irq(void) {
  asm volatile("msr daifclr, #2" ::: "memory");
}

void arch_disable_irq(void) {
  asm volatile("msr daifset, #2" ::: "memory");
}

void exception_sync_el1(struct trap_frame *tf) {
  (void)tf;
  panic("EL1 sync exception");
}

void exception_sync_el0(struct trap_frame *tf) {
  (void)tf;
  panic("EL0 sync exception");
}

void exception_irq(struct trap_frame *tf) {
  (void)tf;
  gic_handle_irq();
}
