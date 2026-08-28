/* SPDX-License-Identifier: Apache-2.0 */
/* boot_runtime.h - minimal host-side declarations for boot_runtime.c. */
#ifndef BK7258_TESTS_BOOT_RUNTIME_H
#define BK7258_TESTS_BOOT_RUNTIME_H

void boot_flash_reset_prepare(void);
void boot_reset_prepare(void);
void boot_prepare_app_handoff(void);
void boot_console_prepare_app_handoff(void);

#endif /* BK7258_TESTS_BOOT_RUNTIME_H */
