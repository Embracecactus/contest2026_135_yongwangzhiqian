/****************************************************************************
 * board/bk7258/chip/ap/bk7258_trng.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP hardware TRNG adapter for the NuttX random-device contract.
 *
 * The BK7258 SDK v3.1.1.9 exposes a synchronous, byte-buffer API
 * (bk_fill_rand()).  NuttX's CONFIG_DEV_URANDOM_ARCH contract requires the
 * architecture-specific implementation to provide the complete
 * devrandom_register()/devurandom_register() path, so this lower half reads
 * directly from the SDK TRNG rather than treating a software PRNG as a
 * hardware source or injecting unrelated timing data into the entropy pool.
 *
 * The SDK TRNG is a global singleton.  Its init API is idempotent, while its
 * start/stop and data path are not documented as re-entrant.  A NuttX mutex
 * serializes all calls made through this adapter.  The adapter never calls
 * the global deinit API because the SDK startup path and other AP clients may
 * share the singleton and NuttX has no ownership-safe global shutdown point.
 *
 * bk_fill_rand() discards the SDK's documented startup samples, but it does
 * not perform a continuous output test.  The adapter therefore rejects two
 * identical adjacent 32-bit samples.  Besides matching established NuttX
 * hardware-TRNG practice, this detects a disabled or stuck BK7258 source
 * instead of reporting a buffer of constant data as a successful read.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_TRNG

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_trng.h>

#include <driver/trng.h>

#if defined(CONFIG_DEV_RANDOM) || defined(CONFIG_DEV_URANDOM_ARCH)
#  include <debug.h>
#  include <nuttx/drivers/drivers.h>
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_trng_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_trng_ready;
static bool g_bk7258_trng_have_last;
static uint32_t g_bk7258_trng_last;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_trng_map_error
 ****************************************************************************/

static int bk7258_trng_map_error(bk_err_t error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  switch (error)
    {
      case BK_ERR_TRNG_DRIVER_NOT_INIT:
      case BK_ERR_NOT_INIT:
      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case BK_ERR_BUSY:
        return -EBUSY;

      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Name: bk7258_trng_initialize_locked
 ****************************************************************************/

static int bk7258_trng_initialize_locked(void)
{
  bk_err_t error;

  if (g_bk7258_trng_ready)
    {
      return OK;
    }

  /* The board wrapper owns initialization lazily rather than relying on an
   * SDK driver_early_init() hook: NuttX's board integration may register the
   * random nodes before the SDK TRNG has been initialized.  The SDK documents
   * bk_trng_driver_init() as the required prerequisite and makes repeated
   * initialization successful, so read() retries this call while ready is
   * still false and the adapter mutex is held.
   */

  error = bk_trng_driver_init();
  if (error != BK_OK)
    {
      return bk7258_trng_map_error(error);
    }

  g_bk7258_trng_ready = true;
  return OK;
}

#if defined(CONFIG_DEV_RANDOM) || defined(CONFIG_DEV_URANDOM_ARCH)

/****************************************************************************
 * Name: bk7258_trng_check_locked
 ****************************************************************************/

static int bk7258_trng_check_locked(FAR const void *buffer, size_t buflen)
{
  FAR const uint8_t *bytes = buffer;
  uint32_t sample;
  size_t offset;

  DEBUGASSERT((buflen % sizeof(sample)) == 0);

  for (offset = 0; offset < buflen; offset += sizeof(sample))
    {
      memcpy(&sample, bytes + offset, sizeof(sample));

      if (g_bk7258_trng_have_last && sample == g_bk7258_trng_last)
        {
          return -EIO;
        }

      g_bk7258_trng_last = sample;
      g_bk7258_trng_have_last = true;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_trng_fill_locked
 ****************************************************************************/

static int bk7258_trng_fill_locked(FAR char *buffer, size_t buflen)
{
  uint32_t sample;
  size_t whole;
  size_t tail;
  bk_err_t error;
  int ret;

  whole = buflen & ~(sizeof(sample) - 1);
  tail = buflen - whole;

  if (whole > 0)
    {
      error = bk_fill_rand(buffer, whole);
      if (error != BK_OK)
        {
          return bk7258_trng_map_error(error);
        }

      ret = bk7258_trng_check_locked(buffer, whole);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* Generate a complete final sample even for a one-to-three byte request.
   * This keeps the continuous test effective across short reads instead of
   * comparing truncated output bytes.
   */

  if (tail > 0)
    {
      error = bk_fill_rand(&sample, sizeof(sample));
      if (error != BK_OK)
        {
          return bk7258_trng_map_error(error);
        }

      ret = bk7258_trng_check_locked(&sample, sizeof(sample));
      if (ret < 0)
        {
          return ret;
        }

      memcpy(buffer + whole, &sample, tail);
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_trng_read
 ****************************************************************************/

static ssize_t bk7258_trng_read(FAR struct file *filep,
                                FAR char *buffer, size_t buflen)
{
  bool nonblock;
  int ret;
  ssize_t result;

  if (buflen == 0)
    {
      return 0;
    }

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  nonblock = filep != NULL && (filep->f_oflags & O_NONBLOCK) != 0;
  ret = nonblock ? nxmutex_trylock(&g_bk7258_trng_lock) :
                   nxmutex_lock(&g_bk7258_trng_lock);
  if (ret < 0)
    {
      /* No data is consumed when a non-blocking caller cannot acquire the
       * singleton.  Report temporary unavailability as EAGAIN, matching the
       * random-device contract.
       */

      return nonblock ? -EAGAIN : ret;
    }

  ret = bk7258_trng_initialize_locked();
  if (ret < 0)
    {
      result = ret;
      goto out;
    }

  /* bk_fill_rand() has no partial-count return value, so the NuttX read is
   * likewise all-or-error.  The helper also applies the continuous output
   * test while this singleton lock is held.
   */

  ret = bk7258_trng_fill_locked(buffer, buflen);
  if (ret == OK)
    {
      result = (ssize_t)buflen;
    }
  else
    {
      result = ret;
    }

out:
  nxmutex_unlock(&g_bk7258_trng_lock);
  return result;
}

static const struct file_operations g_bk7258_trng_fops =
{
  .open  = NULL,
  .close = NULL,
  .read  = bk7258_trng_read,
};

/****************************************************************************
 * Name: bk7258_trng_register
 ****************************************************************************/

static int bk7258_trng_register(FAR const char *path,
                                FAR bool *registered)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_trng_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (*registered)
    {
      ret = OK;
      goto out;
    }

  /* Keep node registration independent of SDK availability.  The read path
   * owns lazy initialization and retries it after a transient failure.
   */

  ret = register_driver(path, &g_bk7258_trng_fops, 0444, NULL);
  if (ret >= 0)
    {
      *registered = true;
    }

out:
  nxmutex_unlock(&g_bk7258_trng_lock);
  return ret;
}

#ifdef CONFIG_DEV_RANDOM
static bool g_bk7258_trng_random_registered;
#endif

#ifdef CONFIG_DEV_URANDOM_ARCH
static bool g_bk7258_trng_urandom_registered;
#endif

#endif /* CONFIG_DEV_RANDOM || CONFIG_DEV_URANDOM_ARCH */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_trng_initialize(void)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_trng_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_trng_initialize_locked();
  nxmutex_unlock(&g_bk7258_trng_lock);
  return ret;
}

#ifdef CONFIG_DEV_RANDOM

/****************************************************************************
 * Name: devrandom_register
 ****************************************************************************/

void devrandom_register(void)
{
  int ret;

  ret = bk7258_trng_register("/dev/random",
                             &g_bk7258_trng_random_registered);
  if (ret < 0)
    {
      snerr("ERROR: BK7258 TRNG /dev/random registration failed: %d\n",
            ret);
    }
}

#endif /* CONFIG_DEV_RANDOM */

#ifdef CONFIG_DEV_URANDOM_ARCH

/****************************************************************************
 * Name: devurandom_register
 ****************************************************************************/

void devurandom_register(void)
{
  int ret;

  ret = bk7258_trng_register("/dev/urandom",
                             &g_bk7258_trng_urandom_registered);
  if (ret < 0)
    {
      snerr("ERROR: BK7258 TRNG /dev/urandom registration failed: %d\n",
            ret);
    }
}

#endif /* CONFIG_DEV_URANDOM_ARCH */

#endif /* CONFIG_BK7258_TRNG */
