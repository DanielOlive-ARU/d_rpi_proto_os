.section .text, "ax"
.align 4
.global user_task_return_to_kernel
.type user_task_return_to_kernel, %function

.extern user_task_return_step
.extern thread_current_user_ctx
.extern el0_resume

user_task_return_to_kernel:
  bl user_task_return_step
  bl thread_current_user_ctx
  b el0_resume

.size user_task_return_to_kernel, . - user_task_return_to_kernel
