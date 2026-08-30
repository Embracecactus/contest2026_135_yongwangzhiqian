/* SPDX-License-Identifier: Apache-2.0 */
/* Observable raw-flash backend for the BL2 flash-map host fixture. */
#ifndef BK7258_TESTS_MOCK_BOOT_FLASH_H
#define BK7258_TESTS_MOCK_BOOT_FLASH_H

#include <stddef.h>
#include <stdint.h>

void mock_boot_flash_reset(void);
uint8_t *mock_boot_flash_data(void);
unsigned int mock_boot_flash_read_calls(void);
unsigned int mock_boot_flash_erase_calls(void);
unsigned int mock_boot_flash_program_calls(void);

#endif /* BK7258_TESTS_MOCK_BOOT_FLASH_H */
