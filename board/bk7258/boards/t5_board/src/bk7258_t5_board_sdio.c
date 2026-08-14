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

static int bk7258_t5_board_sdio_unmap_pin(gpio_id_t gpio_id)
{
  return gpio_dev_unmap(gpio_id) == BK_OK ? OK : -EIO;
}

static int bk7258_t5_board_sdio_configure_pin(gpio_id_t gpio_id)
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

  if (g_t5_board_sdio_initialized)
    {
      return OK;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  /* The pinned SDK's default GPIO table enables P2/P3/P4 as SDIO but P10
   * and P11 as UART0.  gpio_sdio_sel() silently ignores an individual
   * gpio_hal_func_map() failure when a pin is already owned, so explicitly
   * release every default-mapped pin before selecting the group.  This
   * matches the official/Tuya sdio_host_init_gpio() ordering and is
   * essential for four-bit D2/D3.  P5/D1 is absent from the pinned default
   * table and therefore starts unmapped; gpio_dev_unmap(P5) would itself
   * return BK_ERR_GPIO_INVALID_OPERATE.
   */

  if (bk7258_t5_board_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_t5_board_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_t5_board_sdio_unmap_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0 ||
      (widebus &&
       (bk7258_t5_board_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO) < 0 ||
        bk7258_t5_board_sdio_unmap_pin(
          (gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO) < 0)))
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

  if (bk7258_t5_board_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CLK_GPIO) < 0 ||
      bk7258_t5_board_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_CMD_GPIO) < 0 ||
      bk7258_t5_board_sdio_configure_pin(
        (gpio_id_t)BK7258_BOARD_SDIO_D0_GPIO) < 0)
    {
      return -EIO;
    }

  if (widebus)
    {
      if (bk7258_t5_board_sdio_configure_pin(
            (gpio_id_t)BK7258_BOARD_SDIO_D1_GPIO) < 0 ||
          bk7258_t5_board_sdio_configure_pin(
            (gpio_id_t)BK7258_BOARD_SDIO_D2_GPIO) < 0 ||
          bk7258_t5_board_sdio_configure_pin(
            (gpio_id_t)BK7258_BOARD_SDIO_D3_GPIO) < 0)
        {
          return -EIO;
        }
    }

  /* Only configure a card-detect GPIO when the physical board has a
   * verified insertion edge.  T5-Board V1.0.2 keeps P6 high with and
   * without media, so its board contract leaves the pin untouched and uses
   * NuttX's fixed-media probing model instead.
   */

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
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
#endif

  g_t5_board_sdio_initialized = true;
  return OK;
}

bool bk7258_board_sdio_card_present(void)
{
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  bool level;
#endif

  if (!g_t5_board_sdio_initialized)
    {
      return false;
    }

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  level = bk_gpio_get_input(
    (gpio_id_t)BK7258_BOARD_SDIO_CARD_DETECT_GPIO);
  return BK7258_BOARD_SDIO_CARD_DETECT_ACTIVE_LOW ? !level : level;
#else
  /* NuttX documents an always-present status for slots without reliable
   * insertion information.  The upper half probes once during slot setup;
   * the card must therefore be inserted before reset.
   */

  return true;
#endif
}

#endif /* CONFIG_BK7258_SDIO */
