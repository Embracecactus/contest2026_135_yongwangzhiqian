/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_sdio.c
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
 *  2. The Beken SDIO host API is BLOCKING/polling: bk_sdio_host_send_command
 *     + bk_sdio_host_wait_cmd_response() and bk_sdio_host_read_blks_fifo() +
 *     bk_sdio_host_wait_receive_data() complete the transfer synchronously
 *     inside the call (the SDK internally blocks on its own semaphores).  It
 *     exposes NO command-complete / transfer-complete interrupt to the
 *     caller (only init/deinit callbacks).  Therefore the NuttX event model
 *     (waitenable/eventwait/callbackenable/registercallback) is implemented
 *     as a documented polling shim: the data transfer already finished inside
 *     recvsetup()/sendsetup(), so eventwait() simply replays the cached
 *     completion status.  This is correct for a polling SDIO host and keeps
 *     the framework compilable and functional; an interrupt-driven path
 *     (wiring the SDK ISR to NuttX SDIO_WAITENABLE events) is a later
 *     enhancement, not required for card bring-up.
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

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/sdio.h>

#include <arch/chip/bk7258_sdio.h>

/* SDK API headers (Beken).  bk_err_t / BK_OK come via common/bk_err.h. */

#include <driver/sdio_host.h>
#include <driver/sdio_host_types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_SDIO_TIMEOUT_MS
#  define CONFIG_BK7258_SDIO_TIMEOUT_MS 1000u
#endif

#define BK7258_SDIO_CMD_TIMEOUT_MS     2000u

/* Response type bits in the NuttX 32-bit command field (see nuttx/sdio.h). */

#define SDIO_NUTTX_RSP_SHIFT           6
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

  /* Cached data-transfer setup (used by recv/send setup). */

  FAR uint8_t *xfer_buf;
  size_t xfer_nbytes;
  bool xfer_is_read;

  /* Cached completion status for the polling event shim. */

  sdio_eventset_t events;
  int xfer_result;

#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
  worker_t callback;
  FAR void *callback_arg;
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
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int bk7258_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                        worker_t callback, FAR void *arg);
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
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
  .registercallback = bk7258_sdio_registercallback,
#endif
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

  err = bk_sdio_host_init(&cfg);
  if (err == BK_OK)
    {
      priv->widebus_enabled = widebus;
      priv->initialized = true;
    }

  return bk7258_sdio_map_err(err);
}

static void bk7258_sdio_reset(FAR struct sdio_dev_s *dev)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  if (priv->initialized)
    {
      bk_sdio_host_deinit();
      priv->initialized = false;
    }

  bk7258_sdio_host_init_locked(priv, priv->widebus_enabled);
}

static sdio_capset_t bk7258_sdio_capabilities(FAR struct sdio_dev_s *dev)
{
  /* 4-bit supported; DMA not used by this framework (polling FIFO path). */

  sdio_capset_t caps = SDIO_CAPS_4BIT;
  return caps;
}

static sdio_statset_t bk7258_sdio_status(FAR struct sdio_dev_s *dev)
{
  /* No hotplug detect in the Beken host API; report a card present.  The
   * MMCSD driver will discover absence at probe time if no card is seated.
   */

  return SDIO_STATUS_PRESENT;
}

static void bk7258_sdio_widebus(FAR struct sdio_dev_s *dev, bool enable)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  if (priv->initialized && (enable != priv->widebus_enabled))
    {
      /* Bus width is fixed at host-init; re-init to apply.  This resets the
       * controller but is the only SDK path to change width.
       */

      bk_sdio_host_deinit();
      priv->initialized = false;
      bk7258_sdio_host_init_locked(priv, enable);
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
        freq = SDIO_HOST_CLK_80M;
#else
        freq = SDIO_HOST_CLK_26M;
#endif
        break;
    }

  bk_sdio_host_set_clock_freq(freq);
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

  if (!priv->initialized)
    {
      return -EAGAIN;
    }

  host_cmd.cmd_index = cmd & 0x3f;
  host_cmd.argument  = arg;
  host_cmd.wait_rsp_timeout = BK7258_SDIO_CMD_TIMEOUT_MS;
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

  err = bk_sdio_host_send_command(&host_cmd);
  if (err != BK_OK)
    {
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      return priv->xfer_result;
    }

  /* Command is queued; wait for the response now so recv_rN() can read it
   * back synchronously.  Errors are surfaced through the cached status.
   */

  err = bk_sdio_host_wait_cmd_response(host_cmd.cmd_index);
  priv->xfer_result = bk7258_sdio_map_err(err);
  priv->events = (err == BK_OK) ? SDIOWAIT_CMDDONE : SDIOWAIT_ERROR;
  return priv->xfer_result;
}

#ifdef CONFIG_SDIO_BLOCKSETUP
static void bk7258_sdio_blocksetup(FAR struct sdio_dev_s *dev,
                                   unsigned int blocklen,
                                   unsigned int nblocks)
{
  /* Block geometry is captured at data config time inside recv/send setup;
   * nothing to cache separately for the polling FIFO path.  Reserved for
   * symmetry with the NuttX interface.
   */

  (void)dev;
  (void)blocklen;
  (void)nblocks;
}
#endif

static int bk7258_sdio_recvsetup(FAR struct sdio_dev_s *dev,
                                 FAR uint8_t *buffer, size_t nbytes)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  sdio_host_data_config_t dcfg;
  bk_err_t err;

  if (!priv->initialized || buffer == NULL || nbytes == 0)
    {
      return -EINVAL;
    }

  priv->xfer_buf = buffer;
  priv->xfer_nbytes = nbytes;
  priv->xfer_is_read = true;

  dcfg.data_timeout    = CONFIG_BK7258_SDIO_TIMEOUT_MS;
  dcfg.data_len        = (uint32_t)nbytes;
  dcfg.data_block_size = 512;
  dcfg.data_dir        = SDIO_HOST_DATA_DIR_RD;

  err = bk_sdio_host_config_data(&dcfg);
  if (err != BK_OK)
    {
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      return priv->xfer_result;
    }

  /* Read is always a whole number of 512-byte blocks per the SDK FIFO API. */

  err = bk_sdio_host_read_blks_fifo(buffer,
                                    (uint32_t)nbytes / 512u);
  if (err != BK_OK)
    {
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      return priv->xfer_result;
    }

  err = bk_sdio_host_wait_receive_data();
  priv->xfer_result = bk7258_sdio_map_err(err);
  priv->events = (err == BK_OK)
                 ? (SDIOWAIT_CMDDONE | SDIOWAIT_TRANSFERDONE)
                 : SDIOWAIT_ERROR;
  return priv->xfer_result;
}

static int bk7258_sdio_sendsetup(FAR struct sdio_dev_s *dev,
                                 FAR const uint8_t *buffer, size_t nbytes)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;
  sdio_host_data_config_t dcfg;
  bk_err_t err;

  if (!priv->initialized || buffer == NULL || nbytes == 0)
    {
      return -EINVAL;
    }

  priv->xfer_buf = (FAR uint8_t *)buffer;
  priv->xfer_nbytes = nbytes;
  priv->xfer_is_read = false;

  dcfg.data_timeout    = CONFIG_BK7258_SDIO_TIMEOUT_MS;
  dcfg.data_len        = (uint32_t)nbytes;
  dcfg.data_block_size = 512;
  dcfg.data_dir        = SDIO_HOST_DATA_DIR_WR;

  err = bk_sdio_host_config_data(&dcfg);
  if (err != BK_OK)
    {
      priv->xfer_result = bk7258_sdio_map_err(err);
      priv->events = SDIOWAIT_ERROR;
      return priv->xfer_result;
    }

  /* bk_sdio_host_write_fifo blocks internally until the FIFO accepts the
   * data; data_size must be 512-byte aligned per the SDK contract.
   */

  err = bk_sdio_host_write_fifo(buffer, (uint32_t)nbytes);
  priv->xfer_result = bk7258_sdio_map_err(err);
  priv->events = (err == BK_OK)
                 ? (SDIOWAIT_CMDDONE | SDIOWAIT_TRANSFERDONE)
                 : SDIOWAIT_ERROR;
  return priv->xfer_result;
}

static int bk7258_sdio_cancel(FAR struct sdio_dev_s *dev)
{
  /* Polling FIFO path has no in-flight async transfer to cancel. */

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

  /* Polling shim: transfers complete synchronously inside sendcmd /
   * recvsetup / sendsetup, so priv->events already reflects the real
   * completion (SDIOWAIT_CMDDONE | SDIOWAIT_TRANSFERDONE, or
   * SDIOWAIT_ERROR).  Do NOT overwrite that with the upper half's wanted
   * eventset — the wanted bits are about which events to wait for, not the
   * completion state.  OR the request in so eventwait() can report both.
   */

  priv->events |= eventset;
}

static sdio_eventset_t bk7258_sdio_eventwait(FAR struct sdio_dev_s *dev)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  /* Replay the cached completion status.  For a successful transfer this is
   * SDIOWAIT_CMDDONE | SDIOWAIT_TRANSFERDONE; on error SDIOWAIT_ERROR.
   */

  sdio_eventset_t ev = priv->events;
  priv->events = 0;
  return ev;
}

static void bk7258_sdio_callbackenable(FAR struct sdio_dev_s *dev,
                                       sdio_eventset_t eventset)
{
  /* Polling shim: no interrupt to enable. */

  (void)dev;
  (void)eventset;
}

#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int bk7258_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                        worker_t callback, FAR void *arg)
{
  FAR struct bk7258_sdio_priv_s *priv =
    (FAR struct bk7258_sdio_priv_s *)dev;

  /* Polling shim: store the callback; it would be invoked from the SDK ISR
   * context in a future interrupt-driven enhancement.  Not invoked here.
   */

  priv->callback = callback;
  priv->callback_arg = arg;
  return OK;
}
#endif

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

  priv->dev = g_bk7258_sdio_ops;   /* copy vtable into the instance */
  priv->events = 0;
  priv->xfer_result = OK;
  priv->xfer_buf = NULL;
  priv->xfer_nbytes = 0;
  priv->xfer_is_read = false;

  if (priv->initialized)
    {
      *sdio_dev = &priv->dev;
      return OK;
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

  err = bk7258_sdio_host_init_locked(priv, BK7258_SDIO_BUS_WIDTH_4BIT != 0);
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

#endif /* CONFIG_BK7258_SDIO */
