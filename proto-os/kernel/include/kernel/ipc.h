#ifndef KERNEL_IPC_H
#define KERNEL_IPC_H

#include "kernel/arch.h"
#include "kernel/types.h"

#define IPC_MSG_SIZE 256U

#define EP_NONE 0U
#define EP_ECHO 1U
#define EP_COUNT 2U

void ipc_init(void);
uint64_t ipc_syscall_call(struct trap_frame *tf);
uint64_t ipc_syscall_recv(struct trap_frame *tf);
uint64_t ipc_syscall_reply(struct trap_frame *tf);

#endif
