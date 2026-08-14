/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_t5_board_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 TF-card physical binding.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDIO

#include <errno.h>
#include <stdbool.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_sdio.h>

#include <driver/gpio.h>

#if BK7258_BOARD_SDIO_U3_FLASH_FITTED
#  error "T5-Board TF and optional U3 flash cannot share the SDIO/SFC pins"
#endif

/* These v3.1.1.9 functions are exported by libdriver.a and used by the
 * official SDIO driver, but its immutable wrapper bundle omits the private
 * gpio_driver.h declaration.  Keep the ABI declarations local to this
 * physical-board wrapper.
 */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_sdio_sel(int mode);
extern bk_err_t gpio_sdio_one_line_sel(int mode);

#define T5_BOARD_SDIO_PIN_GROUP0 0

static bool g_t5_board_sdio_initialized;

int bk7258_board_sdio_initialize(bool widebus)
{
  bk_err_t ret;

  if (g_t5_board_sdio_initialized)
    {
      return OK;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  /* Re-assert the profile-selected SDIO pin group here.  A four-bit
   * profile maps all four data pins even though the host initially starts
   * at one bit; NuttX switches the host only after the card accepts ACMD6.
   * The SDK archive was built with GPIO_DEFAULT_SET_SUPPORT, so
   * bk_sdio_host_init() deliberately skips its own pin-group setup and
   * assumes the board did it beforehand.
   */

  ret = widebus ? gpio_sdio_sel(T5_BOARD_SDIO_PIN_GROUP0) :
                  gpio_sdio_one_line_sel(T5_BOARD_SDIO_PIN_GROUP0);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  (void)bk_gpio_pull_up((gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO);
  (void)bk_gpio_pull_up((gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO);
  (void)bk_gpio_pull_up((gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO);
  (void)bk_gpio_set_capacity((gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO,
                             GPIO_DRIVER_CAPACITY_3);
  (void)bk_gpio_set_capacity((gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO,
                             GPIO_DRIVER_CAPACITY_3);
  (void)bk_gpio_set_capacity((gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO,
                             GPIO_DRIVER_CAPACITY_3);

  if (widebus)
    {
      (void)bk_gpio_pull_up((gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO);
      (void)bk_gpio_pull_up((gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO);
      (void)bk_gpio_pull_up((gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO);
      (void)bk_gpio_set_capacity((gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO,
                                 GPIO_DRIVER_CAPACITY_3);
      (void)bk_gpio_set_capacity((gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO,
                                 GPIO_DRIVER_CAPACITY_3);
      (void)bk_gpio_set_capacity((gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO,
                                 GPIO_DRIVER_CAPACITY_3);
    }

  /* P6 is the socket's mechanical CD switch.  R54 pulls SD_CD high while
   * the slot is empty and the socket contact pulls it low when a card is
   * inserted.  This matches v3.1.1.9 sd_card_get_insert_status(): a raw
   * high level is handled as "NO SDcard".
   */

  ret = gpio_dev_unmap((gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  if (ret != BK_OK)
    {
      return -EIO;
    }

  (void)bk_gpio_disable_output(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  (void)bk_gpio_enable_input(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  (void)bk_gpio_enable_pull(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  (void)bk_gpio_pull_up(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);

  g_t5_board_sdio_initialized = true;
  return OK;
}

bool bk7258_board_sdio_card_present(void)
{
  bool level;

  if (!g_t5_board_sdio_initialized)
    {
      return false;
    }

  level = bk_gpio_get_input(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  return BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW ? !level : level;
}

#endif /* CONFIG_BK7258_SDIO */
