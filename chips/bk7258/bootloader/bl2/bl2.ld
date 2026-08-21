ENTRY(bk7258_bl2_reset)

#include "../boot_bl2_contract.h"

bk7258_bl2_load_address = BK7258_BL2_SRAM_BASE;

MEMORY
{
  BL2_RAM (rwx) : ORIGIN = BK7258_BL2_SRAM_BASE,
                  LENGTH = BK7258_BL2_SRAM_CAPACITY
}

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > BL2_RAM
  .text : { *(.text.start) *(.text*) } > BL2_RAM
  .bk7258.trust.mcuboot :
  {
    KEEP(*(.rodata.ecdsa_pub_key))
    KEEP(*(.rodata.ecdsa_pub_key_len))
  } > BL2_RAM
  .rodata : { *(.rodata*) } > BL2_RAM
  .data : { *(.data*) } > BL2_RAM
  .bss (NOLOAD) :
  {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    __bss_end__ = .;
  } > BL2_RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
  ASSERT(SIZEOF(.bk7258.trust.mcuboot) == 0x60,
         "BL2 trust identity must contain one P-256 SPKI and its length")
}
