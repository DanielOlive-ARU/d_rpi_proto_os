#include "kernel/arch.h"
#include "kernel/drivers.h"
#include "kernel/thread.h"

#define THREAD_COUNT 3U
#define THREAD_SLOT_A 0U
#define THREAD_SLOT_B 1U
#define THREAD_SLOT_IDLE 2U
#define THREAD_STACK_SIZE 4096U
#define THREAD_PRINT_PERIOD_TICKS 200ULL

static struct thread g_threads[THREAD_COUNT];
static struct thread *g_current = 0;
static struct thread_ctx g_boot_ctx;

static volatile uint64_t g_sched_ticks = 0;
static volatile uint32_t g_resched_pending = 0;

__attribute__((aligned(16))) static uint8_t g_stack_a[THREAD_STACK_SIZE];
__attribute__((aligned(16))) static uint8_t g_stack_b[THREAD_STACK_SIZE];
__attribute__((aligned(16))) static uint8_t g_stack_idle[THREAD_STACK_SIZE];

static void thread_entry_a(void *arg);
static void thread_entry_b(void *arg);
static void thread_entry_idle(void *arg);
static void thread_bootstrap(void);

static inline uint64_t stack_top(uint8_t *stack, uint64_t size) {
  uintptr_t top = (uintptr_t)(stack + size);
  return (uint64_t)(top & ~((uintptr_t)0xFULL));
}

static inline int tick_reached(uint64_t now, uint64_t target) {
  return (int64_t)(now - target) >= 0;
}

static void thread_init_slot(struct thread *t,
                             uint32_t slot,
                             const char *name,
                             void (*entry)(void *arg),
                             void *arg,
                             uint8_t *stack,
                             uint64_t stack_size) {
  t->ctx.x19 = (uint64_t)(uintptr_t)entry;
  t->ctx.x20 = (uint64_t)(uintptr_t)arg;
  t->ctx.x21 = 0;
  t->ctx.x22 = 0;
  t->ctx.x23 = 0;
  t->ctx.x24 = 0;
  t->ctx.x25 = 0;
  t->ctx.x26 = 0;
  t->ctx.x27 = 0;
  t->ctx.x28 = 0;
  t->ctx.x29 = 0;
  t->ctx.sp = stack_top(stack, stack_size);
  t->ctx.lr = (uint64_t)(uintptr_t)thread_bootstrap;
  t->state = THREAD_RUNNABLE;
  t->quantum_left = THREAD_QUANTUM_TICKS;
  t->slot = slot;
  t->entry = entry;
  t->arg = arg;
  t->name = name;
}

static struct thread *pick_next_thread(void) {
  uint32_t i;
  uint32_t idx;
  uint32_t start_slot = g_current ? g_current->slot : THREAD_SLOT_IDLE;

  for (i = 1; i <= THREAD_COUNT; i++) {
    idx = (start_slot + i) % THREAD_COUNT;
    if (idx == THREAD_SLOT_IDLE) {
      continue;
    }
    if (&g_threads[idx] != g_current && g_threads[idx].state == THREAD_RUNNABLE) {
      return &g_threads[idx];
    }
  }

  if (g_current && (g_current->state == THREAD_RUNNING || g_current->state == THREAD_RUNNABLE)) {
    return g_current;
  }

  return &g_threads[THREAD_SLOT_IDLE];
}

static void thread_switch_to(struct thread *next) {
  struct thread *prev = g_current;

  if (!next || next == prev) {
    return;
  }

  if (prev && prev->state == THREAD_RUNNING) {
    prev->state = THREAD_RUNNABLE;
  }

  next->state = THREAD_RUNNING;
  g_current = next;

  if (!next->quantum_left) {
    next->quantum_left = THREAD_QUANTUM_TICKS;
  }

  if (prev) {
    context_switch(&prev->ctx, &next->ctx);
  } else {
    context_switch(&g_boot_ctx, &next->ctx);
  }
}

static void thread_bootstrap(void) {
  struct thread *t = g_current;
  void (*entry)(void *) = (void (*)(void *))(uintptr_t)t->ctx.x19;
  void *arg = (void *)(uintptr_t)t->ctx.x20;

  entry(arg);

  uart_puts("[thread] returned\n");
  for (;;) {
    g_resched_pending = 1;
    thread_resched_point();
  }
}

static void thread_entry_a(void *arg) {
  uint64_t next = thread_ticks_now() + THREAD_PRINT_PERIOD_TICKS;
  (void)arg;

  for (;;) {
    if (tick_reached(thread_ticks_now(), next)) {
      uart_puts("A\n");
      next += THREAD_PRINT_PERIOD_TICKS;
    }
    thread_resched_point();
  }
}

static void thread_entry_b(void *arg) {
  uint64_t next = thread_ticks_now() + THREAD_PRINT_PERIOD_TICKS;
  (void)arg;

  for (;;) {
    if (tick_reached(thread_ticks_now(), next)) {
      uart_puts("B\n");
      next += THREAD_PRINT_PERIOD_TICKS;
    }
    thread_resched_point();
  }
}

static void thread_entry_idle(void *arg) {
  (void)arg;
  for (;;) {
    asm volatile("wfi");
    thread_resched_point();
  }
}

void thread_system_init(void) {
  g_sched_ticks = 0;
  g_resched_pending = 0;
  g_current = 0;

  thread_init_slot(&g_threads[THREAD_SLOT_A],
                   THREAD_SLOT_A,
                   "thread_a",
                   thread_entry_a,
                   0,
                   g_stack_a,
                   THREAD_STACK_SIZE);
  thread_init_slot(&g_threads[THREAD_SLOT_B],
                   THREAD_SLOT_B,
                   "thread_b",
                   thread_entry_b,
                   0,
                   g_stack_b,
                   THREAD_STACK_SIZE);
  thread_init_slot(&g_threads[THREAD_SLOT_IDLE],
                   THREAD_SLOT_IDLE,
                   "idle",
                   thread_entry_idle,
                   0,
                   g_stack_idle,
                   THREAD_STACK_SIZE);
}

void thread_start(void) {
  thread_switch_to(&g_threads[THREAD_SLOT_A]);

  for (;;) {
    asm volatile("wfi");
  }
}

void thread_tick_irq(void) {
  g_sched_ticks++;

  if (!g_current) {
    return;
  }

  if (g_current->quantum_left > 0) {
    g_current->quantum_left--;
  }

  if (g_current->quantum_left == 0) {
    g_current->quantum_left = THREAD_QUANTUM_TICKS;
    g_resched_pending = 1;
  }
}

void thread_resched_point(void) {
  uint32_t pending;
  struct thread *next;

  /* Consume resched_pending atomically with IRQs masked in thread context. */
  arch_disable_irq();
  pending = g_resched_pending;
  g_resched_pending = 0;
  arch_enable_irq();

  if (!pending) {
    return;
  }

  next = pick_next_thread();
  thread_switch_to(next);
}

uint64_t thread_ticks_now(void) {
  return g_sched_ticks;
}
