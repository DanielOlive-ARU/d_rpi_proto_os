#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

#include "kernel/types.h"

#define THREAD_QUANTUM_TICKS 10U

enum thread_state {
  THREAD_RUNNABLE = 0,
  THREAD_RUNNING = 1
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

struct thread {
  struct thread_ctx ctx;
  enum thread_state state;
  uint32_t quantum_left;
  uint32_t slot;
  void (*entry)(void *arg);
  void *arg;
  const char *name;
};

void context_switch(struct thread_ctx *old_ctx, struct thread_ctx *new_ctx);

void thread_system_init(void);
void thread_start(void) __attribute__((noreturn));
void thread_tick_irq(void);
void thread_resched_point(void);
uint64_t thread_ticks_now(void);

#endif
