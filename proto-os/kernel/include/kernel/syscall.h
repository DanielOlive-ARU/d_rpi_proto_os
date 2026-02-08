#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "kernel/arch.h"
#include "kernel/types.h"

enum {
  SYS_yield = 0,
  SYS_time_ticks = 1,
  SYS_write = 2
};

uint64_t syscall_dispatch(struct trap_frame *tf);

#endif
