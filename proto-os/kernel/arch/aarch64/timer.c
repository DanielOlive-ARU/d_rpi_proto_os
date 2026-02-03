#include "kernel/arch.h"
#include "kernel/config.h"
#include "kernel/printk.h"
#include "kernel/drivers.h"

static volatile uint64_t g_ticks = 0;
static uint64_t g_tick_interval = 0;

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

void timer_init(void) {
  uint64_t freq = read_cntfrq();
  g_tick_interval = freq / 1000;
  if (g_tick_interval == 0) {
    g_tick_interval = 1;
  }

  write_cntv_tval(g_tick_interval);
  write_cntv_ctl(1);
}

void timer_handle_irq(void) {
  g_ticks++;

  write_cntv_tval(g_tick_interval);

#if DEBUG_TICK
  if ((g_ticks % 1000) == 0) {
    uart_puts("[tick] ");
    printk_u64(g_ticks);
    uart_puts("\n");
  }
#endif
}
