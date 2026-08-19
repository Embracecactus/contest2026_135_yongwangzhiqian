/*
 * mock_boot_jump.c - implementation of the host handoff-jump hook.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mock_boot_jump.h"

sigjmp_buf g_mock_boot_jump_env;
uint32_t g_mock_boot_jump_msp;
uint32_t g_mock_boot_jump_reset;

void mock_boot_jump(uint32_t msp, uint32_t reset)
{
  g_mock_boot_jump_msp = msp;
  g_mock_boot_jump_reset = reset;
  siglongjmp(g_mock_boot_jump_env, 1);
}