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

}
