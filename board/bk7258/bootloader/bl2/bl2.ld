ENTRY(bk7258_bl2_reset)

MEMORY
{
  BL2_TEXT (rwx)  : ORIGIN = 0x28020000, LENGTH = 0x00002f00
  BL2_TRUST (rx)  : ORIGIN = 0x28022f00, LENGTH = 0x00000100
}

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > BL2_TEXT
  .text : { *(.text.start) *(.text*) } > BL2_TEXT
  .trust_identity :
  {
    KEEP(*(.rodata.ecdsa_pub_key))
    KEEP(*(.rodata.ecdsa_pub_key_len))
  } > BL2_TRUST
  .rodata : { *(.rodata*) } > BL2_TEXT
  .data : { *(.data*) } > BL2_TEXT
  .bss (NOLOAD) :
  {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    __bss_end__ = .;
  } > BL2_TEXT
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
  ASSERT(SIZEOF(.trust_identity) == 0x60,
         "BL2 trust identity must contain one P-256 SPKI and its length")
}
