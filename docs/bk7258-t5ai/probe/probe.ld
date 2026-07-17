/*
 * probe.ld - linker script for the BK7258 (T5-AI, Cortex-M33) bare probe.
 *
 * The probe is linked at flash/XIP logical address 0x02010000, which is the
 * app Reset_Handler entry the bootloader jumps to after validating the app
 * magic. Burning the produced probe.bin (after CRC-expand) at physical flash
 * offset 0x11000 makes the CPU see it at logical 0x02010000.
 *
 * Memory map (reverse-engineered, shared with the future NuttX BSP):
 *   FLASH : 0x02010000 .. 0x0201FFFF  (64 KiB app window, XIP view)
 *   RAM   : 0x28000000 .. 0x2809FFFF  (640 KiB SRAM)
 *   MSP   : 0x2809FFFC  (top of usable SRAM, slot 0 of the vector table)
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x02010000, LENGTH = 0x10000
    RAM   (rwx) : ORIGIN = 0x28000000, LENGTH = 0xA0000
}

/* Initial stack pointer (top of SRAM). Also stored in vector table slot 0. */
_estack = 0x2809FFFC;

SECTIONS
{
    /*
     * Vector table FIRST, pinned at 0x02010000. The 66-entry table is 0x108
     * bytes; entry [64] (the app magic) therefore lands at file offset 0x100,
     * exactly where the bootloader looks for "BK7236\0\0".
     */
    . = ORIGIN(FLASH);
    .vectors : ALIGN(4)
    {
        KEEP(*(.vectors))
        . = ALIGN(4);
    } > FLASH

    /* Code + read-only data (strings live here, XIP-readable). */
    .text : ALIGN(4)
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Initialized data: VMA in RAM, LMA right after .text in FLASH. */
    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(.data*)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT> FLASH
    __data_load_start__ = LOADADDR(.data);

    /* Zero-initialized data (no image bytes). */
    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /*
     * Discard exception-index unwinder tables and build metadata so the
     * freestanding probe carries no stale sections.
     */
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.comment*)
    }
}
