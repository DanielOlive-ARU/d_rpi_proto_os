#include "kernel/config.h"
#include "kernel/ipc.h"
#include "kernel/thread.h"

#define IPC_SLOT_NONE (-1)

struct ipc_endpoint {
  uint32_t owner_slot;
  uint32_t has_pending;
  uint64_t pending_len;
  uint8_t pending_msg[IPC_MSG_SIZE];

  int32_t blocked_caller_slot;
  uintptr_t caller_reply_ptr;
  uint64_t caller_reply_cap;
  uint64_t caller_result_override;

  int32_t blocked_receiver_slot;
  uintptr_t receiver_buf_ptr;
  uint64_t receiver_buf_cap;
};

static struct ipc_endpoint g_endpoints[EP_COUNT];

static int endpoint_valid(uint64_t ep_id) {
  return ep_id > EP_NONE && ep_id < EP_COUNT;
}

static struct ipc_endpoint *get_endpoint(uint64_t ep_id) {
  if (!endpoint_valid(ep_id)) {
    return 0;
  }
  return &g_endpoints[ep_id];
}

static uint64_t min_u64(uint64_t a, uint64_t b) {
  return (a < b) ? a : b;
}

static void bytes_copy(uint8_t *dst, const uint8_t *src, uint64_t len) {
  uint64_t i;
  for (i = 0; i < len; i++) {
    dst[i] = src[i];
  }
}

static void endpoint_clear_pending(struct ipc_endpoint *ep) {
  ep->has_pending = 0;
  ep->pending_len = 0;
}

static void endpoint_clear_caller(struct ipc_endpoint *ep) {
  ep->blocked_caller_slot = IPC_SLOT_NONE;
  ep->caller_reply_ptr = 0;
  ep->caller_reply_cap = 0;
  ep->caller_result_override = 0;
}

static void endpoint_clear_receiver(struct ipc_endpoint *ep) {
  ep->blocked_receiver_slot = IPC_SLOT_NONE;
  ep->receiver_buf_ptr = 0;
  ep->receiver_buf_cap = 0;
}

/* Mirror syscall.c EL0 range checks locally for IPC buffers. */
static int el0_buf_range_ok(const void *buf, uint64_t len) {
  uintptr_t start;
  uintptr_t sandbox_end = EL0_SANDBOX_END;

  if (!buf) {
    return 0;
  }

  start = (uintptr_t)buf;
  if (len == 0) {
    return start >= EL0_SANDBOX_BASE && start < sandbox_end;
  }
  if (start < EL0_SANDBOX_BASE || start >= sandbox_end) {
    return 0;
  }
  if (len > (uint64_t)(sandbox_end - start)) {
    return 0;
  }
  return 1;
}

void ipc_init(void) {
  uint32_t i;
  uint32_t j;

  for (i = 0; i < EP_COUNT; i++) {
    g_endpoints[i].owner_slot = THREAD_SLOT_IDLE;
    g_endpoints[i].has_pending = 0;
    g_endpoints[i].pending_len = 0;
    for (j = 0; j < IPC_MSG_SIZE; j++) {
      g_endpoints[i].pending_msg[j] = 0;
    }
    g_endpoints[i].blocked_caller_slot = IPC_SLOT_NONE;
    g_endpoints[i].caller_reply_ptr = 0;
    g_endpoints[i].caller_reply_cap = 0;
    g_endpoints[i].caller_result_override = 0;
    g_endpoints[i].blocked_receiver_slot = IPC_SLOT_NONE;
    g_endpoints[i].receiver_buf_ptr = 0;
    g_endpoints[i].receiver_buf_cap = 0;
  }

  g_endpoints[EP_UART].owner_slot = THREAD_SLOT_TASK_B;
}

void ipc_handle_task_death(uint32_t slot) {
  uint32_t i;

  for (i = 0; i < EP_COUNT; i++) {
    struct ipc_endpoint *ep = &g_endpoints[i];

    if (ep->blocked_receiver_slot == (int32_t)slot) {
      endpoint_clear_receiver(ep);
    }

    if (ep->owner_slot == slot && ep->blocked_caller_slot != IPC_SLOT_NONE) {
      thread_user_wake_with_x0((uint32_t)ep->blocked_caller_slot, (uint64_t)-1);
      endpoint_clear_caller(ep);
      endpoint_clear_pending(ep);
      thread_request_resched();
    } else if (ep->blocked_caller_slot == (int32_t)slot) {
      endpoint_clear_caller(ep);
      endpoint_clear_pending(ep);
    }
  }
}

uint64_t ipc_syscall_call(struct trap_frame *tf) {
  struct ipc_endpoint *ep = get_endpoint(tf->x[0]);
  const uint8_t *send_ptr = (const uint8_t *)(uintptr_t)tf->x[1];
  uint64_t send_len = tf->x[2];
  uint8_t *reply_ptr = (uint8_t *)(uintptr_t)tf->x[3];
  uint64_t reply_cap = tf->x[4];
  uint32_t caller_slot;
  uint64_t delivered;

  if (!ep || send_len > IPC_MSG_SIZE) {
    return (uint64_t)-1;
  }
  if (!el0_buf_range_ok(send_ptr, send_len) || !el0_buf_range_ok(reply_ptr, reply_cap)) {
    return (uint64_t)-1;
  }

  if (ep->has_pending || ep->blocked_caller_slot != IPC_SLOT_NONE) {
    return (uint64_t)-1;
  }

  caller_slot = thread_current_user_slot();
  ep->blocked_caller_slot = (int32_t)caller_slot;
  ep->caller_reply_ptr = (uintptr_t)reply_ptr;
  ep->caller_reply_cap = reply_cap;
  ep->caller_result_override = 0;

  if (ep->blocked_receiver_slot != IPC_SLOT_NONE) {
    delivered = min_u64(send_len, ep->receiver_buf_cap);
    if (delivered > 0) {
      bytes_copy((uint8_t *)(uintptr_t)ep->receiver_buf_ptr, send_ptr, delivered);
    }

    thread_user_wake_with_x0((uint32_t)ep->blocked_receiver_slot, delivered);
    endpoint_clear_receiver(ep);
    thread_request_resched();
  } else {
    if (send_len > 0) {
      bytes_copy(ep->pending_msg, send_ptr, send_len);
    }
    ep->pending_len = send_len;
    ep->has_pending = 1;
  }

  thread_user_trap_redirect(tf, TASK_RETURN_IPC_BLOCK);
  return 0;
}

uint64_t ipc_syscall_recv(struct trap_frame *tf) {
  struct ipc_endpoint *ep = get_endpoint(tf->x[0]);
  uint8_t *recv_ptr = (uint8_t *)(uintptr_t)tf->x[1];
  uint64_t recv_cap = tf->x[2];
  uint32_t current_slot;
  uint64_t delivered;

  if (!ep) {
    return (uint64_t)-1;
  }
  if (!el0_buf_range_ok(recv_ptr, recv_cap)) {
    return (uint64_t)-1;
  }

  current_slot = thread_current_user_slot();
  if (ep->owner_slot != current_slot) {
    return (uint64_t)-1;
  }

  if (ep->has_pending) {
    delivered = min_u64(ep->pending_len, recv_cap);
    if (delivered > 0) {
      bytes_copy(recv_ptr, ep->pending_msg, delivered);
    }
    endpoint_clear_pending(ep);
    return delivered;
  }

  if (ep->blocked_receiver_slot != IPC_SLOT_NONE) {
    return (uint64_t)-1;
  }

  ep->blocked_receiver_slot = (int32_t)current_slot;
  ep->receiver_buf_ptr = (uintptr_t)recv_ptr;
  ep->receiver_buf_cap = recv_cap;

  thread_user_trap_redirect(tf, TASK_RETURN_IPC_BLOCK);
  return 0;
}

uint64_t ipc_syscall_reply(struct trap_frame *tf) {
  struct ipc_endpoint *ep = get_endpoint(tf->x[0]);
  const uint8_t *reply_ptr = (const uint8_t *)(uintptr_t)tf->x[1];
  uint64_t reply_len = tf->x[2];
  uint32_t current_slot;
  uint64_t copied;
  uint64_t wake_ret;

  if (!ep || reply_len > IPC_MSG_SIZE) {
    return (uint64_t)-1;
  }
  if (!el0_buf_range_ok(reply_ptr, reply_len)) {
    return (uint64_t)-1;
  }

  current_slot = thread_current_user_slot();
  if (ep->owner_slot != current_slot) {
    return (uint64_t)-1;
  }
  if (ep->blocked_caller_slot == IPC_SLOT_NONE) {
    return (uint64_t)-1;
  }

  copied = min_u64(reply_len, ep->caller_reply_cap);
  if (copied > 0) {
    bytes_copy((uint8_t *)(uintptr_t)ep->caller_reply_ptr, reply_ptr, copied);
  }

  wake_ret = copied;
  if (ep->caller_result_override != 0) {
    wake_ret = ep->caller_result_override;
  }

  thread_user_wake_with_x0((uint32_t)ep->blocked_caller_slot, wake_ret);
  endpoint_clear_caller(ep);
  thread_request_resched();
  return 0;
}

uint64_t ipc_route_uart_write(struct trap_frame *tf, const uint8_t *buf, uint64_t len) {
  struct ipc_endpoint *ep = get_endpoint(EP_UART);
  uint32_t caller_slot;
  uint64_t delivered;

  if (!ep || len > IPC_MSG_SIZE) {
    return (uint64_t)-1;
  }
  if (!el0_buf_range_ok(buf, len)) {
    return (uint64_t)-1;
  }
  if (ep->has_pending || ep->blocked_caller_slot != IPC_SLOT_NONE) {
    return (uint64_t)-1;
  }

  caller_slot = thread_current_user_slot();
  ep->blocked_caller_slot = (int32_t)caller_slot;
  ep->caller_reply_ptr = 0;
  ep->caller_reply_cap = 0;
  ep->caller_result_override = len;

  if (ep->blocked_receiver_slot != IPC_SLOT_NONE) {
    delivered = min_u64(len, ep->receiver_buf_cap);
    if (delivered > 0) {
      bytes_copy((uint8_t *)(uintptr_t)ep->receiver_buf_ptr, buf, delivered);
    }

    thread_user_wake_with_x0((uint32_t)ep->blocked_receiver_slot, delivered);
    endpoint_clear_receiver(ep);
    thread_request_resched();
  } else {
    if (len > 0) {
      bytes_copy(ep->pending_msg, buf, len);
    }
    ep->pending_len = len;
    ep->has_pending = 1;
  }

  thread_user_trap_redirect(tf, TASK_RETURN_IPC_BLOCK);
  return 0;
}
