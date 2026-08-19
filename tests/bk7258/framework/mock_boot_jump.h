/*
 * mock_boot_jump.h - host hook for the patched BL2 handoff jump.
 *
 * bk7258_bl2_mcuboot_boot.c finishes with a naked Cortex-M register-file
 * jump that an x86_64 compiler cannot assemble.  patch.py replaces the jump
 * body with mock_boot_jump(), which records (msp, reset) and longjmps back
 * into the test so the full remap/vector/disable sequence before the jump
 * can be asserted.  The call site is the last statement of the noreturn
 * function, so unwinding through it is safe.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BK7258_TESTS_MOCK_BOOT_JUMP_H
#define BK7258_TESTS_MOCK_BOOT_JUMP_H

#include <setjmp.h>
#include <stdint.h>

/* Test-side jump buffer; sigsetjmp(env, 1) around board_boot_image(). */
extern sigjmp_buf g_mock_boot_jump_env;

/* Values passed to the jump, and the 0xffffffff markers used in place of
 * the "invalid vector" infinite loop inside the secondary boot path. */
extern uint32_t g_mock_boot_jump_msp;
extern uint32_t g_mock_boot_jump_reset;

void mock_boot_jump(uint32_t msp, uint32_t reset);

#endif /* BK7258_TESTS_MOCK_BOOT_JUMP_H */