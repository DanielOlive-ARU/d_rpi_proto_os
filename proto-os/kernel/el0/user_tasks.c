#include "kernel/ipc.h"
#include "kernel/syscall.h"
#include "kernel/types.h"

#define EL0_TEXT __attribute__((section(".el0_sandbox.text"), used))
#define EL0_RODATA __attribute__((section(".el0_sandbox.rodata"), used))

static const char g_task_a_msg[] EL0_RODATA = "A\n";
static const char g_uart_ready_msg[] EL0_RODATA = "[uart] ready\n";

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

static inline uint64_t el0_time_ticks(void) {
  return el0_syscall2(SYS_time_ticks, 0, 0);
}

static inline void el0_write(const char *buf, uint64_t len) {
  (void)el0_syscall2(SYS_write, (uint64_t)(uintptr_t)buf, len);
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

static inline int tick_reached(uint64_t now, uint64_t target) {
  return (int64_t)(now - target) >= 0;
}

void __el0_task_a_entry(void) EL0_TEXT __attribute__((noreturn));
void __el0_task_b_entry(void) EL0_TEXT __attribute__((noreturn));

void __el0_task_a_entry(void) {
  uint64_t next = el0_time_ticks() + 200ULL;

  for (;;) {
    uint64_t now = el0_time_ticks();
    if (tick_reached(now, next)) {
      (void)el0_write(g_task_a_msg, sizeof(g_task_a_msg) - 1);
      next += 200ULL;
    }
    el0_yield();
  }
}

void __el0_task_b_entry(void) {
  uint8_t recv_buf[IPC_MSG_SIZE];

  (void)el0_write(g_uart_ready_msg, sizeof(g_uart_ready_msg) - 1);

  for (;;) {
    uint64_t recv_len = el0_ipc_recv(EP_UART, recv_buf, sizeof(recv_buf));
    if (recv_len != (uint64_t)-1) {
      (void)el0_write((const char *)recv_buf, recv_len);
      (void)el0_ipc_reply(EP_UART, recv_buf, 0);
    }
    el0_yield();
  }
}
