#ifndef KERNEL_EL0_H
#define KERNEL_EL0_H

struct user_task_ctx;

void el0_resume(struct user_task_ctx *ctx) __attribute__((noreturn));
void __el0_task_a_entry(void);
void __el0_task_b_entry(void);

#endif
