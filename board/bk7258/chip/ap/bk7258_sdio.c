/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/bk7258_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SDIO host controller — NuttX sdio_dev_s lower-half over the
 * official Beken bk_sdio_host_* SDK API.  Zero register access.
 *
 * Role ownership: AP only.  The 24 bk_sdio_host_* symbols are compiled into
 * the AP libdriver.a exclusively; the CP bundle ships the headers but
 * defines none of the symbols, so this file is guarded by CONFIG_BK7258_AP_CORE.
 *
 * Data path:
 *
 *   NuttX MMCSD/SDIO upper half (sdio_dev_s ops):
 *     sendcmd(cmd,arg)  -> bk_sdio_host_send_command + bk_sdio_host_wait_..
 *     recv_r1..r7()     -> bk_sdio_host_wait_cmd_response +
 *                           bk_sdio_host_get_cmd_rsp_argument
 *     widebus(enable)   -> re-init with 1/4 line
 *     clock(rate)       -> bk_sdio_host_set_clock_freq
 *     recvsetup(buf,len)-> bk_sdio_host_config_data(RD) + read_blks_fifo +
 *                           bk_sdio_host_wait_receive_data
 *     sendsetup(buf,len)-> bk_sdio_host_config_data(WR) + write_fifo
 *
 * Notes on SDK behaviour that shaped this driver (verified against the
 * v3.1.1.9 headers, not assumed):
 *
 *  1. bk_sdio_host_driver_init() is a global one-time resource init;
 *     bk_sdio_host_init(config) powers up the unit and applies GPIO mapping.
 *     Both are one-shot; we call driver_init once and host_init once at
 *     bk7258_sdio_initialize(), then re-init only on reset()/widebus().
 *  2. The Beken SDIO host API is blocking.  NuttX, however, calls
 *     recvsetup() before it sends the read command.  recvsetup() therefore
 *     only configures the data path and records the buffer; sendcmd() drains
 *     the FIFO after the matching read command has completed.  Write setup
 *     occurs after the command and can finish synchronously in sendsetup().
 *     eventwait() then replays only the resulting completion/error bits.
 *  3. Command response type maps from the NuttX 32-bit cmd field: no-response
 *     -> SDIO_HOST_CMD_RSP_NONE; R2 (128-bit) -> SDIO_HOST_CMD_RSP_LONG;
 *     R1/R3/R4/R5/R6/R7 (48-bit) -> SDIO_HOST_CMD_RSP_SHORT.  CRC is checked
 *     for R1/R2/R6/R7 (the response types the spec CRC-protects); R3/R4/R5
 *     have no CRC and are sent without crc_check.
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

#include <arch/board/board.h>
#include <arch/chip/bk7258_sdk_abi.h>

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>
#endif

#include <arch/chip/bk7258_sdio.h>

#ifndef BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
#  error "Selected board must declare its SDIO card-detect capability"
#endif

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE && \
    !defined(BK7258_BOARD_SDIO_MEDIA_POLL_MS)
#  error "Card-detect boards must declare their media polling interval"
#endif

/* SDK API headers (Beken).  bk_err_t / BK_OK come via common/bk_err.h. */

#include <driver/sdio_host.h>
#include <driver/sdio_host_types.h>
#include <driver/int_types.h>

#if defined(CONFIG_BK7258_SDIO_4BIT) && \
    !defined(CONFIG_SDCARD_BUSWIDTH_4LINE)
#  error "Selected AP SDK bundle cannot preserve four-bit SDIO data setup"
#elif !defined(CONFIG_BK7258_SDIO_4BIT) && \
      defined(CONFIG_SDCARD_BUSWIDTH_4LINE)
#  error "Four-bit-only AP SDK bundle cannot serve a one-bit SDIO profile"
#endif

#include "arm_internal.h"
#include "bk7258_sdk_irq.h"

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

#define BK7258_SDIO_INT_STATUS         0x458d0034u
#define BK7258_SDIO_CMD_NO_RSP_END     (1u << 0)
#define BK7258_SDIO_CMD_RSP_END        (1u << 1)
#define BK7258_SDIO_CMD_RSP_TIMEOUT    (1u << 2)
#define BK7258_SDIO_CMD_CRC_OK         (1u << 10)
#define BK7258_SDIO_CMD_CRC_FAIL       (1u << 11)
#define BK7258_SDIO_CMD_STATUS_MASK    (BK7258_SDIO_CMD_NO_RSP_END | \
                                        BK7258_SDIO_CMD_RSP_END | \
                                        BK7258_SDIO_CMD_RSP_TIMEOUT | \
                                        BK7258_SDIO_CMD_CRC_OK | \
                                        BK7258_SDIO_CMD_CRC_FAIL)
#define BK7258_SDIO_IRQ                (BK7258_IRQ_FIRST + INT_SRC_SDIO)
#define BK7258_SDIO_CMD_POLL_US        100000u
#define SDIO_NUTTX_RSP_MASK            (15 << SDIO_NUTTX_RSP_SHIFT)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_sdio_priv_s
{
  struct sdio_dev_s dev;          /* NuttX lower-half vtable anchor */
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

  /* Mechanical card-detect polling and MMC/SD media-change callback.
   * Omitted entirely for fixed-media board variants.
   */

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  spinlock_t media_lock;
  struct work_s media_work;
  worker_t callback;
  FAR void *callback_arg;
  sdio_eventset_t callback_events;
  bool reported_present;
  bool media_poll_started;
#endif
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
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
static void bk7258_sdio_media_worker(FAR void *arg);
#endif

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
 *   Drain each block before waiting for its completion token.  The card clock
 *   remains continuous, matching the official SDK initialization path, so
 *   starting the FIFO drain immediately after the command response prevents
 *   the following block from overflowing while NuttX wakes.  The completion
 *   token is still consumed after every block to validate the SDK's CRC/error
 *   result before advancing to the next block.
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

      copied = 0;
      while (copied < chunk)
        {
          size_t bytes = chunk - copied;

          err = bk_sdio_host_read_fifo(&word);
          if (err != BK_OK)
            {
              syslog(LOG_ERR,
                     "BKSDIO CMD%lu RX fifo failed: block=%lu/%lu offset=%lu "
                     "sdk=%d status=%08lx\n",
                     (unsigned long)cmd_index,
                     (unsigned long)block,
                     (unsigned long)priv->nblocks,
                     (unsigned long)(offset + copied),
                     err, (unsigned long)getreg32(BK7258_SDIO_INT_STATUS));
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

      err = bk_sdio_host_wait_receive_data();
      if (err != BK_OK)
        {
          syslog(LOG_ERR,
                 "BKSDIO CMD%lu RX wait failed: block=%lu/%lu "
                 "blocklen=%lu bytes=%lu sdk=%d status=%08lx\n",
                 (unsigned long)cmd_index,
                 (unsigned long)block,
                 (unsigned long)priv->nblocks,
                 (unsigned long)priv->blocklen,
                 (unsigned long)priv->xfer_nbytes,
                 err, (unsigned long)getreg32(BK7258_SDIO_INT_STATUS));
          priv->xfer_pending = false;
          return bk7258_sdio_map_err(err);
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

/* v3.1.1.9 waits for command completion through a FreeRTOS queue with a
 * four-tick timeout.  Under the NuttX adapter that queue can miss the first
 * edge and leave a completion latched, so the following command consumes
 * stale state.  Command completion is inherently synchronous in the NuttX
 * sdio_dev_s contract; poll the documented BK7258 status register while the
 * SDK ISR line is masked, acknowledge only command bits, then restore the
 * line for data-transfer completion.  This leaves the vendor data path and
 * its semaphores unchanged.
 */

static bk_err_t bk7258_sdio_wait_command_polled(void)
{
  uint32_t status = 0;
  unsigned int elapsed;

  for (elapsed = 0; elapsed < BK7258_SDIO_CMD_POLL_US; elapsed += 10)
    {
      status = getreg32(BK7258_SDIO_INT_STATUS);
      if ((status & (BK7258_SDIO_CMD_NO_RSP_END |
                     BK7258_SDIO_CMD_RSP_END |
                     BK7258_SDIO_CMD_RSP_TIMEOUT)) != 0)
        {
          break;
        }

      up_udelay(10);
    }

  if ((status & BK7258_SDIO_CMD_STATUS_MASK) != 0)
    {
      putreg32(status & BK7258_SDIO_CMD_STATUS_MASK,
               BK7258_SDIO_INT_STATUS);
    }

  /* The SDK ISR shares this line for command and data completion.  Do not
   * clear NVIC pending after acknowledging only the command W1C bits: short
   * data such as SCR may already have asserted the same line.  Re-enabling
   * the IRQ lets the SDK consume any remaining data status.
   */

  UP_DSB();

  if ((status & BK7258_SDIO_CMD_RSP_TIMEOUT) != 0 ||
      elapsed >= BK7258_SDIO_CMD_POLL_US)
    {
      return BK_ERR_SDIO_HOST_CMD_RSP_TIMEOUT;
    }

  if ((status & BK7258_SDIO_CMD_CRC_FAIL) != 0)
    {
      return BK_ERR_SDIO_HOST_CMD_RSP_CRC_FAIL;
    }

  return BK_OK;
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
  (void)dev;

  return bk7258_board_sdio_card_present() ? SDIO_STATUS_PRESENT : 0;
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
  /* No interrupt wiring required by this polling framework. */

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

  up_disable_irq(BK7258_SDIO_IRQ);
  err = bk_sdio_host_send_command(&host_cmd);
  if (err != BK_OK)
    {
      syslog(LOG_ERR,
             "BKSDIO command start failed: CMD%lu arg=%08lx sdk=%d "
             "status=%08lx\n",
             (unsigned long)host_cmd.cmd_index,
             (unsigned long)host_cmd.argument, err,
             (unsigned long)getreg32(BK7258_SDIO_INT_STATUS));
#ifdef CONFIG_SDIO_V2P0
      if (stop_transmission)
        {
          bk7258_sdio_finish_stop_transmission();
        }
#endif

      bk7258_clear_pending_irq(BK7258_SDIO_IRQ);
      up_enable_irq(BK7258_SDIO_IRQ);
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      if ((cmd & MMCSD_DATAXFR_MASK) != 0)
        {
          priv->xfer_pending = false;
        }

      return priv->xfer_result;
    }

  /* Complete the synchronous NuttX command contract without the SDK's
   * FreeRTOS-oriented command-event queue.  Re-enable the ISR immediately
   * afterward so data completion remains interrupt-driven by the SDK.
   */

  err = bk7258_sdio_wait_command_polled();
#ifdef CONFIG_SDIO_V2P0
  if (stop_transmission)
    {
      bk7258_sdio_finish_stop_transmission();
    }
#endif

  up_enable_irq(BK7258_SDIO_IRQ);
  priv->xfer_result = bk7258_sdio_map_err(err);
  if (err != BK_OK)
    {
      syslog(LOG_ERR,
             "BKSDIO command failed: CMD%lu arg=%08lx timeout=%lu "
             "sdk=%d status=%08lx\n",
             (unsigned long)host_cmd.cmd_index,
             (unsigned long)host_cmd.argument,
             (unsigned long)host_cmd.wait_rsp_timeout, err,
             (unsigned long)getreg32(BK7258_SDIO_INT_STATUS));
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

#ifdef CONFIG_SDIO_V2P0
  /* The BK7258 controller pre-reads beyond a completed block transaction.
   * The official v3.1.1.9 SD-card path resets that data/FIFO state and
   * discards one stale RX semaphore before every non-contiguous block read.
   * Each NuttX block request is a discrete transaction, so apply the same
   * sequence for 512-byte media blocks while leaving short protocol records
   * such as the SCR on the generic FIFO path.
   */

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
             "sdk=%d status=%08lx\n",
             (unsigned long)priv->blocklen,
             (unsigned long)priv->nblocks,
             (unsigned long)nbytes, err,
             (unsigned long)getreg32(BK7258_SDIO_INT_STATUS));
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      priv->xfer_pending = false;
      return priv->xfer_result;
    }

  /* Do not touch the FIFO here.  NuttX has not sent the read command yet. */

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

  /* sendcmd() already consumed the response (it called
   * bk_sdio_host_wait_cmd_response(), which pops one response from the
   * SDK's irq_cmd_msg queue).  Calling it again here would either block
   * to timeout or pop the *next* command's response, so we only replay the
   * cached result.
   */

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

  /* Replay the cached completion status.  For a successful transfer this is
   * SDIOWAIT_CMDDONE | SDIOWAIT_TRANSFERDONE; on error SDIOWAIT_ERROR.
   */

  sdio_eventset_t ev = priv->events & priv->waitset;

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
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  irqstate_t flags;

  /* The MMC/SD upper half arms one expected edge at a time.  Keep the
   * reported level unchanged while callbacks are disabled so an edge that
   * occurs between probe/remove and this call is delivered by the next poll.
   */

  flags = spin_lock_irqsave(&priv->media_lock);
  priv->callback_events = eventset &
                          (SDIOMEDIA_INSERTED | SDIOMEDIA_EJECTED);
  spin_unlock_irqrestore(&priv->media_lock, flags);
#else
  /* Fixed-media slots never report insertion/ejection callbacks. */

  (void)dev;
  (void)eventset;
#endif
}

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
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

  present = bk7258_board_sdio_card_present();
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
                       MSEC2TICK(BK7258_BOARD_SDIO_MEDIA_POLL_MS));
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&priv->media_lock);
          priv->media_poll_started = false;
          spin_unlock_irqrestore(&priv->media_lock, flags);
          mcerr("ERROR: SDIO card-detect poll stopped: %d\n", ret);
        }
    }
}
#endif

static int bk7258_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                        worker_t callback, FAR void *arg)
{
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  irqstate_t flags;
  bool start;
  int ret;

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
#else
  /* Registration is required by the sdio_dev_s contract even when the
   * board has no reliable card-detect source.  NuttX will probe the
   * always-present slot once during initialization.
   */

  (void)dev;
  (void)callback;
  (void)arg;
  return OK;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_sdio_initialize(FAR struct sdio_dev_s **sdio_dev)
{
  FAR struct bk7258_sdio_priv_s *priv = &g_bk7258_sdio;
  bk_err_t err;

  if (sdio_dev == NULL)
    {
      return -EINVAL;
    }

  if (!priv->interface_init)
    {
      priv->dev = g_bk7258_sdio_ops;
#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
      spin_lock_init(&priv->media_lock);
#endif
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
  err = bk7258_board_sdio_initialize(BK7258_SDIO_BUS_WIDTH_4BIT != 0);
  if (err < 0)
    {
      return err;
    }

#if BK7258_BOARD_SDIO_CARD_DETECT_AVAILABLE
  priv->reported_present = bk7258_board_sdio_card_present();
#endif

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
