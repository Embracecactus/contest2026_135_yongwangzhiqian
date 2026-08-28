/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/bk7258_i2c.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 I2C master — NuttX i2c_master_s lower-half over the
 * official Beken bk_i2c_* SDK API.  Zero register access.
 *
 * Role ownership: AP only.  bk_i2c_* (21 symbols) are compiled into the AP
 * libdriver.a exclusively; the CP bundle ships the headers but defines none
 * of the symbols, so this file is guarded by CONFIG_BK7258_AP_CORE.
 *
 * Data path:
 *
 *   NuttX i2c_transfer(i2c_msg_s[])
 *     -> bk_i2c_set_baud_rate(id, msg->frequency)
 *       -> bk_i2c_master_write / bk_i2c_master_read            (single seg)
 *       -> bk_i2c_memory_write / bk_i2c_memory_read            (reg+data)
 *         -> configured Beken hardware I2C unit
 *
 * Notes on SDK behaviour that shaped this driver (verified against the
 * v3.1.1.9 headers, not assumed):
 *
 *  1. bk_i2c_driver_init() is a global one-time resource init shared by all
 *     units; we call it once on setup and deinit once on shutdown.  It is
 *     safe to call bk_i2c_init(id, cfg) only after driver_init().
 *  2. The per-transfer SDK calls (bk_i2c_master_*) each issue their own
 *     START+STOP.  The SDK has no raw "hold bus / repeated-start" primitive
 *     exposed here, BUT bk_i2c_memory_read/write() performs the common
 *     "write-N-byte-register-address then (re)start read/write" combined
 *     transaction internally.  We therefore route the typical
 *     {write(reg), read(data)} pair through bk_i2c_memory_read/write.
 *  3. A sequence that requests NOSTOP/NOSTART but cannot be represented by
 *     bk_i2c_memory_read/write() is rejected with -ENOTSUP.  It must never
 *     be silently split into independent START/STOP transactions because
 *     that changes the wire protocol seen by the peripheral.
 *  4. dev_addr is the raw 7-bit slave address (unshifted), matching NuttX
 *     i2c_msg_s::addr.  Verified in the v3.1.1.9 sources: bk_i2c_master_read/
 *     write() do `dev_addr << 1 | rw` themselves (i2c_driver.c:374 / the write
 *     counterpart), so we pass the address unshifted.  NOTE: 10-bit addressing
 *     is NOT actually implemented by this SDK I2C driver (addr_mode is stored
 *     but no two-byte extended-address sequence is emitted), so any I2C_M_TEN
 *     message is rejected with -ENOTSUP rather than silently mis-addressing.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_I2C

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/i2c/i2c_master.h>

#include <arch/chip/bk7258_i2c.h>

/* SDK API headers (Beken).  bk_err_t / BK_OK come via common/bk_err.h. */

#include <driver/i2c.h>
#include <driver/i2c_types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_I2C_BUS
#  define CONFIG_BK7258_I2C_BUS        0
#endif

#ifndef CONFIG_BK7258_I2C_TIMEOUT_MS
#  define CONFIG_BK7258_I2C_TIMEOUT_MS BK7258_I2C_TIMEOUT_MS_DEFAULT
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_i2c_priv_s
{
  struct i2c_master_s dev;        /* NuttX lower-half vtable anchor */
  mutex_t lock;                   /* Serialize transfers on the unit */
  i2c_id_t id;                    /* Configured BK7258 I2C unit */
  uint32_t baud;                  /* Last baud rate applied */
  bool initialized;               /* bk_i2c_init() done for this unit */
  bool driver_init;               /* bk_i2c_driver_init() done */
  bool registered;                /* /dev/i2cN upper half registered */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_i2c_transfer(FAR struct i2c_master_s *dev,
                               FAR struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_I2C_RESET
static int bk7258_i2c_reset(FAR struct i2c_master_s *dev);
#endif
static int bk7258_i2c_setup(FAR struct i2c_master_s *dev);
static int bk7258_i2c_shutdown(FAR struct i2c_master_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2c_ops_s g_bk7258_i2c_ops =
{
  .transfer  = bk7258_i2c_transfer,
#ifdef CONFIG_I2C_RESET
  .reset     = bk7258_i2c_reset,
#endif
  .setup     = bk7258_i2c_setup,
  .shutdown  = bk7258_i2c_shutdown,
};

static struct bk7258_i2c_priv_s g_bk7258_i2c =
{
  .dev.ops  = &g_bk7258_i2c_ops,
  .lock     = NXMUTEX_INITIALIZER,
  .id       = (i2c_id_t)BK7258_I2C_UNIT,
  .baud     = BK7258_I2C_BAUD_RATE_DEFAULT,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_i2c_map_err
 *
 * Description:
 *   Convert a Beken bk_err_t into a negated errno.  BK_OK (0) maps to OK.
 ****************************************************************************/

static int bk7258_i2c_map_err(bk_err_t err)
{
  if (err == BK_OK)
    {
      return OK;
    }

  switch (err)
    {
      case BK_ERR_I2C_ACK_TIMEOUT:
        return -ENXIO;   /* No ACK from slave */
      case BK_ERR_I2C_SCL_TIMEOUT:
        return -ETIMEDOUT;
      case BK_ERR_I2C_NOT_INIT:
      case BK_ERR_I2C_ID_NOT_INIT:
        return -EAGAIN;
      case BK_ERR_I2C_INVALID_ID:
        return -EINVAL;
      case BK_ERR_I2C_SM_BUS_BUSY:
        return -EBUSY;
      default:
        return -EIO;
    }
}

/****************************************************************************
 * Name: bk7258_i2c_set_baud
 *
 * Description:
 *   Apply the requested bus speed if it differs from the last applied value.
 *   The NuttX I2C_SPEED_* and Beken I2C_BAUD_RATE_* constants share the same
 *   numeric kHz values, so msg->frequency passes through directly.
 ****************************************************************************/

static int bk7258_i2c_set_baud(FAR struct bk7258_i2c_priv_s *priv,
                               uint32_t frequency)
{
  bk_err_t err;

  if (frequency == 0)
    {
      frequency = BK7258_I2C_BAUD_RATE_DEFAULT;
    }

  if (frequency == priv->baud)
    {
      return OK;
    }

  err = bk_i2c_set_baud_rate(priv->id, frequency);
  if (err != BK_OK)
    {
      return bk7258_i2c_map_err(err);
    }

  priv->baud = frequency;
  return OK;
}

/****************************************************************************
 * Name: bk7258_i2c_transfer
 *
 * Description:
 *   Perform a sequence of I2C transfers.  See the file header note on how
 *   combined (repeated-start) transactions are mapped onto the SDK.
 ****************************************************************************/

static int bk7258_i2c_transfer(FAR struct i2c_master_s *dev,
                               FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct bk7258_i2c_priv_s *priv =
    (FAR struct bk7258_i2c_priv_s *)dev;
  int ret = OK;
  int i;

  if (msgs == NULL || count <= 0 || count > BK7258_I2C_MAX_MSG)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return -EAGAIN;
    }

  for (i = 0; i < count; i++)
    {
      FAR struct i2c_msg_s *msg = &msgs[i];
      bool is_read = (msg->flags & I2C_M_READ) != 0;

      if (msg->buffer == NULL || msg->length == 0)
        {
          ret = -EINVAL;
          break;
        }

      /* 10-bit addressing is not implemented by the Beken I2C driver
       * (see file header note 4); reject instead of mis-addressing.
       */

      if ((msg->flags & I2C_M_TEN) != 0)
        {
          ret = -ENOTSUP;
          break;
        }

      ret = bk7258_i2c_set_baud(priv, msg->frequency);
      if (ret < 0)
        {
          break;
        }

      /* Detect the common combined transaction:
       *   msg[i] = write with I2C_M_NOSTOP (register address)
       *   msg[i+1] = read  without I2C_M_NOSTART (data)
       * Route it through bk_i2c_memory_read(), which issues the proper
       * repeated-start internally.  Only 1- or 2-byte register prefixes are
       * supported by i2c_mem_addr_size_t.
       */

      if (!is_read && (i + 1) < count &&
          ((msg->flags & I2C_M_NOSTOP) != 0 ||
           (msgs[i + 1].flags & I2C_M_NOSTART) != 0) &&
          (msg->length == 1 || msg->length == 2))
        {
          i2c_mem_param_t mem;
          FAR struct i2c_msg_s *next = &msgs[i + 1];
          bool next_read = (next->flags & I2C_M_READ) != 0;

          if (next->buffer == NULL || next->length == 0 ||
              (next->flags & (I2C_M_TEN | I2C_M_NOSTOP)) != 0 ||
              next->addr != msg->addr ||
              (next->frequency != 0 && msg->frequency != 0 &&
               next->frequency != msg->frequency) ||
              (next_read && (next->flags & I2C_M_NOSTART) != 0) ||
              (!next_read && (next->flags & I2C_M_NOSTART) == 0))
            {
              ret = -ENOTSUP;
              break;
            }

          mem.dev_addr      = msg->addr;
          mem.mem_addr      = (msg->length == 2)
                              ? ((uint32_t)msg->buffer[0] << 8) |
                                (uint32_t)msg->buffer[1]
                              : (uint32_t)msg->buffer[0];
          mem.mem_addr_size = (msg->length == 2)
                              ? I2C_MEM_ADDR_SIZE_16BIT
                              : I2C_MEM_ADDR_SIZE_8BIT;
          mem.data          = next->buffer;
          mem.data_size     = (uint32_t)next->length;
          mem.timeout_ms    = CONFIG_BK7258_I2C_TIMEOUT_MS;

          ret = next_read
                ? bk7258_i2c_map_err(bk_i2c_memory_read(priv->id, &mem))
                : bk7258_i2c_map_err(bk_i2c_memory_write(priv->id, &mem));
          if (ret < 0)
            {
              break;
            }

          i++;   /* consume the paired read */
          continue;
        }

      /* The SDK single-segment calls always emit START and STOP. */

      if ((msg->flags & (I2C_M_NOSTOP | I2C_M_NOSTART)) != 0)
        {
          ret = -ENOTSUP;
          break;
        }

      /* Plain single-segment write or read. */

      if (is_read)
        {
          ret = bk7258_i2c_map_err(bk_i2c_master_read(
                                     priv->id, msg->addr, msg->buffer,
                                     (uint32_t)msg->length,
                                     CONFIG_BK7258_I2C_TIMEOUT_MS));
        }
      else
        {
          ret = bk7258_i2c_map_err(bk_i2c_master_write(
                                     priv->id, msg->addr, msg->buffer,
                                     (uint32_t)msg->length,
                                     CONFIG_BK7258_I2C_TIMEOUT_MS));
        }

      if (ret < 0)
        {
          break;
        }
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

#ifdef CONFIG_I2C_RESET
static int bk7258_i2c_reset(FAR struct i2c_master_s *dev)
{
  FAR struct bk7258_i2c_priv_s *priv =
    (FAR struct bk7258_i2c_priv_s *)dev;
  i2c_config_t cfg;
  bk_err_t err;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  err = bk_i2c_deinit(priv->id);
  if (err != BK_OK)
    {
      ret = bk7258_i2c_map_err(err);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  /* Deinit succeeded, so the old controller instance no longer exists.
   * Commit that fact before attempting to recreate it.  If init fails,
   * later setup() can recover instead of treating a dead controller as live.
   */

  priv->initialized = false;

#ifdef CONFIG_BK7258_I2C_ADDR_10BIT
  cfg.addr_mode = I2C_ADDR_MODE_10BIT;
#else
  cfg.addr_mode = I2C_ADDR_MODE_7BIT;
#endif
  cfg.baud_rate = priv->baud;
  cfg.slave_addr = 0;

  err = bk_i2c_init(priv->id, &cfg);
  if (err == BK_OK)
    {
      priv->initialized = true;
    }

  ret = bk7258_i2c_map_err(err);
  nxmutex_unlock(&priv->lock);
  return ret;
}
#endif

static int bk7258_i2c_setup(FAR struct i2c_master_s *dev)
{
  FAR struct bk7258_i2c_priv_s *priv =
    (FAR struct bk7258_i2c_priv_s *)dev;
  i2c_config_t cfg;
  bk_err_t err;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->initialized)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  if (!priv->driver_init)
    {
      err = bk_i2c_driver_init();
      if (err != BK_OK)
        {
          ret = bk7258_i2c_map_err(err);
          nxmutex_unlock(&priv->lock);
          return ret;
        }

      priv->driver_init = true;
    }

#ifdef CONFIG_BK7258_I2C_ADDR_10BIT
  cfg.addr_mode = I2C_ADDR_MODE_10BIT;
#else
  cfg.addr_mode = I2C_ADDR_MODE_7BIT;
#endif
  cfg.baud_rate  = priv->baud;
  cfg.slave_addr = 0;

  err = bk_i2c_init(priv->id, &cfg);
  if (err != BK_OK)
    {
      /* Roll back the global driver resource so a later setup() (or a clean
       * shutdown()) starts from a consistent state instead of leaking the
       * driver_init flag while initialized stays false.
       */

      if (priv->driver_init)
        {
          if (bk_i2c_driver_deinit() == BK_OK)
            {
              priv->driver_init = false;
            }
        }

      ret = bk7258_i2c_map_err(err);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->initialized = true;
  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_i2c_shutdown(FAR struct i2c_master_s *dev)
{
  FAR struct bk7258_i2c_priv_s *priv =
    (FAR struct bk7258_i2c_priv_s *)dev;
  bk_err_t err;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->initialized)
    {
      err = bk_i2c_deinit(priv->id);
      if (err != BK_OK)
        {
          ret = bk7258_i2c_map_err(err);
          nxmutex_unlock(&priv->lock);
          return ret;
        }

      priv->initialized = false;
    }

  if (priv->driver_init)
    {
      err = bk_i2c_driver_deinit();
      if (err != BK_OK)
        {
          ret = bk7258_i2c_map_err(err);
          nxmutex_unlock(&priv->lock);
          return ret;
        }

      priv->driver_init = false;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_i2c_initialize(void)
{
  int ret;

  if (g_bk7258_i2c.registered)
    {
      return OK;
    }

#ifdef CONFIG_I2C_DRIVER
  /* i2c_register() only publishes /dev/i2cN.  Its upper half calls setup()
   * on the first open and shutdown() on the last close, so registration
   * must not claim or initialize the hardware eagerly during AP boot.
   */

  ret = i2c_register(&g_bk7258_i2c.dev, CONFIG_BK7258_I2C_BUS);
  if (ret < 0)
    {
      return ret;
    }
#endif

  g_bk7258_i2c.registered = true;
  return OK;
}

#endif /* CONFIG_BK7258_I2C */
