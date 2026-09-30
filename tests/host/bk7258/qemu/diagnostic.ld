/* SPDX-License-Identifier: Apache-2.0 */
ENTRY(cp_vectors)
MEMORY
{
  FLASH (rx) : ORIGIN = 0x02010000, LENGTH = 0x10000
  SRAM (rw) : ORIGIN = 0x28010000, LENGTH = 0x1000
}
SECTIONS
{
  .vectors.cp 0x02010000 : { KEEP(*(.vectors.cp)) } > FLASH
  .vectors.ap1 0x02010400 : { KEEP(*(.vectors.ap1)) } > FLASH
  .vectors.ap2 0x02010800 : { KEEP(*(.vectors.ap2)) } > FLASH
  .text : { *(.text*) *(.rodata*) } > FLASH
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > SRAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) }
}
