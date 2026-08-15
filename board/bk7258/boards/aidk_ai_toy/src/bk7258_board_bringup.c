/****************************************************************************
 * board/bk7258/boards/aidk_ai_toy/src/bk7258_board_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal board hook: UART0 is owned by the shared chip console path and no
 * unverified external device is registered here.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_board_binding.h>

/* AIDK is schematic-only and intentionally contributes no MIC, SDIO or audio
 * binding.  Keeping an explicit empty aggregate lets an accidentally
 * enabled lower half fail closed at runtime; the board Kconfig should also
 * reject those options before compilation.
 */

static const struct bk7258_board_binding_s g_bk7258_aidk_binding =
{
  .version = BK7258_BINDING_VERSION,
  .size = sizeof(struct bk7258_board_binding_s),
  .mic = NULL,
  .sdio = NULL,
  .audio = NULL,
  .early_initialize = bk7258_board_early_initialize,
  .devices_initialize = bk7258_board_devices_initialize,
};

const struct bk7258_board_binding_s *bk7258_board_get_binding(void)
{
  return &g_bk7258_aidk_binding;
}

int bk7258_board_early_initialize(void)
{
  return OK;
}

int bk7258_board_devices_initialize(void)
{
  return OK;
}
