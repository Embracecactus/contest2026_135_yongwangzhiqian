/* SPDX-License-Identifier: Apache-2.0 */
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

#include <bk7258_partitions.h>
#include "boot_bl2_contract.h"

ENTRY(Reset_Handler)
bk7258_bl1_bl2_copy_size = BK7258_BL2_COPY_SIZE;

MEMORY
{
#if BK7258_BL1_SIGNED
    FLASH (rx)  : ORIGIN = BK7258_ARTIFACT_BOOT_XIP_START,
                  LENGTH = BK7258_ARTIFACT_BOOT_LOGICAL_SIZE - 0x100
    TRUST (rx)  : ORIGIN = BK7258_ARTIFACT_BOOT_XIP_END - 0x100,
                  LENGTH = 0x100
#else
    FLASH (rx)  : ORIGIN = BK7258_ARTIFACT_BOOT_XIP_START,
                  LENGTH = BK7258_ARTIFACT_BOOT_LOGICAL_SIZE
#endif
    RAM   (rwx) : ORIGIN = 0x28000000, LENGTH = 0x000A0000
}

SECTIONS
{
    .text BK7258_ARTIFACT_BOOT_XIP_START : {
        *(.text.startup)
        *(.text*)
    } > FLASH

#if BK7258_BL1_SIGNED
    .bk7258.trust.bl1 : {
        KEEP(*(.rodata.bk7258_bl1_manifest_root_public_key))
        KEEP(*(.rodata.bk7258_beken_manifest_root_public_key_hash))
    } > TRUST
#endif

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
#if BK7258_BL1_SIGNED
    ASSERT(SIZEOF(.bk7258.trust.bl1) == 0x60,
           "bootloader trust identity must contain P-256 X||Y plus its hash")
    ASSERT((ADDR(.rodata) + SIZEOF(.rodata)) <=
           ORIGIN(TRUST), "bootloader overlaps the fixed trust identity")
#endif
}
