ENTRY(_start)

SECTIONS
{
  . = 0x40000000;

  .text : {
    *(.text.boot)
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    __data_start = .;
    *(.data*)
    __data_end = .;
  }

  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }

  . = ALIGN(16);
  __stack_bottom = .;
  . += 0x10000;
  __stack_top = .;

  . = 0x40E00000;
  .el0_sandbox : {
    __el0_sandbox_start = .;
    KEEP(*(.el0_sandbox.text))
    KEEP(*(.el0_sandbox.rodata))
    KEEP(*(.el0_sandbox.data))
    KEEP(*(.el0_sandbox.bss))
    . = ALIGN(16);
    __el0_sandbox_end = .;
  }

  __el0_task_a_stack_bottom = 0x40FC0000;
  __el0_task_a_stack_top = 0x40FE0000;
  __el0_task_b_stack_bottom = 0x40FE0000;
  __el0_task_b_stack_top = 0x41000000;

}

ASSERT(__el0_sandbox_end <= 0x40E10000, "EL0 code exceeds 64KiB sandbox code region")
ASSERT(__el0_sandbox_end <= 0x41000000, "EL0 sandbox exceeds 2MiB block")
ASSERT(__el0_task_a_stack_bottom >= 0x40E00000, "task_a stack bottom outside sandbox")
ASSERT(__el0_task_a_stack_top <= 0x41000000, "task_a stack top outside sandbox")
ASSERT(__el0_task_b_stack_bottom >= 0x40E00000, "task_b stack bottom outside sandbox")
ASSERT(__el0_task_b_stack_top <= 0x41000000, "task_b stack top outside sandbox")
ASSERT(__el0_task_a_stack_bottom < __el0_task_a_stack_top, "task_a stack bounds invalid")
ASSERT(__el0_task_b_stack_bottom < __el0_task_b_stack_top, "task_b stack bounds invalid")
ASSERT(__el0_task_a_stack_top == __el0_task_b_stack_bottom, "EL0 task stacks not contiguous")
