/*
 * bootloader.ld - BK7258 Tier-1 bootloader link script.
 *
 * Logical layout (consumed by the CRC packer, bl_crc.bin):
 *   FLASH @ 0x02000000, slot size 0x10000 (logical).
 *   0x000..0x1FF : vector table (0x000..0x0FF, 0x108..0x1FF) + bl magic (0x100)
 *   0x200..      : Reset_Handler code + .rodata (incl. FAL partition table)
 * c_main uses no mutable global state, so .data/.bss are expected empty and
 * start.S does not need to zero .bss.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x02000000, LENGTH = 0x00010000
    RAM   (rwx) : ORIGIN = 0x28000000, LENGTH = 0x000A0000
}

SECTIONS
{
    .text 0x02000000 : {
        *(.text.startup)
        *(.text*)
    } > FLASH

    .rodata : {
        *(.rodata*)
    } > FLASH

    .data : {
        *(.data*)
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    } > RAM

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
    }
}
