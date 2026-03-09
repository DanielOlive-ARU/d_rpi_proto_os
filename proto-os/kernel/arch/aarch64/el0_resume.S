.section .text, "ax"
.align 4
.global el0_resume
.type el0_resume, %function

/* x0 = pointer to struct user_task_ctx */
el0_resume:
  /* Keep ELR/SPSR programming atomic against IRQ state clobbering. */
  msr daifset, #2
  mov x9, x0

  ldr x10, [x9, #248]
  msr sp_el0, x10
  ldr x10, [x9, #256]
  msr elr_el1, x10
  ldr x10, [x9, #264]
  msr spsr_el1, x10

  ldr x1, [x9, #8]
  ldr x2, [x9, #16]
  ldr x3, [x9, #24]
  ldr x4, [x9, #32]
  ldr x5, [x9, #40]
  ldr x6, [x9, #48]
  ldr x7, [x9, #56]
  ldr x8, [x9, #64]
  ldr x11, [x9, #88]
  ldr x12, [x9, #96]
  ldr x13, [x9, #104]
  ldr x14, [x9, #112]
  ldr x15, [x9, #120]
  ldr x16, [x9, #128]
  ldr x17, [x9, #136]
  ldr x18, [x9, #144]
  ldr x19, [x9, #152]
  ldr x20, [x9, #160]
  ldr x21, [x9, #168]
  ldr x22, [x9, #176]
  ldr x23, [x9, #184]
  ldr x24, [x9, #192]
  ldr x25, [x9, #200]
  ldr x26, [x9, #208]
  ldr x27, [x9, #216]
  ldr x28, [x9, #224]
  ldr x29, [x9, #232]
  ldr x30, [x9, #240]
  ldr x0, [x9, #0]
  ldr x10, [x9, #80]
  ldr x9, [x9, #72]

  isb
  eret

.size el0_resume, . - el0_resume
