/****************************************************************************
 * board/bk7258/boards/aidk_ai_toy/src/bk7258_aidk_board_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK AI Toy 1GB SD NAND physical binding (SDIO map mode 1, P14-P19).
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDIO

#include <errno.h>
#include <stdbool.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_sdio.h>

#include <driver/gpio.h>

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_sdio_sel(int mode);
extern bk_err_t gpio_sdio_one_line_sel(int mode);

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

  if (bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0 ||
      bk7258_aidk_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO) < 0 ||
      (widebus &&
       (bk7258_aidk_sdio_unmap_pin(
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
