/*
 * contest2026_135_yongwangzhiqian/board/bk7258/tests/qemu_mbox_proxy/link.ld
 *
 * Link layout for QEMU mps2-an521.  The "flash"/code region is the 4 MB boot
 * SSRAM at 0x10000000 (an alias of 0x0); on this board it is real RAM, so the
 * vector table, text, rodata, data and bss all live there and no .data copy
 * loop is required.  The two per-core stacks live in the SSE-200 SRAM at
 * 0x20000000 (128 KB), which both cores share; nothing else uses it.
 */

ENTRY(Reset_Handler)

MEMORY
{
  CODE (rwx) : ORIGIN = 0x10000000, LENGTH = 0x00400000
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 0x00020000
}

/* Two 64 KB per-core stacks, back-to-back at the top of SRAM. */
_stack_core0 = ORIGIN(RAM) + 0x00010000;
_stack_core1 = ORIGIN(RAM) + 0x00020000;

SECTIONS
{
  .text :
  {
    KEEP(*(.vector_table))
    *(.text*)
    *(.rodata*)
  } > CODE

  .data :
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > CODE

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > CODE
}
