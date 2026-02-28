#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

#include "kernel/types.h"

#define THREAD_QUANTUM_TICKS 10U

enum thread_state {
  THREAD_RUNNABLE = 0,
  THREAD_RUNNING = 1,
  THREAD_STOPPED = 2
};

enum thread_kind {
  THREAD_IDLE = 0,
  THREAD_USER_TASK = 1
};

enum task_state {
  TASK_RUNNABLE = 0,
  TASK_RUNNING = 1,
  TASK_BLOCKED = 2,
  TASK_DEAD = 3
};

enum task_return_reason {
  TASK_RETURN_NONE = 0,
  TASK_RETURN_YIELD = 1,
  TASK_RETURN_EXIT = 2,
  TASK_RETURN_FAULT = 3
};

struct thread_ctx {
  uint64_t x19;
  uint64_t x20;
  uint64_t x21;
  uint64_t x22;
  uint64_t x23;
  uint64_t x24;
  uint64_t x25;
  uint64_t x26;
  uint64_t x27;
  uint64_t x28;
  uint64_t x29;
  uint64_t sp;
  uint64_t lr;
};

struct user_task_ctx {
  uint64_t x[31];
  uint64_t sp_el0;
  uint64_t elr;
  uint64_t spsr;
};

struct user_task {
  struct user_task_ctx ctx;
  enum task_state state;
  enum task_return_reason return_reason;
  uint64_t user_entry;
  uint64_t user_sp;
  uint64_t ttbr0_future;
  const char *name;
  uint8_t started;
};

struct thread {
  struct thread_ctx ctx;
  enum thread_state state;
  enum thread_kind kind;
  uint32_t quantum_left;
  uint32_t slot;
  void (*entry)(void *arg);
  void *arg;
  const char *name;
  struct user_task user;
};

struct trap_frame;

void context_switch(struct thread_ctx *old_ctx, struct thread_ctx *new_ctx);

void thread_system_init(void);
void thread_start(void) __attribute__((noreturn));
void thread_tick_irq(void);
void thread_resched_point(void);
uint64_t thread_ticks_now(void);
void thread_user_trap_redirect(struct trap_frame *tf, enum task_return_reason reason);
void thread_user_fault(struct trap_frame *tf);

#endif
