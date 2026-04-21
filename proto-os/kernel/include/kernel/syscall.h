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
  SYS_exit = 3,
  SYS_ipc_call = 4,
  SYS_ipc_recv = 5,
  SYS_ipc_reply = 6,
  SYS_supervise_wait = 7,
  SYS_task_restart = 8
};

uint64_t syscall_dispatch(struct trap_frame *tf, enum syscall_origin origin);

#endif
