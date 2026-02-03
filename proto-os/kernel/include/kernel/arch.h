#ifndef KERNEL_ARCH_H
#define KERNEL_ARCH_H

#include "kernel/types.h"

struct trap_frame {
  uint64_t x[31];
  uint64_t sp;
  uint64_t elr;
  uint64_t spsr;
  uint64_t esr;
  uint64_t far;
};

static inline void mmio_write(uintptr_t addr, uint32_t value) {
  *(volatile uint32_t *)addr = value;
}

static inline uint32_t mmio_read(uintptr_t addr) {
  return *(volatile uint32_t *)addr;
}

static inline void isb(void) {
  asm volatile("isb" ::: "memory");
}

static inline void dsb_sy(void) {
  asm volatile("dsb sy" ::: "memory");
}

void vectors_init(void);
void gic_init(void);
void gic_handle_irq(void);
void timer_init(void);
void timer_handle_irq(void);
void arch_enable_irq(void);
void arch_disable_irq(void);

void exception_sync_el1(struct trap_frame *tf);
void exception_sync_el0(struct trap_frame *tf);
void exception_irq(struct trap_frame *tf);

#endif
