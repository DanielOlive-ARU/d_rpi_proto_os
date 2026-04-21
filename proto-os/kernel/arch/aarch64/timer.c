#include "kernel/arch.h"
#include "kernel/config.h"
#include "kernel/drivers.h"
#include "kernel/printk.h"
#include "kernel/thread.h"

static volatile uint64_t g_ticks = 0;
static uint64_t g_counter_freq = 0;
static uint64_t g_tick_interval = 0;

#define CNTKCTL_EL1_EL0VCTEN (1UL << 1)

static inline uint64_t read_cntfrq(void) {
  uint64_t v;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
  return v;
}

static inline void write_cntv_tval(uint64_t v) {
  asm volatile("msr cntv_tval_el0, %0" :: "r"(v));
}

static inline void write_cntv_ctl(uint64_t v) {
  asm volatile("msr cntv_ctl_el0, %0" :: "r"(v));
}

static inline uint64_t read_cntkctl_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, cntkctl_el1" : "=r"(v));
  return v;
}

static inline void write_cntkctl_el1(uint64_t v) {
  asm volatile("msr cntkctl_el1, %0" :: "r"(v));
}

void timer_init(void) {
  uint64_t freq = read_cntfrq();
  uint64_t cntkctl;

  g_counter_freq = freq;
  g_tick_interval = freq / 1000;
  if (g_tick_interval == 0) {
    g_tick_interval = 1;
  }

  cntkctl = read_cntkctl_el1();
  cntkctl |= CNTKCTL_EL1_EL0VCTEN;
  write_cntkctl_el1(cntkctl);
  isb();

  write_cntv_tval(g_tick_interval);
  write_cntv_ctl(1);
}

void timer_handle_irq(void) {
  g_ticks++;

  write_cntv_tval(g_tick_interval);
  thread_tick_irq();

#if DEBUG_TICK && defined(BENCH_MODE_OFF)
  if ((g_ticks % 1000) == 0) {
    uart_puts("[tick] ");
    printk_u64(g_ticks);
    uart_puts("\n");
  }
#endif
}

uint64_t timer_counter_freq_hz(void) {
  return g_counter_freq;
}
