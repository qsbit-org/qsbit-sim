OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0;
  .text : { *(.text*) }
  . = ALIGN(0x1000);
  .data : { *(.data*) *(.rodata*) }
  .bss : { *(.bss*) *(COMMON) }
}
