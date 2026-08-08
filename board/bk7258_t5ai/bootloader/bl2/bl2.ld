ENTRY(bk7258_bl2_reset)

MEMORY
{
  BL2_TEXT (rwx) : ORIGIN = 0x28020000, LENGTH = 0x00020000
}

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > BL2_TEXT
  .text : { *(.text.start) *(.text*) } > BL2_TEXT
  .rodata : { *(.rodata*) } > BL2_TEXT
  .data : { *(.data*) } > BL2_TEXT
  .bss (NOLOAD) :
  {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    __bss_end__ = .;
  } > BL2_TEXT
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
