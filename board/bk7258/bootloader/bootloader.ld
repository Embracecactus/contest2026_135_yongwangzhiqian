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

#include "../chip/include/bk7258_partition_layout.h"
#include "boot_bl2_contract.h"

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = BK7258_ROLE_BOOT_XIP_START,
                  LENGTH = BK7258_ROLE_BOOT_LOGICAL_SIZE
    RAM   (rwx) : ORIGIN = 0x28000000, LENGTH = 0x000A0000
}

SECTIONS
{
    .text BK7258_ROLE_BOOT_XIP_START : {
        *(.text.startup)
        *(.text*)
    } > FLASH

    .rodata : {
        *(.rodata*)

        /* Keep cryptographic constants in XIP Flash so BL1 has no initialized
         * RAM requirement. */
        *(.data.g_curve_p)
        *(.data.g_curve_b)
        *(.data.g_curve_n)
        *(.data.g_curve_g)
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

    ASSERT(SIZEOF(.data) == 0, "bootloader must not require initialized RAM data")
    ASSERT(SIZEOF(.bss) == 0, "bootloader must not retain hidden BSS state")
    ASSERT((ADDR(.rodata) + SIZEOF(.rodata)) <=
           (BK7258_ROLE_BOOT_XIP_END - BK7258_BL1_MANIFEST_TAIL_SIZE),
           "bootloader overlaps the reserved BL1 Manifest tail")
}
