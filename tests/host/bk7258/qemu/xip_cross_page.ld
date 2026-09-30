/* SPDX-License-Identifier: Apache-2.0 */
/* Force one Thumb-2 instruction to span the 4-KiB MMIO page boundary. */
ENTRY(start)
SECTIONS {
 .vectors 0x02010000 : { *(.vectors) }
 .fault 0x02010040 : { *(.fault) }
 .text 0x02010ffe : SUBALIGN(2) { *(.text*) }
}
