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

        /* NuttX crypto/ecc.c declares its four P-256 domain constants
         * without const, although the verifier only reads them.  Keep these
         * exact, source-verified tables in XIP Flash so Tier-1 still has no
         * initialized RAM requirement.  No NuttX source is changed.
         */
        *(.data.g_curve_p)
        *(.data.g_curve_b)
        *(.data.g_curve_n)
        *(.data.g_curve_g)
    } > FLASH

    /* The only raw program primitive executes from SRAM while the on-chip
     * Flash controller is busy.  Its load image remains inside the 64 KiB
     * boot slot and is copied only after every N15-D gate has passed.
     */
    .boot_ota_ramfunc 0x2800C000 : {
        __boot_ota_ramfunc_start = .;
        KEEP(*(.boot_ota_ramfunc.text))
        KEEP(*(.boot_ota_ramfunc.text.*))
        KEEP(*(.boot_ota_ramfunc.rodata))
        KEEP(*(.boot_ota_ramfunc.rodata.*))
        __boot_ota_ramfunc_end = .;
    } > RAM AT > FLASH
    __boot_ota_ramfunc_load_start = LOADADDR(.boot_ota_ramfunc);

    /* N15-C needs the 4 KiB metadata snapshot and 8 KiB validation scratch
     * simultaneously.  Keep this boot-only workspace below the CP app SRAM
     * window and restore it to zero before handoff.
     */
    .boot_ota_workspace 0x2800D000 (NOLOAD) : {
        KEEP(*(.boot_ota_workspace))
        KEEP(*(.boot_ota_workspace.*))
    } > RAM

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

    ASSERT(SIZEOF(.boot_ota_workspace) <= 0x3000,
           "N15 boot OTA workspace exceeds 12 KiB")
    ASSERT((ADDR(.boot_ota_workspace) + SIZEOF(.boot_ota_workspace)) <= 0x28010000,
           "N15 boot OTA workspace overlaps CP application SRAM")
    ASSERT(SIZEOF(.boot_ota_ramfunc) <= 0x1000,
           "N15 boot OTA SRAM writer exceeds 4 KiB")
    ASSERT((ADDR(.boot_ota_ramfunc) + SIZEOF(.boot_ota_ramfunc)) <= 0x2800D000,
           "N15 boot OTA SRAM writer overlaps validation workspace")
    ASSERT(SIZEOF(.data) == 0, "bootloader must not require initialized RAM data")
    ASSERT(SIZEOF(.bss) == 0, "bootloader must not retain hidden BSS state")
    ASSERT((LOADADDR(.boot_ota_ramfunc) + SIZEOF(.boot_ota_ramfunc)) <=
           BK7258_ROLE_BOOT_XIP_END,
           "bootloader exceeds its 64 KiB logical slot")
}
