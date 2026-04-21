#include "kernel/config.h"
#include "kernel/ipc.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"
#include "kernel/types.h"

#define EL0_TEXT __attribute__((section(".el0_sandbox.text"), used))
#define EL0_RODATA __attribute__((section(".el0_sandbox.rodata"), used))
#define EL0_DATA __attribute__((section(".el0_sandbox.data"), used))

#define BENCH_WRITE_PAYLOAD_SIZE 1U
#define BENCH_IPC_PAYLOAD_SIZE 1U

static const char g_task_a_msg[] EL0_RODATA = "A\n";
static const char g_uart_ready_msg[] EL0_RODATA = "[uart] ready\n";
static const char g_sup_ready_msg[] EL0_RODATA = "[sup] ready\n";
static const char g_sup_restarted_msg[] EL0_RODATA = "[sup] restarted uart\n";
static const uint8_t g_bench_write_payload[] EL0_RODATA = {0x00};
static const uint8_t g_bench_ipc_payload[] EL0_RODATA = {0x5a};
static const char g_bench_test_sys_write[] EL0_RODATA = "sys_write";
static const char g_bench_test_ipc_roundtrip[] EL0_RODATA = "ipc_roundtrip";
static const char g_bench_flavor_str[] EL0_RODATA = KERNEL_FLAVOR_STR;
static const char g_bench_mode_str[] EL0_RODATA = BENCH_MODE_STR;
static const char g_bench_meta_end_prefix[] EL0_RODATA = "BENCH_META schema=1 phase=end flavor=";
static const char g_bench_line_prefix[] EL0_RODATA = "BENCH test=";
static const char g_bench_recovery_prefix[] EL0_RODATA = "BENCH test=recovery_window flavor=";
static const char g_bench_mode_field[] EL0_RODATA = " mode=";
static const char g_bench_flavor_field[] EL0_RODATA = " flavor=";
static const char g_bench_iter_field[] EL0_RODATA = " iter=";
static const char g_bench_min_field[] EL0_RODATA = " min_cycles=";
static const char g_bench_median_field[] EL0_RODATA = " median_cycles=";
static const char g_bench_max_field[] EL0_RODATA = " max_cycles=";
static const char g_bench_total_field[] EL0_RODATA = " total_cycles=";
static const char g_bench_cycles_field[] EL0_RODATA = " cycles=";
static const char g_bench_cntfrq_field[] EL0_RODATA = " cntfrq_hz=";
static const char g_bench_iterations_field[] EL0_RODATA = " iterations=";
static const char g_bench_newline[] EL0_RODATA = "\n";
static int g_uart_server_crashed_once EL0_DATA = 0;

static inline uint64_t el0_syscall2(uint64_t nr, uint64_t arg0, uint64_t arg1) {
  register uint64_t x0 asm("x0") = arg0;
  register uint64_t x1 asm("x1") = arg1;
  register uint64_t x8 asm("x8") = nr;

  asm volatile("svc #0"
               : "+r"(x0), "+r"(x1), "+r"(x8)
               :
               : "x2", "x3", "x4", "x5", "x6", "x7",
                 "x9", "x10", "x11", "x12", "x13", "x14",
                 "x15", "x16", "x17", "x18", "cc", "memory");
  return x0;
}

static inline uint64_t el0_syscall3(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
  register uint64_t x0 asm("x0") = arg0;
  register uint64_t x1 asm("x1") = arg1;
  register uint64_t x2 asm("x2") = arg2;
  register uint64_t x8 asm("x8") = nr;

  asm volatile("svc #0"
               : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x8)
               :
               : "x3", "x4", "x5", "x6", "x7",
                 "x9", "x10", "x11", "x12", "x13", "x14",
                 "x15", "x16", "x17", "x18", "cc", "memory");
  return x0;
}

static inline uint64_t el0_write_ret(const char *buf, uint64_t len) {
  return el0_syscall2(SYS_write, (uint64_t)(uintptr_t)buf, len);
}

static inline void el0_write(const char *buf, uint64_t len) {
  (void)el0_write_ret(buf, len);
}

static inline void el0_yield(void) {
  (void)el0_syscall2(SYS_yield, 0, 0);
}

static inline uint64_t el0_ipc_recv(uint64_t ep_id, void *recv_ptr, uint64_t recv_cap) {
  return el0_syscall3(SYS_ipc_recv, ep_id, (uint64_t)(uintptr_t)recv_ptr, recv_cap);
}

static inline uint64_t el0_ipc_reply(uint64_t ep_id, const void *reply_ptr, uint64_t reply_len) {
  return el0_syscall3(SYS_ipc_reply, ep_id, (uint64_t)(uintptr_t)reply_ptr, reply_len);
}

static inline uint64_t el0_supervise_wait(void) {
  return el0_syscall2(SYS_supervise_wait, 0, 0);
}

static inline uint64_t el0_task_restart(uint64_t slot) {
  return el0_syscall2(SYS_task_restart, slot, 0);
}

#ifdef BENCH_MODE_OFF
static inline uint64_t el0_time_ticks(void) {
  return el0_syscall2(SYS_time_ticks, 0, 0);
}
#endif

#if !defined(BENCH_MODE_OFF)
static inline uint64_t el0_syscall5(uint64_t nr,
                                    uint64_t arg0,
                                    uint64_t arg1,
                                    uint64_t arg2,
                                    uint64_t arg3,
                                    uint64_t arg4) {
  register uint64_t x0 asm("x0") = arg0;
  register uint64_t x1 asm("x1") = arg1;
  register uint64_t x2 asm("x2") = arg2;
  register uint64_t x3 asm("x3") = arg3;
  register uint64_t x4 asm("x4") = arg4;
  register uint64_t x8 asm("x8") = nr;

  asm volatile("svc #0"
               : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x8)
               :
               : "x5", "x6", "x7",
                 "x9", "x10", "x11", "x12", "x13", "x14",
                 "x15", "x16", "x17", "x18", "cc", "memory");
  return x0;
}

static inline uint64_t el0_ipc_call(uint64_t ep_id,
                                    const void *send_ptr,
                                    uint64_t send_len,
                                    void *reply_ptr,
                                    uint64_t reply_cap) {
  return el0_syscall5(SYS_ipc_call,
                      ep_id,
                      (uint64_t)(uintptr_t)send_ptr,
                      send_len,
                      (uint64_t)(uintptr_t)reply_ptr,
                      reply_cap);
}

static inline uint64_t el0_read_cntvct(void) {
  uint64_t value;

  asm volatile("isb\n"
               "mrs %0, cntvct_el0\n"
               "isb"
               : "=r"(value)
               :
               : "memory");
  return value;
}

static inline uint64_t el0_read_cntfrq(void) {
  uint64_t value;

  asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}
#endif

#ifdef BENCH_MODE_OFF
static inline int tick_reached(uint64_t now, uint64_t target) {
  return (int64_t)(now - target) >= 0;
}
#endif

#if !defined(BENCH_MODE_OFF)
static uint64_t EL0_TEXT bench_cstr_len(const char *s) {
  uint64_t len = 0;

  while (s[len] != '\0') {
    len++;
  }
  return len;
}

static void EL0_TEXT bench_emit_str(const char *s) {
  el0_write(s, bench_cstr_len(s));
}

static void EL0_TEXT bench_emit_u64(uint64_t value) {
  char buf[21];
  uint64_t i = 0;

  if (value == 0) {
    buf[i++] = '0';
  } else {
    while (value > 0 && i < sizeof(buf)) {
      buf[i++] = (char)('0' + (value % 10));
      value /= 10;
    }
  }

  while (i > 0) {
    i--;
    el0_write(&buf[i], 1);
  }
}

static void EL0_TEXT bench_emit_meta_end(uint64_t cntfrq_hz) {
  bench_emit_str(g_bench_meta_end_prefix);
  bench_emit_str(g_bench_flavor_str);
  bench_emit_str(g_bench_mode_field);
  bench_emit_str(g_bench_mode_str);
  bench_emit_str(g_bench_cntfrq_field);
  bench_emit_u64(cntfrq_hz);
  bench_emit_str(g_bench_iterations_field);
  bench_emit_u64(BENCH_ITERATIONS);
  bench_emit_str(g_bench_newline);
}

static void EL0_TEXT __attribute__((unused)) bench_emit_latency_result(const char *test_name,
                                                                       uint64_t min_cycles,
                                                                       uint64_t median_cycles,
                                                                       uint64_t max_cycles,
                                                                       uint64_t total_cycles) {
  bench_emit_str(g_bench_line_prefix);
  bench_emit_str(test_name);
  bench_emit_str(g_bench_flavor_field);
  bench_emit_str(g_bench_flavor_str);
  bench_emit_str(g_bench_iter_field);
  bench_emit_u64(BENCH_ITERATIONS);
  bench_emit_str(g_bench_min_field);
  bench_emit_u64(min_cycles);
  bench_emit_str(g_bench_median_field);
  bench_emit_u64(median_cycles);
  bench_emit_str(g_bench_max_field);
  bench_emit_u64(max_cycles);
  bench_emit_str(g_bench_total_field);
  bench_emit_u64(total_cycles);
  bench_emit_str(g_bench_newline);
}

static void EL0_TEXT __attribute__((unused)) bench_emit_recovery_result(uint64_t cycles) {
  bench_emit_str(g_bench_recovery_prefix);
  bench_emit_str(g_bench_flavor_str);
  bench_emit_str(g_bench_iter_field);
  bench_emit_u64(1);
  bench_emit_str(g_bench_cycles_field);
  bench_emit_u64(cycles);
  bench_emit_str(g_bench_newline);
}

static void EL0_TEXT bench_settle(void) {
  uint64_t i;

  for (i = 0; i < 8U; i++) {
    el0_yield();
  }
}

static void EL0_TEXT __attribute__((unused)) bench_sort_u64(uint64_t *samples, uint64_t count) {
  uint64_t i;

  for (i = 1; i < count; i++) {
    uint64_t key = samples[i];
    uint64_t j = i;

    while (j > 0 && samples[j - 1] > key) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = key;
  }
}

static uint64_t EL0_TEXT __attribute__((unused)) bench_median_u64(uint64_t *samples, uint64_t count) {
  uint64_t mid;

  bench_sort_u64(samples, count);
  mid = count / 2U;
  if ((count & 1U) == 0U) {
    uint64_t lower = samples[mid - 1U];
    uint64_t upper = samples[mid];
    return lower + ((upper - lower) / 2U);
  }
  return samples[mid];
}

static void EL0_TEXT __attribute__((unused)) bench_collect_write_samples(uint64_t *samples,
                                                                         uint64_t *min_cycles,
                                                                         uint64_t *max_cycles,
                                                                         uint64_t *total_cycles) {
  uint64_t i;

  *min_cycles = (uint64_t)-1;
  *max_cycles = 0;
  *total_cycles = 0;

  for (i = 0; i < BENCH_ITERATIONS; i++) {
    uint64_t start = el0_read_cntvct();
    uint64_t retval = el0_write_ret((const char *)(uintptr_t)g_bench_write_payload,
                                    BENCH_WRITE_PAYLOAD_SIZE);
    uint64_t end = el0_read_cntvct();
    uint64_t elapsed = (retval == BENCH_WRITE_PAYLOAD_SIZE) ? (end - start) : 0;

    samples[i] = elapsed;
    *total_cycles += elapsed;
    if (elapsed < *min_cycles) {
      *min_cycles = elapsed;
    }
    if (elapsed > *max_cycles) {
      *max_cycles = elapsed;
    }
  }
}

static void EL0_TEXT __attribute__((unused)) bench_collect_ipc_samples(uint64_t *samples,
                                                                       uint8_t *reply_buf,
                                                                       uint64_t *min_cycles,
                                                                       uint64_t *max_cycles,
                                                                       uint64_t *total_cycles) {
  uint64_t i;

  *min_cycles = (uint64_t)-1;
  *max_cycles = 0;
  *total_cycles = 0;

  for (i = 0; i < BENCH_ITERATIONS; i++) {
    uint64_t start = el0_read_cntvct();
    uint64_t retval = el0_ipc_call(EP_BENCH,
                                   g_bench_ipc_payload,
                                   BENCH_IPC_PAYLOAD_SIZE,
                                   reply_buf,
                                   BENCH_IPC_PAYLOAD_SIZE);
    uint64_t end = el0_read_cntvct();
    uint64_t elapsed = (retval == BENCH_IPC_PAYLOAD_SIZE) ? (end - start) : 0;

    samples[i] = elapsed;
    *total_cycles += elapsed;
    if (elapsed < *min_cycles) {
      *min_cycles = elapsed;
    }
    if (elapsed > *max_cycles) {
      *max_cycles = elapsed;
    }
  }
}

#ifdef BENCH_MODE_LATENCY
static void EL0_TEXT bench_run_latency(void) __attribute__((noreturn));

static void EL0_TEXT bench_run_latency(void) {
  uint64_t cntfrq_hz = el0_read_cntfrq();
  uint64_t samples[BENCH_ITERATIONS];
  uint8_t reply_buf[BENCH_IPC_PAYLOAD_SIZE];
  uint64_t min_cycles;
  uint64_t max_cycles;
  uint64_t total_cycles;
  uint64_t median_cycles;

  bench_settle();
  (void)el0_write_ret((const char *)(uintptr_t)g_bench_write_payload, BENCH_WRITE_PAYLOAD_SIZE);
  (void)el0_ipc_call(EP_BENCH,
                     g_bench_ipc_payload,
                     BENCH_IPC_PAYLOAD_SIZE,
                     reply_buf,
                     BENCH_IPC_PAYLOAD_SIZE);

  bench_collect_write_samples(samples, &min_cycles, &max_cycles, &total_cycles);
  median_cycles = bench_median_u64(samples, BENCH_ITERATIONS);
  bench_emit_latency_result(g_bench_test_sys_write,
                            min_cycles,
                            median_cycles,
                            max_cycles,
                            total_cycles);

  bench_collect_ipc_samples(samples, reply_buf, &min_cycles, &max_cycles, &total_cycles);
  median_cycles = bench_median_u64(samples, BENCH_ITERATIONS);
  bench_emit_latency_result(g_bench_test_ipc_roundtrip,
                            min_cycles,
                            median_cycles,
                            max_cycles,
                            total_cycles);

  bench_emit_meta_end(cntfrq_hz);

  for (;;) {
    el0_yield();
  }
}
#endif

#ifdef BENCH_MODE_RECOVERY
static void EL0_TEXT bench_run_recovery(void) __attribute__((noreturn));

static void EL0_TEXT bench_run_recovery(void) {
  uint64_t cntfrq_hz = el0_read_cntfrq();
  uint64_t start;
  uint64_t end;
  uint64_t retval;
  uint64_t cycles;

  bench_settle();
  (void)el0_write_ret((const char *)(uintptr_t)g_bench_write_payload, BENCH_WRITE_PAYLOAD_SIZE);

  start = el0_read_cntvct();
  retval = el0_write_ret((const char *)(uintptr_t)g_bench_write_payload, BENCH_WRITE_PAYLOAD_SIZE);
  end = el0_read_cntvct();
  cycles = (retval == BENCH_WRITE_PAYLOAD_SIZE) ? (end - start) : 0;

  bench_emit_recovery_result(cycles);
  bench_emit_meta_end(cntfrq_hz);

  for (;;) {
    el0_yield();
  }
}
#endif
#endif

void __el0_task_a_entry(void) EL0_TEXT __attribute__((noreturn));
void __el0_task_b_entry(void) EL0_TEXT __attribute__((noreturn));
void __el0_task_c_entry(void) EL0_TEXT __attribute__((noreturn));

void __el0_task_a_entry(void) {
#ifdef BENCH_MODE_OFF
  uint64_t next = el0_time_ticks() + 200ULL;

  for (;;) {
    uint64_t now = el0_time_ticks();
    if (tick_reached(now, next)) {
      (void)el0_write_ret(g_task_a_msg, sizeof(g_task_a_msg) - 1U);
      next += 200ULL;
    }
    el0_yield();
  }
#elif defined(BENCH_MODE_LATENCY)
  bench_run_latency();
#elif defined(BENCH_MODE_RECOVERY)
  bench_run_recovery();
#else
  for (;;) {
    el0_yield();
  }
#endif
}

void __el0_task_b_entry(void) {
  uint8_t recv_buf[IPC_MSG_SIZE];

#ifdef BENCH_MODE_OFF
  (void)el0_write_ret(g_uart_ready_msg, sizeof(g_uart_ready_msg) - 1U);
#endif

  for (;;) {
    uint64_t recv_len = el0_ipc_recv(EP_UART, recv_buf, sizeof(recv_buf));
    if (recv_len != (uint64_t)-1) {
      (void)el0_write_ret((const char *)recv_buf, recv_len);
      (void)el0_ipc_reply(EP_UART, recv_buf, 0);
#ifndef BENCH_MODE_LATENCY
      if (!g_uart_server_crashed_once) {
        g_uart_server_crashed_once = 1;
        asm volatile("brk #0");
      }
#endif
    }
  }
}

void __el0_task_c_entry(void) {
#ifdef BENCH_MODE_LATENCY
  uint8_t recv_buf[IPC_MSG_SIZE];

  for (;;) {
    uint64_t recv_len = el0_ipc_recv(EP_BENCH, recv_buf, sizeof(recv_buf));
    if (recv_len != (uint64_t)-1) {
      (void)el0_ipc_reply(EP_BENCH, recv_buf, recv_len);
    }
  }
#else
#ifdef BENCH_MODE_OFF
  (void)el0_write_ret(g_sup_ready_msg, sizeof(g_sup_ready_msg) - 1U);
#endif

  for (;;) {
    uint64_t slot = el0_supervise_wait();
    if (slot == THREAD_SLOT_TASK_B) {
      if (el0_task_restart(THREAD_SLOT_TASK_B) == 0) {
#ifdef BENCH_MODE_OFF
        (void)el0_write_ret(g_sup_restarted_msg, sizeof(g_sup_restarted_msg) - 1U);
#endif
      }
    }
  }
#endif
}
