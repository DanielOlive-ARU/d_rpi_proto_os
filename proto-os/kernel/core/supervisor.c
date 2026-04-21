#include "kernel/supervisor.h"

#include "kernel/thread.h"

#define SUPERVISOR_SLOT_NONE (-1)

static int32_t g_pending_death_slot = SUPERVISOR_SLOT_NONE;
static int32_t g_blocked_supervisor_slot = SUPERVISOR_SLOT_NONE;

void supervisor_init(void) {
  g_pending_death_slot = SUPERVISOR_SLOT_NONE;
  g_blocked_supervisor_slot = SUPERVISOR_SLOT_NONE;
}

void supervisor_note_task_death(uint32_t slot) {
  if (slot >= THREAD_COUNT) {
    return;
  }

  if ((int32_t)slot == g_blocked_supervisor_slot) {
    g_blocked_supervisor_slot = SUPERVISOR_SLOT_NONE;
  }

  if (g_blocked_supervisor_slot != SUPERVISOR_SLOT_NONE) {
    thread_user_wake_with_x0((uint32_t)g_blocked_supervisor_slot, slot);
    g_blocked_supervisor_slot = SUPERVISOR_SLOT_NONE;
    thread_request_resched();
    return;
  }

  g_pending_death_slot = (int32_t)slot;
}

uint64_t supervisor_syscall_wait(struct trap_frame *tf) {
  uint32_t current_slot = thread_current_user_slot();

  if (current_slot != THREAD_SLOT_TASK_C) {
    return (uint64_t)-1;
  }

  if (g_pending_death_slot != SUPERVISOR_SLOT_NONE) {
    uint64_t slot = (uint64_t)(uint32_t)g_pending_death_slot;
    g_pending_death_slot = SUPERVISOR_SLOT_NONE;
    return slot;
  }

  if (g_blocked_supervisor_slot != SUPERVISOR_SLOT_NONE) {
    return (uint64_t)-1;
  }

  g_blocked_supervisor_slot = (int32_t)current_slot;
  thread_user_trap_redirect(tf, TASK_RETURN_NOTIFY_BLOCK);
  return 0;
}

uint64_t supervisor_syscall_restart(uint64_t slot) {
  if (thread_current_user_slot() != THREAD_SLOT_TASK_C) {
    return (uint64_t)-1;
  }
  /* M10 scope: only task_b restart is permitted. */
  if (slot != THREAD_SLOT_TASK_B) {
    return (uint64_t)-1;
  }
  if (thread_user_restart((uint32_t)slot) != 0) {
    return (uint64_t)-1;
  }
  return 0;
}
