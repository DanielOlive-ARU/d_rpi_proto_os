#ifndef KERNEL_SUPERVISOR_H
#define KERNEL_SUPERVISOR_H

#include "kernel/arch.h"
#include "kernel/types.h"

void supervisor_init(void);
void supervisor_note_task_death(uint32_t slot);
uint64_t supervisor_syscall_wait(struct trap_frame *tf);
uint64_t supervisor_syscall_restart(uint64_t slot);

#endif
