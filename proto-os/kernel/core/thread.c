#include "kernel/arch.h"
#include "kernel/config.h"
#include "kernel/drivers.h"
#include "kernel/el0.h"
#include "kernel/ipc.h"
#include "kernel/panic.h"
#include "kernel/printk.h"
#include "kernel/supervisor.h"
#include "kernel/thread.h"

#define THREAD_STACK_SIZE 4096U

extern char __el0_task_a_stack_bottom;
extern char __el0_task_a_stack_top;
extern char __el0_task_b_stack_bottom;
extern char __el0_task_b_stack_top;
extern char __el0_task_c_stack_bottom;
extern char __el0_task_c_stack_top;
extern char __el0_sandbox_end;
extern void __el0_task_a_entry(void);
extern void __el0_task_b_entry(void);
extern void __el0_task_c_entry(void);

static struct thread g_threads[THREAD_COUNT];
static struct thread *g_current = 0;
static struct thread *g_current_task = 0;
static struct thread_ctx g_boot_ctx;

static volatile uint64_t g_sched_ticks = 0;
static volatile uint32_t g_resched_pending = 0;

__attribute__((aligned(16))) static uint8_t g_stack_task_a[THREAD_STACK_SIZE];
__attribute__((aligned(16))) static uint8_t g_stack_task_b[THREAD_STACK_SIZE];
__attribute__((aligned(16))) static uint8_t g_stack_task_c[THREAD_STACK_SIZE];
__attribute__((aligned(16))) static uint8_t g_stack_idle[THREAD_STACK_SIZE];

static __attribute__((noreturn)) void thread_entry_user_task(void *arg);
static void thread_entry_idle(void *arg);
static void thread_bootstrap(void);
extern void user_task_return_to_kernel(void);
__attribute__((noinline)) void user_task_return_step(void);

static inline uint64_t stack_top(uint8_t *stack, uint64_t size) {
  uintptr_t top = (uintptr_t)(stack + size);
  return (uint64_t)(top & ~((uintptr_t)0xFULL));
}

static int thread_is_selectable(const struct thread *t) {
  if (!t) {
    return 0;
  }
  if (t->state != THREAD_RUNNABLE && t->state != THREAD_RUNNING) {
    return 0;
  }
  if (t->kind == THREAD_USER_TASK &&
      (t->user.state == TASK_DEAD || t->user.state == TASK_BLOCKED)) {
    return 0;
  }
  return 1;
}

static void validate_el0_layout(void) {
  uintptr_t sandbox_end = (uintptr_t)&__el0_sandbox_end;
  uintptr_t task_a_entry = (uintptr_t)&__el0_task_a_entry;
  uintptr_t task_b_entry = (uintptr_t)&__el0_task_b_entry;
  uintptr_t task_c_entry = (uintptr_t)&__el0_task_c_entry;
  uintptr_t task_c_bottom = (uintptr_t)&__el0_task_c_stack_bottom;
  uintptr_t task_c_top = (uintptr_t)&__el0_task_c_stack_top;
  uintptr_t task_a_bottom = (uintptr_t)&__el0_task_a_stack_bottom;
  uintptr_t task_a_top = (uintptr_t)&__el0_task_a_stack_top;
  uintptr_t task_b_bottom = (uintptr_t)&__el0_task_b_stack_bottom;
  uintptr_t task_b_top = (uintptr_t)&__el0_task_b_stack_top;

  if (sandbox_end > EL0_CODE_END) {
    panic("EL0 code exceeds reserved region");
  }

  if (task_a_entry < EL0_SANDBOX_BASE || task_a_entry >= EL0_CODE_END) {
    panic("task_a entry outside EL0 code region");
  }

  if (task_b_entry < EL0_SANDBOX_BASE || task_b_entry >= EL0_CODE_END) {
    panic("task_b entry outside EL0 code region");
  }

  if (task_c_entry < EL0_SANDBOX_BASE || task_c_entry >= EL0_CODE_END) {
    panic("task_c entry outside EL0 code region");
  }

  if (task_c_bottom != EL0_TASK_C_STACK_BOTTOM || task_c_top != EL0_TASK_C_STACK_TOP ||
      task_a_bottom != EL0_TASK_A_STACK_BOTTOM || task_a_top != EL0_TASK_A_STACK_TOP ||
      task_b_bottom != EL0_TASK_B_STACK_BOTTOM || task_b_top != EL0_TASK_B_STACK_TOP) {
    panic("EL0 stack symbol mismatch");
  }
}

static void thread_init_slot(struct thread *t,
                             uint32_t slot,
                             enum thread_kind kind,
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
  t->kind = kind;
  t->quantum_left = THREAD_QUANTUM_TICKS;
  t->slot = slot;
  t->entry = entry;
  t->arg = arg;
  t->name = name;
  t->user.state = TASK_DEAD;
  t->user.return_reason = TASK_RETURN_NONE;
  t->user.user_entry = 0;
  t->user.user_sp = 0;
  t->user.ttbr0_future = 0;
  t->user.name = 0;
  t->user.started = 0;
}

static void user_task_init(struct thread *t,
                           const char *task_name,
                           uint64_t entry,
                           uint64_t user_sp) {
  uint32_t i;

  for (i = 0; i < 31; i++) {
    t->user.ctx.x[i] = 0;
  }
  t->user.ctx.sp_el0 = user_sp;
  t->user.ctx.elr = entry;
  t->user.ctx.spsr = SPSR_EL0T_MASKED;
  t->user.state = TASK_RUNNABLE;
  t->user.return_reason = TASK_RETURN_NONE;
  t->user.user_entry = entry;
  t->user.user_sp = user_sp;
  t->user.name = task_name;
  t->user.started = 0;
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
    if (&g_threads[idx] != g_current && thread_is_selectable(&g_threads[idx])) {
      return &g_threads[idx];
    }
  }

  if (thread_is_selectable(g_current) && g_current->slot != THREAD_SLOT_IDLE) {
    return g_current;
  }

  if (thread_is_selectable(&g_threads[THREAD_SLOT_IDLE])) {
    return &g_threads[THREAD_SLOT_IDLE];
  }

  panic("no selectable threads");
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

  if (next->ctx.lr == 0 || next->ctx.sp == 0) {
    panic("invalid next thread context");
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

static __attribute__((noreturn)) void thread_enter_el0(struct thread *t) {
  g_current_task = t;
  t->state = THREAD_RUNNING;
  t->user.state = TASK_RUNNING;
  el0_resume(&t->user.ctx);
  panic("el0_resume returned");
}

static __attribute__((noreturn)) void thread_entry_user_task(void *arg) {
  struct thread *t = (struct thread *)(uintptr_t)arg;

  if (!t || t->kind != THREAD_USER_TASK) {
    panic("invalid user task thread");
  }

  if (!t->user.started) {
    t->user.started = 1;
  }

  thread_enter_el0(t);
}

__attribute__((noinline)) void user_task_return_step(void) {
  struct thread *t = g_current_task;

  if (!t || t != g_current || t->kind != THREAD_USER_TASK) {
    panic("invalid current user task");
  }

  if (t->user.return_reason == TASK_RETURN_YIELD) {
    t->user.state = TASK_RUNNABLE;
    t->state = THREAD_RUNNABLE;
  } else if (t->user.return_reason == TASK_RETURN_IPC_BLOCK) {
    t->user.state = TASK_BLOCKED;
    t->state = THREAD_RUNNABLE;
  } else if (t->user.return_reason == TASK_RETURN_NOTIFY_BLOCK) {
    t->user.state = TASK_BLOCKED;
    t->state = THREAD_RUNNABLE;
  } else if (t->user.return_reason == TASK_RETURN_EXIT ||
             t->user.return_reason == TASK_RETURN_FAULT) {
    if (t->user.return_reason == TASK_RETURN_EXIT) {
      uart_puts("[task] exit ");
      if (t->user.name) {
        uart_puts(t->user.name);
      } else {
        uart_puts("unknown");
      }
      uart_puts("\n");
    }
    t->user.state = TASK_DEAD;
    t->state = THREAD_STOPPED;
    ipc_handle_task_death(t->slot);
    supervisor_note_task_death(t->slot);
  } else {
    panic("unknown user return reason");
  }

  g_resched_pending = 1;
  thread_resched_point();

  if (g_current != t || t->kind != THREAD_USER_TASK) {
    panic("resumed user continuation on non-user thread");
  }
  if (t->state == THREAD_STOPPED ||
      t->user.state == TASK_DEAD ||
      t->user.state == TASK_BLOCKED) {
    panic("dead user task resumed");
  }

  t->state = THREAD_RUNNING;
  t->user.state = TASK_RUNNING;
  t->user.return_reason = TASK_RETURN_NONE;
  g_current_task = t;
}

uint32_t thread_current_user_slot(void) {
  struct thread *t = g_current_task;

  if (!t || t != g_current || t->kind != THREAD_USER_TASK) {
    panic("invalid current user task");
  }
  return t->slot;
}

struct user_task_ctx *thread_current_user_ctx(void) {
  struct thread *t = g_current_task;
  uintptr_t ctx_addr;

  if (!t || t != g_current || t->kind != THREAD_USER_TASK) {
    panic("invalid current user task");
  }

  ctx_addr = (uintptr_t)&t->user.ctx;

  if (ctx_addr < KERNEL_LOAD_ADDR ||
      ctx_addr + sizeof(t->user.ctx) > (KERNEL_LOAD_ADDR + KERNEL_IDMAP_SIZE)) {
    panic("user context pointer outside kernel map");
  }
  if (t->user.ctx.elr < EL0_SANDBOX_BASE || t->user.ctx.elr >= EL0_SANDBOX_END) {
    panic("user resume ELR outside sandbox");
  }
  return &t->user.ctx;
}

int thread_user_restart(uint32_t slot) {
  struct thread *t;
  uint64_t entry;
  uint64_t user_sp;
  const char *name;

  if (slot >= THREAD_COUNT) {
    return -1;
  }

  t = &g_threads[slot];
  if (t->kind != THREAD_USER_TASK) {
    return -1;
  }
  if (t->user.state != TASK_DEAD || t->state != THREAD_STOPPED) {
    return -1;
  }

  entry = t->user.user_entry;
  user_sp = t->user.user_sp;
  name = t->user.name;
  if (!entry || !user_sp || !name) {
    return -1;
  }

  user_task_init(t, name, entry, user_sp);
  t->state = THREAD_RUNNABLE;
  thread_request_resched();
  return 0;
}

void thread_user_wake_with_x0(uint32_t slot, uint64_t retval) {
  struct thread *t;

  if (slot >= THREAD_COUNT) {
    panic("wake slot out of range");
  }

  t = &g_threads[slot];
  if (t->kind != THREAD_USER_TASK) {
    panic("wake target is not user task");
  }
  if (t->state == THREAD_STOPPED || t->user.state == TASK_DEAD) {
    panic("wake target is dead");
  }

  t->user.ctx.x[0] = retval;
  t->user.state = TASK_RUNNABLE;
  t->state = THREAD_RUNNABLE;
}

void thread_request_resched(void) {
  g_resched_pending = 1;
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
  g_current_task = 0;

  validate_el0_layout();

  thread_init_slot(&g_threads[THREAD_SLOT_TASK_A],
                   THREAD_SLOT_TASK_A,
                   THREAD_USER_TASK,
                   "task_a_thread",
                   thread_entry_user_task,
                   &g_threads[THREAD_SLOT_TASK_A],
                   g_stack_task_a,
                   THREAD_STACK_SIZE);
  user_task_init(&g_threads[THREAD_SLOT_TASK_A],
                 "task_a",
                 (uint64_t)(uintptr_t)&__el0_task_a_entry,
                 (uint64_t)(uintptr_t)&__el0_task_a_stack_top);

  thread_init_slot(&g_threads[THREAD_SLOT_TASK_B],
                   THREAD_SLOT_TASK_B,
                   THREAD_USER_TASK,
                   "task_b_thread",
                   thread_entry_user_task,
                   &g_threads[THREAD_SLOT_TASK_B],
                   g_stack_task_b,
                   THREAD_STACK_SIZE);
  user_task_init(&g_threads[THREAD_SLOT_TASK_B],
                 "task_b",
                 (uint64_t)(uintptr_t)&__el0_task_b_entry,
                 (uint64_t)(uintptr_t)&__el0_task_b_stack_top);

  thread_init_slot(&g_threads[THREAD_SLOT_TASK_C],
                   THREAD_SLOT_TASK_C,
                   THREAD_USER_TASK,
                   "task_c_thread",
                   thread_entry_user_task,
                   &g_threads[THREAD_SLOT_TASK_C],
                   g_stack_task_c,
                   THREAD_STACK_SIZE);
  user_task_init(&g_threads[THREAD_SLOT_TASK_C],
                 "task_c",
                 (uint64_t)(uintptr_t)&__el0_task_c_entry,
                 (uint64_t)(uintptr_t)&__el0_task_c_stack_top);

  thread_init_slot(&g_threads[THREAD_SLOT_IDLE],
                   THREAD_SLOT_IDLE,
                   THREAD_IDLE,
                   "idle",
                   thread_entry_idle,
                   0,
                   g_stack_idle,
                   THREAD_STACK_SIZE);
}

void thread_start(void) {
  thread_switch_to(&g_threads[THREAD_SLOT_TASK_A]);

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

void thread_user_trap_redirect(struct trap_frame *tf, enum task_return_reason reason) {
  struct thread *t = g_current_task;
  uint32_t i;

  if (!t || t != g_current || t->kind != THREAD_USER_TASK) {
    panic("EL0 trap redirect without user task");
  }

  if (t->ctx.lr == 0 || t->ctx.sp == 0) {
    panic("current user thread context invalid");
  }

  if (tf->elr < EL0_SANDBOX_BASE || tf->elr >= EL0_SANDBOX_END) {
    panic("EL0 trap ELR outside sandbox");
  }

  for (i = 0; i < 31; i++) {
    t->user.ctx.x[i] = tf->x[i];
  }
  t->user.ctx.sp_el0 = tf->sp;
  t->user.ctx.elr = tf->elr;
  t->user.ctx.spsr = tf->spsr;
  t->user.return_reason = reason;

  tf->elr = (uint64_t)(uintptr_t)user_task_return_to_kernel;
  tf->spsr = SPSR_EL1H_MASKED;
}

void thread_user_fault(struct trap_frame *tf) {
  struct thread *t = g_current_task;

  if (!t || t != g_current || t->kind != THREAD_USER_TASK) {
    panic("EL0 fault without user task");
  }

#ifdef BENCH_MODE_RECOVERY
  uart_puts("BENCH_META schema=1 phase=fault_injected flavor=" KERNEL_FLAVOR_STR
            " mode=" BENCH_MODE_STR " task=");
  if (t->user.name) {
    uart_puts(t->user.name);
  } else {
    uart_puts("unknown");
  }
  uart_puts(" esr=0x");
  printk_hex_u64(tf->esr);
  uart_puts(" elr=0x");
  printk_hex_u64(tf->elr);
  uart_puts(" far=0x");
  printk_hex_u64(tf->far);
  uart_puts("\n");
#else
  uart_puts("[fault] ");
  if (t->user.name) {
    uart_puts(t->user.name);
  } else {
    uart_puts("unknown");
  }
  uart_puts(" dead esr=0x");
  printk_hex_u64(tf->esr);
  uart_puts(" elr=0x");
  printk_hex_u64(tf->elr);
  uart_puts(" far=0x");
  printk_hex_u64(tf->far);
  uart_puts("\n");
#endif

  thread_user_trap_redirect(tf, TASK_RETURN_FAULT);
}

uint64_t thread_ticks_now(void) {
  return g_sched_ticks;
}
