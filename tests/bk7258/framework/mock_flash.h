/*
 * mock_flash.h - host-side XIP flash window at the BK7258 XIP base.
 *
 * Modules that dereference XIP addresses (flash_area_read, BL2 vector
 * reads) read real host memory backed by an anonymous mmap placed at
 * BK7258_FLASH_XIP_BASE (0x02000000).  Tests prefill the window and treat
 * anything past the image as erased (0xff) where the board contract says so.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BK7258_TESTS_MOCK_FLASH_H
#define BK7258_TESTS_MOCK_FLASH_H

#include <stddef.h>
#include <stdint.h>

/* Base and size of the BK7258 flash XIP window. */
#define BK7258_HOST_FLASH_XIP_BASE 0x02000000u
#define BK7258_HOST_FLASH_SIZE     0x00800000u

/* The BK7258 flash controller's FLASH_DATA_FLASH_TO_SW window auto-increments
 * on every read, so boot_flash.c pulls a 32-byte granule with 8 reads of the
 * same address.  The patched source reads through mock_flash_fifo_ref(), a
 * rolling word pointer into a 32-byte FIFO seeded by the test. */
uint32_t *mock_flash_fifo_ref(void);
void mock_flash_fifo_seed(const uint8_t data[32]);

/* Map the XIP window, zero-fill it, and return the base pointer.
 * Returns NULL when the window cannot be mapped (test should skip). */
void *mock_flash_map(void);

/* Fill the window [offset, offset+len) with 0xff (erased flash). */
void mock_flash_erase_fill(uint32_t offset, uint32_t len);

/* Unmap the window. */
void mock_flash_unmap(void);

#endif /* BK7258_TESTS_MOCK_FLASH_H */