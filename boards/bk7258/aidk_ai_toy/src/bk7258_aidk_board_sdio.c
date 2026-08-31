/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_board_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy 1GB SD NAND physical binding (SDIO map mode 1, P14-P19).
 * NAND_VDD shares the P52-controlled LDO_3V3 rail with NFC.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDIO

#include <stdbool.h>
#include <syslog.h>

#include <nuttx/signal.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_pinmux.h>
#include <arch/chip/bk7258_sdio.h>

#define AIDK_SD_NAND_POWER_SETTLE_US 10000u

#if BK7258_BOARD_HAS_SD_NAND != 1 || BK7258_BOARD_PIN_LDO33_EN != 52
#  error "AIDK SD NAND power binding no longer matches the board"
#endif

static bool g_bk7258_aidk_sdio_initialized;

const struct bk7258_sdio_pin_config_s g_bk7258_board_sdio_pins =
{
  .map_mode = BK7258_BOARD_SDIO_MAP_MODE,
  .clk_pin = BK7258_BOARD_SDIO_CLK_GPIO,
  .cmd_pin = BK7258_BOARD_SDIO_CMD_GPIO,
  .data_pin =
  {
    BK7258_BOARD_SDIO_D0_GPIO,
    BK7258_BOARD_SDIO_D1_GPIO,
    BK7258_BOARD_SDIO_D2_GPIO,
    BK7258_BOARD_SDIO_D3_GPIO,
  },
};

int bk7258_board_sdio_prepare(bool widebus)
{
  int ret;

  (void)widebus;
  if (g_bk7258_aidk_sdio_initialized)
    {
      return OK;
    }

  /* R45 ties NAND_VDD to LDO_3V3.  SDIO establishes its own vote before the
   * first protocol command; NFC later acquires an independent vote.
   */

  ret = bk7258_shared_rail_vote(BK7258_SHARED_RAIL_SDIO,
                                 BK7258_BOARD_PIN_LDO33_EN, true);
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK SD NAND LDO vote failed: %d\n", ret);
      return ret;
    }

  (void)nxsig_usleep(AIDK_SD_NAND_POWER_SETTLE_US);
  g_bk7258_aidk_sdio_initialized = true;
  return OK;
}

bool bk7258_board_sdio_card_present(void)
{
  /* SD NAND is soldered and always present. */

  return true;
}

#endif /* CONFIG_BK7258_SDIO */
