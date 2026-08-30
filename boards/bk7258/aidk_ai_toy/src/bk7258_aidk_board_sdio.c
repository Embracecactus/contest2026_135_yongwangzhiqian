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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_sdio.h>

#include <nuttx/signal.h>

#include <driver/gpio.h>

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_sdio_sel(int mode);
extern bk_err_t gpio_sdio_one_line_sel(int mode);
extern bk_err_t bk_pm_module_vote_ctrl_external_ldo(
  uint32_t module, gpio_id_t gpio_id, gpio_output_state_e value);

#define AIDK_SD_NAND_POWER_SETTLE_US 10000u

/* The SDK default GPIO table installs one-line SDIO map mode 0 during GPIO
 * initialization.  Selecting map mode 1 does not remove that old group, so
 * release it before binding the controller to the pins wired on this board.
 */

#define AIDK_SDK_DEFAULT_SDIO_CLK_GPIO 2
#define AIDK_SDK_DEFAULT_SDIO_CMD_GPIO 3
#define AIDK_SDK_DEFAULT_SDIO_D0_GPIO  4

#if BK7258_BOARD_HAS_SD_NAND != 1 || \
    BK7258_BOARD_PIN_LDO33_EN != 52
#  error "AIDK SD NAND power binding no longer matches the board"
#endif

#if BK7258_BOARD_HAS_DUAL_SPI_LCD != 1 || \
    BK7258_BOARD_LCD1_CLK_GPIO != AIDK_SDK_DEFAULT_SDIO_CLK_GPIO || \
    BK7258_BOARD_LCD1_CS_GPIO != AIDK_SDK_DEFAULT_SDIO_CMD_GPIO || \
    BK7258_BOARD_LCD1_DATA_GPIO != AIDK_SDK_DEFAULT_SDIO_D0_GPIO
#  error "AIDK SDK-default SDIO release no longer matches LCD1 wiring"
#endif

static bool g_bk7258_aidk_sdio_initialized;

static int bk7258_aidk_sdio_unmap_pin(gpio_id_t gpio_id)
{
  return gpio_dev_unmap(gpio_id) == BK_OK ? OK : -EIO;
}

static int bk7258_aidk_sdio_configure_pin(gpio_id_t gpio_id)
{
  if (bk_gpio_pull_up(gpio_id) != BK_OK ||
      bk_gpio_set_capacity(gpio_id, GPIO_DRIVER_CAPACITY_3) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

int bk7258_board_sdio_initialize(bool widebus)
{
  bk_err_t ret;

  if (g_bk7258_aidk_sdio_initialized)
    {
      return OK;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  /* R45 ties NAND_VDD to LDO_3V3.  This probe runs before NFC bring-up, so
   * SDIO must establish its own vote before the first CMD0/CMD1/CMD8.  The
   * SDK vote manager keeps P52 high when NFC later adds a second vote.
   */

  ret = bk_pm_module_vote_ctrl_external_ldo(
          GPIO_CTRL_LDO_MODULE_SDIO,
          (gpio_id_t)BK7258_BOARD_PIN_LDO33_EN,
          GPIO_OUTPUT_STATE_HIGH);
  if (ret != BK_OK)
    {
      syslog(LOG_ERR, "AIDK SD NAND LDO vote failed: %d\n", ret);
      return -EIO;
    }

  (void)nxsig_usleep(AIDK_SD_NAND_POWER_SETTLE_US);

  if (bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)AIDK_SDK_DEFAULT_SDIO_CLK_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)AIDK_SDK_DEFAULT_SDIO_CMD_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)AIDK_SDK_DEFAULT_SDIO_D0_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0 ||
      (widebus &&
       (bk7258_aidk_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO) < 0 ||
        bk7258_aidk_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO) < 0 ||
        bk7258_aidk_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO) < 0)))
    {
      return -EIO;
    }

  ret = widebus ? gpio_sdio_sel(BK7258_BOARD_SDIO_MAP_MODE) :
                  gpio_sdio_one_line_sel(BK7258_BOARD_SDIO_MAP_MODE);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  if (bk7258_aidk_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_aidk_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_aidk_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0)
    {
      return -EIO;
    }

  if (widebus &&
      (bk7258_aidk_sdio_configure_pin(
         (gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO) < 0 ||
       bk7258_aidk_sdio_configure_pin(
         (gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO) < 0 ||
       bk7258_aidk_sdio_configure_pin(
         (gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO) < 0))
    {
      return -EIO;
    }

  g_bk7258_aidk_sdio_initialized = true;
  return OK;
}

bool bk7258_board_sdio_card_present(void)
{
  /* SD NAND is soldered and always present. */

  return true;
}

#endif /* CONFIG_BK7258_SDIO */
