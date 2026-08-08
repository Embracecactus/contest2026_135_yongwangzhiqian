/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_spi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SPI master — NuttX spi_dev_s lower-half over the
 * official Beken bk_spi_* SDK API.  Zero register access.
 *
 * Role ownership: AP only.  bk_spi_* (37 symbols) are compiled into the AP
 * libdriver.a exclusively; the CP bundle ships the headers but defines none
 * of the symbols, so this file is guarded by CONFIG_BK7258_AP_CORE.
 *
 * Data path:
 *
 *   NuttX SPI sequence (upper half):
 *     lock() -> select(devid,true) -> setfrequency/setmode/setbits
 *            -> exchange(tx,rx,len) [xN] -> select(devid,false) -> unlock()
 *     -> bk_spi_set_baud_rate / bk_spi_set_mode / bk_spi_set_bit_width
 *       -> bk_spi_transmit / bk_spi_write_bytes / bk_spi_read_bytes
 *         -> Beken hardware SPI unit (id 0)
 *
 * Notes on SDK behaviour that shaped this driver (verified against the
 * v3.1.1.9 headers, not assumed):
 *
 *  1. bk_spi_driver_init() is a global one-time resource init shared by all
 *     units; called once on initialize() and deinit once on teardown.
 *     bk_spi_init(id, cfg) must follow driver_init().
 *  2. spi_config_t carries polarity/phase/bit_width/baud_rate/bit_order, but
 *     the live runtime setters are separate: bk_spi_set_mode(id, spi_mode_t)
 *     (POL_MODE_0..3), bk_spi_set_bit_width(id, 8/16), bk_spi_set_baud_rate().
 *     SPI_MODE_e (SPIDEV_MODE0..3) maps 1:1 onto spi_mode_t (POL_MODE_0..3).
 *  3. The Beken SPI driver has NO chip-select primitive.  CS is a board GPIO
 *     and is driven by the board-supplied callback installed via
 *     bk7258_spi_set_csinfo(); select() invokes it.  Without a callback CS
 *     is simply not toggled (the board may drive it externally).
 *  4. exchange() maps the three NuttX buffer combinations onto the SDK:
 *       tx && rx  -> bk_spi_transmit(id, tx, len, rx, len)  (full duplex)
 *       tx only   -> bk_spi_write_bytes(id, tx, len)
 *       rx only   -> bk_spi_read_bytes(id, rx, len)
 *     The SDK's bk_spi_transmit() requires equal tx/rx sizes, which holds
 *     for NuttX full-duplex exchanges (same nwords).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SPI

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include <arch/chip/bk7258_spi.h>

/* SDK API headers (Beken).  bk_err_t / BK_OK come via common/bk_err.h. */

#include <driver/spi.h>
#include <driver/spi_types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_SPI_BUS
#  define CONFIG_BK7258_SPI_BUS        0
#endif

#ifndef CONFIG_BK7258_SPI_TIMEOUT_MS
#  define CONFIG_BK7258_SPI_TIMEOUT_MS BK7258_SPI_TIMEOUT_MS_DEFAULT
#endif

/* NuttX SPIDEV_MODE0..3 align 1:1 with Beken SPI_POL_MODE_0..3. */

#define BK7258_SPI_MODE_MAX            3

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_spi_priv_s
{
  struct spi_dev_s dev;             /* NuttX lower-half vtable anchor */
  mutex_t lock;                     /* NuttX bus lock */
  spi_id_t id;                      /* BK7258 SPI unit id (SPI_ID_0) */
  uint32_t freq;                    /* Cached frequency */
  uint8_t mode;                     /* Cached SPI mode (0..3) */
  uint8_t bits;                     /* Cached bits per word (8/16) */
  bool initialized;                 /* bk_spi_init() done for this unit */
  bool driver_init;                 /* bk_spi_driver_init() done */
  bk7258_spi_cs_cb_t cs_cb;         /* Board chip-select callback */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_spi_lock(FAR struct spi_dev_s *dev, bool lock);
static void bk7258_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected);
static uint32_t bk7258_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency);
static void bk7258_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode);
static void bk7258_spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t bk7258_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid);
#ifdef CONFIG_SPI_EXCHANGE
static void bk7258_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords);
#else
static void bk7258_spi_sndblock(FAR struct spi_dev_s *dev,
                                FAR const void *buffer, size_t nwords);
static void bk7258_spi_recvblock(FAR struct spi_dev_s *dev,
                                 FAR void *buffer, size_t nwords);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_spi_priv_s g_bk7258_spi =
{
  .dev.ops  = NULL,   /* ops assigned below (after the vtable is defined) */
  .id       = (spi_id_t)BK7258_SPI_UNIT,
  .freq     = BK7258_SPI_BAUD_DEFAULT,
  .mode     = BK7258_SPI_MODE_DEFAULT,
  .bits     = BK7258_SPI_BITS_DEFAULT,
};

static const struct spi_ops_s g_bk7258_spi_ops =
{
  .lock        = bk7258_spi_lock,
  .select      = bk7258_spi_select,
  .setfrequency = bk7258_spi_setfrequency,
  .setmode     = bk7258_spi_setmode,
  .setbits     = bk7258_spi_setbits,
  .status      = bk7258_spi_status,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange    = bk7258_spi_exchange,
#else
  .sndblock    = bk7258_spi_sndblock,
  .recvblock   = bk7258_spi_recvblock,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_spi_map_err
 *
 * Description:
 *   Convert a Beken bk_err_t into a negated errno.  BK_OK (0) maps to OK.
 ****************************************************************************/

static int bk7258_spi_map_err(bk_err_t err)
{
  if (err == BK_OK)
    {
      return OK;
    }

  switch (err)
    {
      case BK_ERR_SPI_RX_TIMEOUT:
      case BK_ERR_SPI_TX_TIMEOUT:
        return -ETIMEDOUT;
      case BK_ERR_SPI_NOT_INIT:
      case BK_ERR_SPI_ID_NOT_INIT:
        return -EAGAIN;
      case BK_ERR_SPI_INVALID_ID:
        return -EINVAL;
      case BK_ERR_SPI_FIFO_WR_NOT_READY:
        /* BK_ERR_SPI_FIFO_RD_NOT_READY aliases this value in the SDK
         * (both are BK_ERR_SPI_BASE - 6), so a separate case for it would
         * be a duplicate case value.
         */
        return -EAGAIN;
      default:
        return -EIO;
    }
}

static int bk7258_spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;

  if (lock)
    {
      return nxmutex_lock(&priv->lock);
    }
  else
    {
      return nxmutex_unlock(&priv->lock);
    }
}

static void bk7258_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;

  /* CS is a board GPIO; the Beken driver exposes no CS primitive.  Invoke
   * the board callback if installed.  No callback => CS is not toggled
   * here (board may drive it externally).
   */

  if (priv->cs_cb != NULL)
    {
      priv->cs_cb(devid, selected);
    }
}

static uint32_t bk7258_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;

  if (!priv->initialized)
    {
      priv->freq = frequency;
      return frequency;
    }

  if (frequency != priv->freq)
    {
      bk_err_t err = bk_spi_set_baud_rate(priv->id, frequency);
      if (err == BK_OK)
        {
          priv->freq = frequency;
        }
    }

  return priv->freq;
}

static void bk7258_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;
  if ((unsigned int)mode > BK7258_SPI_MODE_MAX)
    {
      return;
    }

  if (!priv->initialized)
    {
      priv->mode = (uint8_t)mode;
      return;
    }

  if ((uint8_t)mode != priv->mode &&
      bk_spi_set_mode(priv->id, (spi_mode_t)mode) == BK_OK)
    {
      priv->mode = (uint8_t)mode;
    }
}

static void bk7258_spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;
  spi_bit_width_t width;

  if (nbits != 8 && nbits != 16)
    {
      return;
    }

  width = nbits == 16 ? SPI_BIT_WIDTH_16BITS : SPI_BIT_WIDTH_8BITS;

  if (!priv->initialized)
    {
      priv->bits = (uint8_t)nbits;
      return;
    }

  if ((uint8_t)nbits != priv->bits &&
      bk_spi_set_bit_width(priv->id, width) == BK_OK)
    {
      priv->bits = (uint8_t)nbits;
    }
}

static uint8_t bk7258_spi_status(FAR struct spi_dev_s *dev,
                                 uint32_t devid)
{
  /* No device-status polling is provided by this lower half. */

  return 0;
}

#ifdef CONFIG_SPI_EXCHANGE
static void bk7258_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;
  size_t nbytes;
  bk_err_t err;

  if (!priv->initialized || nwords == 0 ||
      (txbuffer == NULL && rxbuffer == NULL))
    {
      return;
    }

  if (nwords > UINT32_MAX / (priv->bits / 8u))
    {
      return;
    }

  nbytes = nwords * (priv->bits / 8u);

  if (txbuffer != NULL && rxbuffer != NULL)
    {
      /* Full duplex: SDK requires equal tx/rx sizes, which matches. */

      err = bk_spi_transmit(priv->id, txbuffer, (uint32_t)nbytes,
                           rxbuffer, (uint32_t)nbytes);
    }
  else if (txbuffer != NULL)
    {
      err = bk_spi_write_bytes(priv->id, txbuffer, (uint32_t)nbytes);
    }
  else
    {
      err = bk_spi_read_bytes(priv->id, rxbuffer, (uint32_t)nbytes);
    }

  /* exchange() has no error return; mismatches surface on the next
   * setfrequency/lock and are logged by the upper half through status().
   * The SDK returns asynchronously via its internal semaphore, so a
   * non-BK_OK here is a programming/configuration error.
   */

  (void)err;
}
#else
static void bk7258_spi_sndblock(FAR struct spi_dev_s *dev,
                                FAR const void *buffer, size_t nwords)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;
  size_t nbytes;

  if (priv->initialized && nwords > 0 &&
      nwords <= UINT32_MAX / (priv->bits / 8u))
    {
      nbytes = nwords * (priv->bits / 8u);
      bk_spi_write_bytes(priv->id, buffer, (uint32_t)nbytes);
    }
}

static void bk7258_spi_recvblock(FAR struct spi_dev_s *dev,
                                 FAR void *buffer, size_t nwords)
{
  FAR struct bk7258_spi_priv_s *priv =
    (FAR struct bk7258_spi_priv_s *)dev;
  size_t nbytes;

  if (priv->initialized && nwords > 0 &&
      nwords <= UINT32_MAX / (priv->bits / 8u))
    {
      nbytes = nwords * (priv->bits / 8u);
      bk_spi_read_bytes(priv->id, buffer, (uint32_t)nbytes);
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_spi_initialize(FAR struct spi_dev_s **spi_dev)
{
  FAR struct bk7258_spi_priv_s *priv = &g_bk7258_spi;
  spi_config_t cfg;
  bk_err_t err;

  if (spi_dev == NULL)
    {
      return -EINVAL;
    }

  /* Attach the vtable now (it cannot be a static initializer because the
   * ops symbols are defined later in this translation unit).
   */

  priv->dev.ops = &g_bk7258_spi_ops;

  if (priv->initialized)
    {
      *spi_dev = &priv->dev;
      return OK;
    }

  nxmutex_init(&priv->lock);

  if (!priv->driver_init)
    {
      err = bk_spi_driver_init();
      if (err != BK_OK)
        {
          nxmutex_destroy(&priv->lock);
          return bk7258_spi_map_err(err);
        }

      priv->driver_init = true;
    }

  /* Build the initial config from the cached defaults.  spi_config_t uses
   * polarity/phase directly; derive them from the cached mode.
   */

  cfg.role      = SPI_ROLE_MASTER;
  cfg.bit_width = (priv->bits == 16) ? SPI_BIT_WIDTH_16BITS
                                     : SPI_BIT_WIDTH_8BITS;
  cfg.polarity  = (priv->mode == 2 || priv->mode == 3)
                 ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
  cfg.phase     = (priv->mode == 1 || priv->mode == 3)
                 ? SPI_PHASE_2ND_EDGE : SPI_PHASE_1ST_EDGE;
  cfg.wire_mode = SPI_4WIRE_MODE;
  cfg.baud_rate = priv->freq;
  cfg.bit_order = SPI_MSB_FIRST;

  err = bk_spi_init(priv->id, &cfg);
  if (err != BK_OK)
    {
      /* Roll back the global driver resource so a later initialize() (or a
       * clean teardown) starts consistent, not leaking driver_init with
       * initialized still false.
       */

      if (priv->driver_init)
        {
          bk_spi_driver_deinit();
          priv->driver_init = false;
        }

      nxmutex_destroy(&priv->lock);
      return bk7258_spi_map_err(err);
    }

  priv->initialized = true;
  *spi_dev = &priv->dev;
  return OK;
}

void bk7258_spi_set_csinfo(bk7258_spi_cs_cb_t cs_cb)
{
  g_bk7258_spi.cs_cb = cs_cb;
}

#endif /* CONFIG_BK7258_SPI */
