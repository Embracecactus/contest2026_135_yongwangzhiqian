/* SPDX-License-Identifier: Apache-2.0 */
/* boot_libc.h - minimal host-side declarations for boot_libc.c. */
#ifndef BK7258_TESTS_BOOT_LIBC_H
#define BK7258_TESTS_BOOT_LIBC_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t len);
void *memset(void *destination, int value, size_t len);
int memcmp(const void *left, const void *right, size_t len);
int strcmp(const char *left, const char *right);

#endif /* BK7258_TESTS_BOOT_LIBC_H */
