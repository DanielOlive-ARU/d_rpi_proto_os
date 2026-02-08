#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "kernel/arch.h"
#include "kernel/types.h"

enum syscall_origin {
  SYSCALL_ORIGIN_EL1 = 0,
  SYSCALL_ORIGIN_EL0 = 1
};

enum {
  SYS_yield = 0,
  SYS_time_ticks = 1,
  SYS_write = 2,
  SYS_exit = 3
};

uint64_t syscall_dispatch(struct trap_frame *tf, enum syscall_origin origin);
void syscall_set_el0_exit_target(uint64_t elr);
void syscall_clear_el0_exit_target(void);

#endif
