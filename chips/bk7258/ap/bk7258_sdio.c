/****************************************************************************
 * chips/bk7258/ap/bk7258_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SDIO host controller — NuttX sdio_dev_s lower-half over the
 * official Beken bk_sdio_host_* SDK API.
 *
 * Role ownership: AP only.  The 24 bk_sdio_host_* symbols are compiled into
 * the AP libdriver.a exclusively; the CP bundle ships the headers but
 * defines none of the symbols, so this file is guarded by CONFIG_BK7258_AP_CORE.
 *
 * Data path:
 *
 *   NuttX MMCSD/SDIO upper half (sdio_dev_s ops):
 *     sendcmd(cmd,arg)  -> bk_sdio_host_send_command + SDK queue completion
 *     recv_r1..r7()     -> bk_sdio_host_get_cmd_rsp_argument
 *     widebus(enable)   -> re-init with 1/4 line
 *     clock(rate)       -> bk_sdio_host_set_clock_freq
 *     recvsetup(buf,len)-> cache the NuttX transfer contract
 *     data sendcmd()     -> bk_sdio_host_config_data(RD) + command +
 *                           bk_sdio_host_wait_receive_data + FIFO drain
 *     sendsetup(buf,len)-> bk_sdio_host_config_data(WR) + write_fifo
 *
 * Notes on SDK behaviour that shaped this driver (verified against the
 * v3.1.1.9 headers, not assumed):
 *
 *  1. bk_sdio_host_driver_init() is a global one-time resource init;
 *     bk_sdio_host_init(config) powers up the unit.  The selected v3.1.1.9
 *     bundle has CONFIG_GPIO_DEFAULT_SET_SUPPORT, so that call assumes the
 *     SDK application's default GPIO table rather than selecting a route.
 *     NuttX must therefore restore the generated board route after every
 *     host init, before the first command.  We call driver_init once and
 *     host_init once at bk7258_sdio_initialize(), then re-init only on
 *     reset()/widebus().
 *  2. The Beken SDIO host API is blocking.  NuttX, however, calls
 *     recvsetup() before it sends the read command and may insert CMD55 before
 *     an application data command.  recvsetup() therefore records only the
 *     transfer contract; sendcmd() arms the SDK data path immediately before
 *     the matching command, then drains the FIFO after command completion.
 *     Write setup occurs after the command and can finish synchronously in
 *     sendsetup().  eventwait() then replays only the resulting
 *     completion/error bits.
 *  3. Command response type maps from the NuttX 32-bit cmd field: no-response
 *     -> SDIO_HOST_CMD_RSP_NONE; R2 (128-bit) -> SDIO_HOST_CMD_RSP_LONG;
 *     R1/R3/R4/R5/R6/R7 (48-bit) -> SDIO_HOST_CMD_RSP_SHORT.  CRC is checked
 *     for R1/R2/R6/R7 (the response types the spec CRC-protects); R3/R4/R5
 *     have no CRC and are sent without crc_check.
 *  4. The SDK ISR remains the sole owner of command and data status.  Its
 *     public queue wait has a four-millisecond slice, so the lower half waits
 *     repeatedly for the same in-flight command up to the NuttX deadline; it
 *     never masks the SDK IRQ or reads/acknowledges controller status itself.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDIO

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/sdio.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include "bk7258_sdk_abi.h"

#include <arch/chip/bk7258_pinmux.h>
#include <arch/chip/bk7258_sdio.h>

/* SDK API headers (Beken).  bk_err_t / BK_OK come via common/bk_err.h. */

#include <driver/sdio_host.h>
#include <driver/sdio_host_types.h>
#include <driver/gpio.h>

#if defined(CONFIG_BK7258_SDIO_4BIT) && \
    !defined(CONFIG_SDCARD_BUSWIDTH_4LINE)
#  error "Selected AP SDK bundle cannot preserve four-bit SDIO data setup"
#elif !defined(CONFIG_BK7258_SDIO_4BIT) && \
      defined(CONFIG_SDCARD_BUSWIDTH_4LINE)
#  error "Four-bit-only AP SDK bundle cannot serve a one-bit SDIO profile"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_SDIO_V2P0
#  define BK7258_SDIO_ID_CMD_TIMEOUT   2500u
#  define BK7258_SDIO_ID_DATA_TIMEOUT  10000u
#  define BK7258_SDIO_XFR_CMD_TIMEOUT  20000u
#  define BK7258_SDIO_XFR_DATA_TIMEOUT 640000u
#else
#  define BK7258_SDIO_ID_CMD_TIMEOUT   5000u
#  define BK7258_SDIO_ID_DATA_TIMEOUT  20000u
#  define BK7258_SDIO_XFR_CMD_TIMEOUT  1200000u
#  define BK7258_SDIO_XFR_DATA_TIMEOUT 12000000u
#endif

/* Response type bits in the NuttX 32-bit command field (see nuttx/sdio.h). */

#define SDIO_NUTTX_RSP_SHIFT           6

#define BK7258_SDIO_CMD_WAIT_MS        100u
#define SDIO_NUTTX_RSP_MASK            (15 << SDIO_NUTTX_RSP_SHIFT)
#define BK7258_SDIO_MAP_COUNT           2u
#define BK7258_SDIO_PIN_COUNT           6u
#define BK7258_SDIO_SDK_DEFAULT_COUNT   3u

#ifdef CONFIG_DEBUG_FS_INFO
#  define BKSDIO_TRACE(fmt, ...) \
     syslog(LOG_INFO, "BKSDIO TRACE " fmt "\n", ##__VA_ARGS__)
#else
#  define BKSDIO_TRACE(fmt, ...) do { } while (0)
#endif

extern bk_err_t gpio_sdio_sel(int mode);
extern bk_err_t gpio_sdio_one_line_sel(int mode);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_sdio_priv_s
{
  struct sdio_dev_s dev;          /* NuttX lower-half vtable anchor */
  FAR const struct bk7258_sdio_board_s *board;
  bool widebus_enabled;           /* 4-bit mode active */
  bool initialized;                /* bk_sdio_host_init() done */
  bool driver_init;                /* bk_sdio_host_driver_init() done */
  bool interface_init;             /* dev vtable copied exactly once */
  uint32_t width_transitions;       /* Successful 1-bit/4-bit re-inits */
  uint32_t width_failures;          /* Failed width re-inits */
  int last_width_error;             /* Most recent width re-init status */
  uint32_t cmd_timeout;            /* Controller clock-cycle timeout */
  uint32_t data_timeout;           /* Controller clock-cycle timeout */

  /* Cached data-transfer setup (used by recv/send setup). */

  FAR uint8_t *xfer_buf;
  size_t xfer_nbytes;
  size_t blocklen;
  size_t nblocks;
  bool xfer_is_read;
  bool xfer_pending;

  /* Cached completion status for the polling event shim. */

  sdio_eventset_t events;
  sdio_eventset_t waitset;
  int xfer_result;

  /* Mechanical card-detect polling and MMC/SD media-change callback.  The
   * storage is always present; the selected binding decides at runtime
   * whether the fixed-media path or this polling path is active.
   */

  spinlock_t media_lock;
  struct work_s media_work;
  worker_t callback;
  FAR void *callback_arg;
  sdio_eventset_t callback_events;
  bool reported_present;
  bool media_poll_started;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void bk7258_sdio_reset(FAR struct sdio_dev_s *dev);
static sdio_capset_t bk7258_sdio_capabilities(FAR struct sdio_dev_s *dev);
static sdio_statset_t bk7258_sdio_status(FAR struct sdio_dev_s *dev);
static void bk7258_sdio_widebus(FAR struct sdio_dev_s *dev, bool enable);
static void bk7258_sdio_clock(FAR struct sdio_dev_s *dev,
                              enum sdio_clock_e rate);
static int bk7258_sdio_attach(FAR struct sdio_dev_s *dev);
static int bk7258_sdio_sendcmd(FAR struct sdio_dev_s *dev, uint32_t cmd,
                               uint32_t arg);
#ifdef CONFIG_SDIO_BLOCKSETUP
static void bk7258_sdio_blocksetup(FAR struct sdio_dev_s *dev,
                                   unsigned int blocklen,
                                   unsigned int nblocks);
#endif
static int bk7258_sdio_recvsetup(FAR struct sdio_dev_s *dev,
                                 FAR uint8_t *buffer, size_t nbytes);
static int bk7258_sdio_sendsetup(FAR struct sdio_dev_s *dev,
                                 FAR const uint8_t *buffer, size_t nbytes);
static int bk7258_sdio_cancel(FAR struct sdio_dev_s *dev);
static int bk7258_sdio_waitresponse(FAR struct sdio_dev_s *dev,
                                    uint32_t cmd);
static int bk7258_sdio_recv_r1(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R1);
static int bk7258_sdio_recv_r2(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t R2[4]);
static int bk7258_sdio_recv_r3(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R3);
static int bk7258_sdio_recv_r4(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R4);
static int bk7258_sdio_recv_r5(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R5);
static int bk7258_sdio_recv_r6(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R6);
static int bk7258_sdio_recv_r7(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R7);
static void bk7258_sdio_waitenable(FAR struct sdio_dev_s *dev,
                                   sdio_eventset_t eventset, uint32_t timeout);
static sdio_eventset_t bk7258_sdio_eventwait(FAR struct sdio_dev_s *dev);
static void bk7258_sdio_callbackenable(FAR struct sdio_dev_s *dev,
                                       sdio_eventset_t eventset);
static int bk7258_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                        worker_t callback, FAR void *arg);
static void bk7258_sdio_media_worker(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_sdio_priv_s g_bk7258_sdio;

static const struct sdio_dev_s g_bk7258_sdio_ops =
{
  .reset        = bk7258_sdio_reset,
  .capabilities = bk7258_sdio_capabilities,
  .status       = bk7258_sdio_status,
  .widebus      = bk7258_sdio_widebus,
  .clock        = bk7258_sdio_clock,
  .attach       = bk7258_sdio_attach,
  .sendcmd      = bk7258_sdio_sendcmd,
#ifdef CONFIG_SDIO_BLOCKSETUP
  .blocksetup   = bk7258_sdio_blocksetup,
#endif
  .recvsetup    = bk7258_sdio_recvsetup,
  .sendsetup    = bk7258_sdio_sendsetup,
  .cancel       = bk7258_sdio_cancel,
  .waitresponse = bk7258_sdio_waitresponse,
  .recv_r1      = bk7258_sdio_recv_r1,
  .recv_r2      = bk7258_sdio_recv_r2,
  .recv_r3      = bk7258_sdio_recv_r3,
  .recv_r4      = bk7258_sdio_recv_r4,
  .recv_r5      = bk7258_sdio_recv_r5,
  .recv_r6      = bk7258_sdio_recv_r6,
  .recv_r7      = bk7258_sdio_recv_r7,
  .waitenable   = bk7258_sdio_waitenable,
  .eventwait    = bk7258_sdio_eventwait,
  .callbackenable = bk7258_sdio_callbackenable,
  .registercallback = bk7258_sdio_registercallback,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_sdio_map_err
 ****************************************************************************/

static int bk7258_sdio_map_err(bk_err_t err)
{
  if (err == BK_OK)
    {
      return OK;
    }

  switch (err)
    {
      case BK_ERR_SDIO_HOST_CMD_RSP_TIMEOUT:
      case BK_ERR_SDIO_HOST_DATA_TIMEOUT:
        return -ETIMEDOUT;
      case BK_ERR_SDIO_HOST_NOT_INIT:
        return -EAGAIN;
      default:
        return -EIO;
    }
}

/* v3.1.1.9 waits only four milliseconds in each public command-queue call.
 * Keep the same command in flight and repeat that public wait until the
 * NuttX command deadline expires.  The SDK ISR remains the sole owner of
 * command status and queue delivery throughout the transaction.
 */

static bk_err_t bk7258_sdio_wait_command(uint32_t cmd_index)
{
  clock_t start;
  clock_t limit;
  bk_err_t err;

  start = clock_systime_ticks();
  limit = MSEC2TICK(BK7258_SDIO_CMD_WAIT_MS);
  if (limit < 1)
    {
      limit = 1;
    }

  do
    {
      err = bk_sdio_host_wait_cmd_response(cmd_index);
      if (err != BK_ERR_SDIO_HOST_CMD_RSP_TIMEOUT)
        {
          return err;
        }
    }
  while (clock_systime_ticks() - start < limit);

  return BK_ERR_SDIO_HOST_CMD_RSP_TIMEOUT;
}

static int bk7258_sdio_configure_pins(
  FAR const struct bk7258_sdio_pin_config_s *pins)
{
  static const uint8_t expected[BK7258_SDIO_MAP_COUNT]
                               [BK7258_SDIO_PIN_COUNT] =
  {
    {2u, 3u, 4u, 5u, 10u, 11u},
    {14u, 15u, 16u, 17u, 18u, 19u},
  };
  static const uint8_t sdk_defaults[BK7258_SDIO_SDK_DEFAULT_COUNT] =
  {
    2u, 3u, 4u
  };
  struct bk7258_pinmux_config_s reclaim[
    BK7258_SDIO_SDK_DEFAULT_COUNT + BK7258_SDIO_PIN_COUNT];
  const uint8_t *selected;
  gpio_id_t gpio;
  size_t count;
  size_t index;
  size_t selected_count;
  bk_err_t error;
  int ret;

  if (pins == NULL || pins->map_mode >= BK7258_SDIO_MAP_COUNT)
    {
      return -EINVAL;
    }

  selected = expected[pins->map_mode];
  if (pins->clk_pin != selected[0] || pins->cmd_pin != selected[1] ||
      memcmp(pins->data_pin, &selected[2], sizeof(pins->data_pin)) != 0)
    {
      return -EINVAL;
    }

  if (bk_gpio_driver_init() != BK_OK)
    {
      return -EIO;
    }

#ifdef CONFIG_BK7258_SDIO_4BIT
  /* Physical pin ownership and the negotiated protocol width are different
   * facts.  A four-line instance must own and pull up DAT1-DAT3 from power-on
   * even though the controller starts in one-line identification mode.  The
   * SDK default tables can leave optional SDIO data pins in unrelated output
   * functions; leaving DAT3 there can hold the card-select signal low during
   * CMD0 and put an attached device into SPI mode.  ACMD6 still remains the
   * only point that changes the controller's bus width.
   */

  selected_count = BK7258_SDIO_PIN_COUNT;
#else
  selected_count = 3u;
#endif
  count = 0u;
  for (index = 0u; index < BK7258_SDIO_SDK_DEFAULT_COUNT; index++)
    {
      reclaim[count].pin = sdk_defaults[index];
      reclaim[count].function = 0u;
      reclaim[count].peripheral = false;
      count++;
    }

  /* A one-bit binding owns only CLK/CMD/D0 and leaves every unselected data
   * pin available to its selected platform function.
   */

  for (index = 0u; index < selected_count; index++)
    {
      reclaim[count].pin = selected[index];
      reclaim[count].function = 0u;
      reclaim[count].peripheral = false;
      count++;
    }

  ret = bk7258_pinmux_apply(reclaim, count);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_SDIO_4BIT
  error = gpio_sdio_sel(pins->map_mode);
#else
  error = gpio_sdio_one_line_sel(pins->map_mode);
#endif
  if (error != BK_OK)
    {
      return -EIO;
    }

  for (index = 0u; index < selected_count; index++)
    {
      gpio = (gpio_id_t)selected[index];
      if (bk_gpio_pull_up(gpio) != BK_OK ||
          bk_gpio_set_capacity(gpio, GPIO_DRIVER_CAPACITY_3) != BK_OK)
        {
          return -EIO;
        }
    }

  return OK;
}

static bool bk7258_sdio_card_detect_enabled(
  FAR const struct bk7258_sdio_priv_s *priv)
{
  return priv != NULL && priv->board != NULL &&
         priv->board->card_detect_available;
}

#ifdef CONFIG_SDIO_V2P0
static void bk7258_sdio_finish_stop_transmission(void)
{
  /* Match the tail of the official v3.1.1.9 CMD12 sequence on every exit.
   * The command needs a continuously clocked card; its tail returns the
   * controller to the SDK's FIFO-controlled state.  The next command restores
   * continuous clocking.  Reset once more afterward because the controller
   * may have prefetched invalid data before the card accepted CMD12.
   */

  bk_sdio_tx_fifo_clk_gate_config(0);
  bk_sdio_clk_gate_config(0);
  bk_sdio_host_reset_sd_state();
}
#endif

/****************************************************************************
 * Name: bk7258_sdio_complete_read
 *
 * Description:
 *   Finish the read configured by recvsetup().  The SDK's convenience
 *   bk_sdio_host_read_blks_fifo() hard-codes 512-byte blocks, while NuttX
 *   also requests short protocol records such as the 8-byte SCR.  Drain the
 *   public word FIFO directly so the configured block geometry is honoured.
 *   Follow the official SDK CPU-FIFO order for every block: wait for the data
 *   completion token first, validate its CRC/error result, then drain the
 *   FIFO.  The selected profile constrains NuttX to single-block transfers,
 *   so this preserves the SDK contract without a multiblock overflow window.
 ****************************************************************************/

static int bk7258_sdio_complete_read(
  FAR struct bk7258_sdio_priv_s *priv, uint32_t cmd_index)
{
  size_t offset = 0;
  size_t block;
  size_t chunk;
  size_t copied;
  uint32_t word;
  bk_err_t err;

  if (!priv->xfer_pending || !priv->xfer_is_read ||
      priv->xfer_buf == NULL || priv->blocklen == 0 ||
      priv->nblocks == 0)
    {
      return -EINVAL;
    }

  for (block = 0; block < priv->nblocks && offset < priv->xfer_nbytes;
       block++)
    {
      chunk = priv->blocklen;
      if (chunk > priv->xfer_nbytes - offset)
        {
          chunk = priv->xfer_nbytes - offset;
        }

      err = bk_sdio_host_wait_receive_data();
      if (err != BK_OK)
        {
          syslog(LOG_ERR,
                 "BKSDIO CMD%lu RX wait failed: block=%lu/%lu "
                 "blocklen=%lu bytes=%lu sdk=%d\n",
                 (unsigned long)cmd_index,
                 (unsigned long)block,
                 (unsigned long)priv->nblocks,
                 (unsigned long)priv->blocklen,
                 (unsigned long)priv->xfer_nbytes, err);
          priv->xfer_pending = false;
          return bk7258_sdio_map_err(err);
        }

      copied = 0;
      while (copied < chunk)
        {
          size_t bytes = chunk - copied;

          err = bk_sdio_host_read_fifo(&word);
          if (err != BK_OK)
            {
              syslog(LOG_ERR,
                     "BKSDIO CMD%lu RX fifo failed: block=%lu/%lu offset=%lu "
                     "sdk=%d\n",
                     (unsigned long)cmd_index,
                     (unsigned long)block,
                     (unsigned long)priv->nblocks,
                     (unsigned long)(offset + copied), err);
              priv->xfer_pending = false;
              return bk7258_sdio_map_err(err);
            }

          if (bytes > sizeof(word))
            {
              bytes = sizeof(word);
            }

          memcpy(priv->xfer_buf + offset + copied, &word, bytes);
          copied += bytes;
        }

      offset += chunk;
    }

  priv->xfer_pending = false;
  return offset == priv->xfer_nbytes ? OK : -EIO;
}

/****************************************************************************
 * Name: bk7258_sdio_host_init_locked
 *
 * Description:
 *   (Re)initialise the Beken SDIO host with the given bus width.  Caller
 *   holds no lock assumption; this is the single place that builds
 *   sdio_host_config_t.
 ****************************************************************************/

static int bk7258_sdio_host_init_locked(FAR struct bk7258_sdio_priv_s *priv,
                                        bool widebus)
{
  sdio_host_config_t cfg;
  bk_err_t err;
  int ret;

  /* Init the host at the slowest supported clock: SD card protocol wants
   * 400 kHz, but the V2.0 enum's slowest divider is 100 kHz.
   */

#ifdef CONFIG_SDIO_V2P0
  cfg.clock_freq = SDIO_HOST_CLK_100K;
#else
  cfg.clock_freq = SDIO_HOST_CLK_400K;
#endif
  cfg.bus_width = widebus ? SDIO_HOST_BUS_WIDTH_4LINE
                          : SDIO_HOST_BUS_WIDTH_1LINE;
  cfg.dma_tx_en = 0;
  cfg.dma_rx_en = 0;

#ifdef CONFIG_SDIO_V2P0
  /* Match the official v3.1.1.9 bk_sd_card_init() ordering.  Continuous
   * clocking is needed while the card is leaving its power-up state; the
   * setting survives bk_sdio_host_init()'s partial register reset.
   */

  bk_sdio_clk_gate_config(1);
#endif

  /* bk_sdio_host_init() in the fixed v3.1.1.9 SDK raises CLK_PWR_ID_SDIO
   * through sys_drv_dev_clk_pwr_up() -> bk_pm_clock_ctrl().  The AP linker
   * wrapper translates that existing vendor edge to the CP-owned RPMsg PM
   * service.  Do not add a second explicit vote here: it would double the
   * server reference count and leave the clock pinned after deinit.
   */

  err = bk_sdio_host_init(&cfg);
  if (err == BK_OK)
    {
      /* v3.1.1.9 sdio_host_init_common() trusts its SDK-application GPIO
       * default table when CONFIG_GPIO_DEFAULT_SET_SUPPORT is enabled.  That
       * table is not a board binding for NuttX, so re-apply the generated
       * physical route after every init.  Keep this inside the single re-init
       * path so reset and the post-ACMD6 width transition preserve it.
       */

      ret = bk7258_sdio_configure_pins(priv->board->pins);
      if (ret < 0)
        {
          (void)bk_sdio_host_deinit();
          return ret;
        }

      /* The official SD-card wrapper gives the powered controller and card
       * 30 ms to settle before issuing CMD0.
       */

      up_mdelay(30);
      priv->widebus_enabled = widebus;
      priv->initialized = true;
      priv->cmd_timeout = BK7258_SDIO_ID_CMD_TIMEOUT;
      priv->data_timeout = BK7258_SDIO_ID_DATA_TIMEOUT;
    }
  return bk7258_sdio_map_err(err);
}

static void bk7258_sdio_reset(FAR struct sdio_dev_s *dev)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  bk_err_t err;
  int ret;

  if (priv->initialized)
    {
      err = bk_sdio_host_deinit();
      if (err != BK_OK)
        {
          priv->width_failures++;
          priv->last_width_error = bk7258_sdio_map_err(err);
          return;
        }

      priv->initialized = false;
    }

  ret = bk7258_sdio_host_init_locked(priv, priv->widebus_enabled);
  if (ret < 0)
    {
      priv->width_failures++;
      priv->last_width_error = ret;
    }
  else
    {
      priv->last_width_error = OK;
    }
}

static sdio_capset_t bk7258_sdio_capabilities(FAR struct sdio_dev_s *dev)
{
  /* Advertise exactly the data mode compiled into the selected SDK bundle.
   * The v3.1.1.9 V2 data-setup helpers re-apply their private compile-time
   * width on every transfer.  SDIO_CAPS_4BIT_ONLY makes NuttX send ACMD6 and
   * call widebus(true) before its first data transfer (the SCR read), so the
   * card and the fixed-four-line controller become wide at the same point.
   * The ordinary SDIO_CAPS_4BIT path reads SCR first and is therefore unsafe
   * with that bundle.  The default bundle remains explicitly one-bit-only.
   */

  (void)dev;
#ifdef CONFIG_BK7258_SDIO_4BIT
  return SDIO_CAPS_4BIT_ONLY;
#else
  /* NuttX tests this explicit flag before sending ACMD6.  Returning zero
   * does not mean one-bit-only; it means that the upper half may still
   * switch an SD card to four data lines.
   */

  return SDIO_CAPS_1BIT_ONLY;
#endif
}

static sdio_statset_t bk7258_sdio_status(FAR struct sdio_dev_s *dev)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  return priv != NULL && priv->board != NULL &&
         priv->board->card_present() ? SDIO_STATUS_PRESENT : 0;
}

static void bk7258_sdio_widebus(FAR struct sdio_dev_s *dev, bool enable)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  bk_err_t err;
  int ret;

#ifndef CONFIG_BK7258_SDIO_4BIT
  enable = false;
#endif

  if (priv->initialized && (enable != priv->widebus_enabled))
    {
      /* Bus width is fixed at host-init; re-init to apply.  This resets the
       * controller but is the only SDK path to change width.
       */

      err = bk_sdio_host_deinit();
      if (err != BK_OK)
        {
          priv->width_failures++;
          priv->last_width_error = bk7258_sdio_map_err(err);
          return;
        }

      priv->initialized = false;
      ret = bk7258_sdio_host_init_locked(priv, enable);
      if (ret < 0)
        {
          priv->width_failures++;
          priv->last_width_error = ret;
          return;
        }

      priv->width_transitions++;
      priv->last_width_error = OK;
    }
}

static void bk7258_sdio_clock(FAR struct sdio_dev_s *dev,
                              enum sdio_clock_e rate)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
#ifdef CONFIG_SDIO_V2P0
  sdio_host_clock_freq_t freq = SDIO_HOST_CLK_100K;
#else
  sdio_host_clock_freq_t freq = SDIO_HOST_CLK_400K;
#endif

  if (!priv->initialized)
    {
      return;
    }

  /* Map the NuttX clock enum onto the nearest Beken divider.  The V2.0
   * enum carries divider indices (100K..20M/80M); the non-V2.0 enum
   * carries real Hz values.
   */

  switch (rate)
    {
      case CLOCK_SDIO_DISABLED:
        return;   /* leave running; SDK has no explicit gate here */
      case CLOCK_IDMODE:
#ifdef CONFIG_SDIO_V2P0
        freq = SDIO_HOST_CLK_100K;
#else
        freq = SDIO_HOST_CLK_400K;
#endif
        break;
      case CLOCK_MMC_TRANSFER:
      case CLOCK_SD_TRANSFER_1BIT:
      case CLOCK_SD_TRANSFER_4BIT:
      default:
#ifdef CONFIG_SDIO_V2P0
        /* Match the official v3.1.1.9 BK7258 SD-card profile.  Its
         * CONFIG_SDCARD_DEFAULT_CLOCK_FREQ is enum value 14, i.e. 20 MHz.
         * Profiles using this immutable CPU-FIFO SDK bundle constrain NuttX
         * to single-block transfers, avoiding the controller's multiblock
         * pre-read overflow without reducing ordinary block throughput.
         */

        freq = SDIO_HOST_CLK_20M;
#else
        freq = SDIO_HOST_CLK_26M;
#endif
        break;
    }

  if (bk_sdio_host_set_clock_freq(freq) == BK_OK)
    {
      /* Keep the continuous card clock selected by host_init_locked().
       * Official v3.1.1.9 and Tuya both leave it enabled across CMD7,
       * CMD16 and the ordinary transfer-clock switch.  Disabling it here
       * makes command clocking depend on FIFO state and can prevent the
       * first post-selection command from reaching the card.  CMD12 owns
       * its separate, bounded gate sequence in sendcmd().
       */

      if (rate == CLOCK_IDMODE)
        {
          priv->cmd_timeout = BK7258_SDIO_ID_CMD_TIMEOUT;
          priv->data_timeout = BK7258_SDIO_ID_DATA_TIMEOUT;
        }
      else if (rate != CLOCK_SDIO_DISABLED)
        {
          priv->cmd_timeout = BK7258_SDIO_XFR_CMD_TIMEOUT;
          priv->data_timeout = BK7258_SDIO_XFR_DATA_TIMEOUT;
        }
    }
}

static int bk7258_sdio_attach(FAR struct sdio_dev_s *dev)
{
  /* The SDK owns and registers the controller ISR. */

  return OK;
}

static int bk7258_sdio_sendcmd(FAR struct sdio_dev_s *dev, uint32_t cmd,
                               uint32_t arg)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  sdio_host_cmd_cfg_t host_cmd;
  uint32_t rsp_type;
  bk_err_t err;
#ifdef CONFIG_SDIO_V2P0
  bool stop_transmission;
#endif

  if (!priv->initialized)
    {
      return -EAGAIN;
    }

  host_cmd.cmd_index = cmd & 0x3f;
  host_cmd.argument  = arg;
  host_cmd.wait_rsp_timeout = priv->cmd_timeout;
  host_cmd.crc_check = true;

  BKSDIO_TRACE("CMD_BEGIN CMD%lu arg=%08lx flags=%08lx pending=%u",
               (unsigned long)host_cmd.cmd_index,
               (unsigned long)arg, (unsigned long)cmd,
               priv->xfer_pending);

  rsp_type = (cmd & SDIO_NUTTX_RSP_MASK) >> SDIO_NUTTX_RSP_SHIFT;
  switch (rsp_type)
    {
      case 0: /* no response */
        host_cmd.response = SDIO_HOST_CMD_RSP_NONE;
        host_cmd.crc_check = false;
        break;
      case 3: /* R2, 128-bit */
        host_cmd.response = SDIO_HOST_CMD_RSP_LONG;
        break;
      case 4: /* R3, no CRC */
      case 5: /* R4, no CRC */
      case 6: /* R5, no CRC */
        host_cmd.response = SDIO_HOST_CMD_RSP_SHORT;
        host_cmd.crc_check = false;
        break;
      default: /* R1/R6/R7 and others, 48-bit with CRC */
        host_cmd.response = SDIO_HOST_CMD_RSP_SHORT;
        break;
    }

#ifdef CONFIG_SDIO_V2P0
  stop_transmission = host_cmd.cmd_index == 12u;

  /* BK7258 continues pre-reading after the requested CMD18 blocks.  With
   * FIFO-controlled clock backpressure, those residual words can stop the
   * card clock before CMD12 is issued.  The official v3.1.1.9 SD-card driver
   * resets the data/FIFO state and temporarily forces the clock on around
   * STOP_TRANSMISSION; preserve that hardware protocol here.
   */

  if (stop_transmission)
    {
      bk_sdio_host_reset_sd_state();
    }
  else if (host_cmd.cmd_index == 24u || host_cmd.cmd_index == 25u)
    {
      /* NuttX sends the write command before sendsetup(), so this command
       * edge is the only correct place to match the SDK's write reset.
       */

      bk_sdio_host_reset_sd_state();
    }

  /* CMD12 deliberately leaves the controller in FIFO-controlled clock mode.
   * Restore continuous clocking at the next command boundary; doing this
   * here cannot race an active data phase and matches the SDK requirement
   * that command/response traffic always has a card clock.
   */

  bk_sdio_clk_gate_config(1);
#endif

  err = bk_sdio_host_send_command(&host_cmd);
  if (err != BK_OK)
    {
      syslog(LOG_ERR,
             "BKSDIO command start failed: CMD%lu arg=%08lx sdk=%d\n",
             (unsigned long)host_cmd.cmd_index,
             (unsigned long)host_cmd.argument, err);
#ifdef CONFIG_SDIO_V2P0
      if (stop_transmission)
        {
          bk7258_sdio_finish_stop_transmission();
        }
#endif

      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      if ((cmd & MMCSD_DATAXFR_MASK) != 0)
        {
          priv->xfer_pending = false;
        }

      return priv->xfer_result;
    }

  err = bk7258_sdio_wait_command(host_cmd.cmd_index);
#ifdef CONFIG_SDIO_V2P0
  if (stop_transmission)
    {
      bk7258_sdio_finish_stop_transmission();
    }
#endif

  priv->xfer_result = bk7258_sdio_map_err(err);
  BKSDIO_TRACE("CMD_DONE CMD%lu sdk=%d result=%d",
               (unsigned long)host_cmd.cmd_index, err, priv->xfer_result);
  if (err != BK_OK)
    {
      syslog(LOG_ERR,
             "BKSDIO command failed: CMD%lu arg=%08lx timeout=%lu "
             "sdk=%d\n",
             (unsigned long)host_cmd.cmd_index,
             (unsigned long)host_cmd.argument,
             (unsigned long)host_cmd.wait_rsp_timeout, err);
      priv->events = SDIOWAIT_ERROR;
      if ((cmd & MMCSD_DATAXFR_MASK) != 0)
        {
          priv->xfer_pending = false;
        }

      return priv->xfer_result;
    }

  priv->events |= SDIOWAIT_CMDDONE | SDIOWAIT_RESPONSEDONE;

  /* recvsetup() must precede the command in the NuttX contract.  Complete
   * only on the actual read-data command; an intervening CMD55 must leave
   * the pending SCR/ACMD transfer armed.
   */

  if (priv->xfer_pending && priv->xfer_is_read &&
      (cmd & MMCSD_DATAXFR_MASK) != 0 &&
      (cmd & MMCSD_WRXFR) == 0)
    {
      priv->xfer_result =
        bk7258_sdio_complete_read(priv, host_cmd.cmd_index);
      if (priv->xfer_result < 0)
        {
          priv->events |= SDIOWAIT_ERROR;
        }
      else
        {
          priv->events |= SDIOWAIT_TRANSFERDONE;
        }
    }

  return priv->xfer_result;
}

#ifdef CONFIG_SDIO_BLOCKSETUP
static void bk7258_sdio_blocksetup(FAR struct sdio_dev_s *dev,
                                   unsigned int blocklen,
                                   unsigned int nblocks)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  priv->blocklen = blocklen;
  priv->nblocks = nblocks;
}
#endif

static int bk7258_sdio_recvsetup(FAR struct sdio_dev_s *dev,
                                 FAR uint8_t *buffer, size_t nbytes)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  sdio_host_data_config_t dcfg;
  bk_err_t err;

  if (!priv->initialized)
    {
      return -EAGAIN;
    }

  if (buffer == NULL || nbytes == 0 || nbytes > UINT32_MAX)
    {
      return -EINVAL;
    }

#ifdef CONFIG_SDIO_BLOCKSETUP
  if (priv->blocklen == 0 || priv->nblocks == 0 ||
      priv->nblocks > SIZE_MAX / priv->blocklen ||
      priv->blocklen * priv->nblocks != nbytes ||
      priv->blocklen > UINT32_MAX)
    {
      return -EINVAL;
    }
#else
  priv->blocklen = nbytes;
  priv->nblocks = 1;
#endif

  priv->xfer_buf = buffer;
  priv->xfer_nbytes = nbytes;
  priv->xfer_is_read = true;
  priv->xfer_pending = true;

  dcfg.data_timeout    = priv->data_timeout;
  dcfg.data_len        = (uint32_t)nbytes;
  dcfg.data_block_size = (uint32_t)priv->blocklen;
  dcfg.data_dir        = SDIO_HOST_DATA_DIR_RD;

  BKSDIO_TRACE("RX_CONFIG len=%lu blocks=%lu bytes=%lu",
               (unsigned long)priv->blocklen,
               (unsigned long)priv->nblocks,
               (unsigned long)nbytes);

#ifdef CONFIG_SDIO_V2P0
  if (priv->blocklen == 512u)
    {
      bk_sdio_host_reset_sd_state();
      bk_sdio_host_discard_previous_receive_data_sema();
    }
#endif

  err = bk_sdio_host_config_data(&dcfg);
  if (err != BK_OK)
    {
      syslog(LOG_ERR,
             "BKSDIO RX config failed: blocklen=%lu blocks=%lu bytes=%lu "
             "sdk=%d\n",
             (unsigned long)priv->blocklen,
             (unsigned long)priv->nblocks,
             (unsigned long)nbytes, err);
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      priv->xfer_pending = false;
      return priv->xfer_result;
    }

  priv->xfer_result = OK;
  return OK;
}

static int bk7258_sdio_sendsetup(FAR struct sdio_dev_s *dev,
                                 FAR const uint8_t *buffer, size_t nbytes)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  sdio_host_data_config_t dcfg;
  bk_err_t err;

  if (!priv->initialized)
    {
      return -EAGAIN;
    }

  if (buffer == NULL || nbytes == 0 || nbytes > UINT32_MAX)
    {
      return -EINVAL;
    }

#ifdef CONFIG_SDIO_BLOCKSETUP
  if (priv->blocklen == 0 || priv->nblocks == 0 ||
      priv->nblocks > SIZE_MAX / priv->blocklen ||
      priv->blocklen * priv->nblocks != nbytes ||
      priv->blocklen > UINT32_MAX)
    {
      return -EINVAL;
    }
#else
  priv->blocklen = nbytes;
  priv->nblocks = 1;
#endif

  /* The v3.1.1.9 CPU FIFO writer loads 32-bit words.  Reject a partial word
   * or unaligned source instead of reading beyond the caller's buffer or
   * relying on Cortex-M unaligned-access policy.  FAT_DIRECT_RETRY can then
   * retry an unaligned direct transfer through its aligned sector buffer.
   */

  if (((uintptr_t)buffer & 3u) != 0)
    {
      return -EFAULT;
    }

  if ((nbytes & 3u) != 0 || (nbytes % 512u) != 0)
    {
      return -ENOTSUP;
    }

  priv->xfer_buf = (FAR uint8_t *)buffer;
  priv->xfer_nbytes = nbytes;
  priv->xfer_is_read = false;
  priv->xfer_pending = true;

  dcfg.data_timeout    = priv->data_timeout;
  dcfg.data_len        = (uint32_t)nbytes;
  dcfg.data_block_size = (uint32_t)priv->blocklen;
  dcfg.data_dir        = SDIO_HOST_DATA_DIR_WR;

  err = bk_sdio_host_config_data(&dcfg);
  if (err != BK_OK)
    {
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      priv->xfer_pending = false;
      return priv->xfer_result;
    }

  /* bk_sdio_host_write_fifo blocks internally until the FIFO accepts the
   * data; data_size must be 512-byte aligned per the SDK contract.
   */

  err = bk_sdio_host_write_fifo(buffer, (uint32_t)nbytes);
  priv->xfer_result = bk7258_sdio_map_err(err);
  priv->xfer_pending = false;
  priv->events |= (err == BK_OK) ? SDIOWAIT_TRANSFERDONE : SDIOWAIT_ERROR;
  return priv->xfer_result;
}

static int bk7258_sdio_cancel(FAR struct sdio_dev_s *dev)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  /* A blocking SDK call cannot be interrupted, but an armed read that has
   * not yet received its command can be cancelled safely.
   */

  priv->xfer_pending = false;
  priv->xfer_buf = NULL;
  priv->xfer_nbytes = 0;

  return OK;
}

static int bk7258_sdio_waitresponse(FAR struct sdio_dev_s *dev,
                                    uint32_t cmd)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  /* sendcmd() already completed this command through the SDK command queue.
   * Do not consume the SDK queue twice; simply replay the cached result.
   */

  (void)cmd;
  return priv->xfer_result;
}

static int bk7258_sdio_recv_r1(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R1)
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R1 != NULL)
    {
      *R1 = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static int bk7258_sdio_recv_r2(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t R2[4])
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R2 != NULL)
    {
      /* 128-bit response spans the four Beken response registers.  The SDK
       * lays the CID/CSD out MSB-first across RSP0..RSP3; we copy them in
       * that order into the NuttX R2[4] (MSB in R2[0]).
       */

      R2[0] = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
      R2[1] = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP1);
      R2[2] = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP2);
      R2[3] = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP3);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static int bk7258_sdio_recv_r3(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R3)
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R3 != NULL)
    {
      *R3 = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static int bk7258_sdio_recv_r4(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R4)
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R4 != NULL)
    {
      *R4 = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static int bk7258_sdio_recv_r5(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R5)
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R5 != NULL)
    {
      *R5 = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static int bk7258_sdio_recv_r6(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R6)
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R6 != NULL)
    {
      *R6 = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static int bk7258_sdio_recv_r7(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                FAR uint32_t *R7)
{
  bk7258_sdio_waitresponse(dev, cmd);
  if (R7 != NULL)
    {
      *R7 = bk_sdio_host_get_cmd_rsp_argument(SDIO_HOST_RSP0);
    }

  return ((FAR struct bk7258_sdio_priv_s *)dev)->xfer_result;
}

static void bk7258_sdio_waitenable(FAR struct sdio_dev_s *dev,
                                   sdio_eventset_t eventset, uint32_t timeout)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  (void)timeout;

  /* This arms a new wait.  Requested bits are not completion bits; keep
   * them separate and discard stale completion from the preceding command.
   * The SDK applies its own bounded command/data waits, so timeout is only
   * represented by the mapped SDIOWAIT_TIMEOUT result below.
   */

  priv->waitset = eventset;
  priv->events = 0;
}

static sdio_eventset_t bk7258_sdio_eventwait(FAR struct sdio_dev_s *dev)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  sdio_eventset_t ev;

  /* Replay the cached completion status.  For a successful transfer this is
   * SDIOWAIT_CMDDONE | SDIOWAIT_TRANSFERDONE; on error SDIOWAIT_ERROR.
   */

  ev = priv->events & priv->waitset;

  if ((priv->events & SDIOWAIT_ERROR) != 0)
    {
      ev |= priv->xfer_result == -ETIMEDOUT ? SDIOWAIT_TIMEOUT
                                            : SDIOWAIT_ERROR;
    }

  if (ev == 0)
    {
      ev = SDIOWAIT_ERROR;
    }

  priv->events = 0;
  priv->waitset = 0;
  return ev;
}

static void bk7258_sdio_callbackenable(FAR struct sdio_dev_s *dev,
                                       sdio_eventset_t eventset)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  irqstate_t flags;

  if (!bk7258_sdio_card_detect_enabled(priv))
    {
      /* Fixed-media slots never report insertion/ejection callbacks. */

      return;
    }

  /* The MMC/SD upper half arms one expected edge at a time.  Keep the
   * reported level unchanged while callbacks are disabled so an edge that
   * occurs between probe/remove and this call is delivered by the next poll.
   */

  flags = spin_lock_irqsave(&priv->media_lock);
  priv->callback_events = eventset &
                          (SDIOMEDIA_INSERTED | SDIOMEDIA_EJECTED);
  spin_unlock_irqrestore(&priv->media_lock, flags);
}

static void bk7258_sdio_media_worker(FAR void *arg)
{
  FAR struct bk7258_sdio_priv_s *priv = arg;
  worker_t callback = NULL;
  FAR void *callback_arg = NULL;
  sdio_eventset_t edge;
  irqstate_t flags;
  bool present;
  bool restart;
  int ret;

  present = priv->board->card_present();
  edge = present ? SDIOMEDIA_INSERTED : SDIOMEDIA_EJECTED;

  flags = spin_lock_irqsave(&priv->media_lock);
  if (present != priv->reported_present &&
      (priv->callback_events & edge) != 0)
    {
      priv->reported_present = present;
      priv->callback_events = 0;
      callback = priv->callback;
      callback_arg = priv->callback_arg;
    }

  restart = priv->media_poll_started;
  spin_unlock_irqrestore(&priv->media_lock, flags);

  /* NuttX requires media callbacks from work-thread context.  Invoke the
   * upper half without holding the private spinlock because it immediately
   * samples status and rearms the opposite edge.
   */

  if (callback != NULL)
    {
      callback(callback_arg);
    }

  if (restart)
    {
      ret = work_queue(HPWORK, &priv->media_work,
                       bk7258_sdio_media_worker, priv,
                       MSEC2TICK(priv->board->media_poll_ms));
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&priv->media_lock);
          priv->media_poll_started = false;
          spin_unlock_irqrestore(&priv->media_lock, flags);
          mcerr("ERROR: SDIO card-detect poll stopped: %d\n", ret);
        }
    }
}

static int bk7258_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                        worker_t callback, FAR void *arg)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  irqstate_t flags;
  bool start;
  int ret;

  if (!bk7258_sdio_card_detect_enabled(priv))
    {
      /* Registration is still required by the sdio_dev_s ABI, but a
       * fixed-media slot has no edge source to poll. */

      return OK;
    }

  flags = spin_lock_irqsave(&priv->media_lock);
  priv->callback_events = 0;
  priv->callback = callback;
  priv->callback_arg = arg;
  start = !priv->media_poll_started;
  priv->media_poll_started = true;
  spin_unlock_irqrestore(&priv->media_lock, flags);

  if (!start)
    {
      return OK;
    }

  ret = work_queue(HPWORK, &priv->media_work,
                   bk7258_sdio_media_worker, priv, 0);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&priv->media_lock);
      priv->media_poll_started = false;
      spin_unlock_irqrestore(&priv->media_lock, flags);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_sdio_initialize(
  FAR struct sdio_dev_s **sdio_dev,
  FAR const struct bk7258_sdio_board_s *board)
{
  FAR struct bk7258_sdio_priv_s *priv = &g_bk7258_sdio;
  bk_err_t err;

  if (sdio_dev == NULL || board == NULL || board->pins == NULL ||
      board->card_present == NULL ||
      (board->card_detect_available && board->media_poll_ms == 0))
    {
      return -EINVAL;
    }

  if (priv->board == NULL)
    {
      priv->board = board;
    }
  else if (priv->board != board)
    {
      return -EBUSY;
    }

  if (!priv->interface_init)
    {
      priv->dev = g_bk7258_sdio_ops;
      spin_lock_init(&priv->media_lock);
      priv->interface_init = true;
    }

  if (priv->initialized)
    {
      *sdio_dev = &priv->dev;
      return OK;
    }

  priv->events = 0;
  priv->waitset = 0;
  priv->xfer_result = OK;
  priv->xfer_buf = NULL;
  priv->xfer_nbytes = 0;
  priv->blocklen = 0;
  priv->nblocks = 0;
  priv->xfer_is_read = false;
  priv->xfer_pending = false;
  if (priv->board->prepare != NULL)
    {
      err = priv->board->prepare(BK7258_SDIO_BUS_WIDTH_4BIT != 0);
      if (err < 0)
        {
          return err;
        }
    }

  if (bk7258_sdio_card_detect_enabled(priv))
    {
      priv->reported_present = priv->board->card_present();
    }

  if (!priv->driver_init)
    {
      err = bk_sdio_host_driver_init();
      if (err != BK_OK)
        {
          return bk7258_sdio_map_err(err);
        }

      priv->driver_init = true;
    }

  /* Every SD card powers up in one-bit mode.  The four-bit profile maps
   * D1-D3 at the board layer and advertises SDIO_CAPS_4BIT_ONLY, but the
   * controller must remain narrow until the MMC/SD upper half has sent
   * ACMD6 successfully and calls bk7258_sdio_widebus(true).
   */

  err = bk7258_sdio_host_init_locked(priv, false);
  if (err != BK_OK)
    {
      if (priv->driver_init)
        {
          bk_sdio_host_driver_deinit();
          priv->driver_init = false;
        }

      return err;
    }

  *sdio_dev = &priv->dev;
  return OK;
}

int bk7258_sdio_get_runtime(FAR struct bk7258_sdio_runtime_s *runtime)
{
  FAR struct bk7258_sdio_priv_s *priv = &g_bk7258_sdio;

  if (runtime == NULL)
    {
      return -EINVAL;
    }

  runtime->initialized = priv->initialized ? 1u : 0u;
  runtime->bus_width = priv->widebus_enabled ? 4u : 1u;
  runtime->width_transitions = priv->width_transitions;
  runtime->width_failures = priv->width_failures;
  runtime->last_width_error = priv->last_width_error;
  return OK;
}

#endif /* CONFIG_BK7258_SDIO */
