/****************************************************************************
 * board/bk7258_t5ai/chip/cp/bk7258_touch.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP capacitive-touch buttons lower-half for the standard NuttX
 * buttons upper-half.
 *
 * The BK7258 touch controller is CP-owned in SDK v3.1.1.9.  This wrapper
 * consumes only the SDK's public CP touch API and maps the selected channel's
 * status bit to the NuttX button bitmask.  No touch character-device ABI is
 * introduced here.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_TOUCH

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/input/buttons.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_touch.h>

/* These are public CP SDK headers from v3.1.1.9. */

#include <common/bk_err.h>
#include <driver/touch.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_TOUCH_SENSITIVITY_MAX 3u
#define BK7258_TOUCH_THRESHOLD_MAX   7u
#define BK7258_TOUCH_RANGE_MAX       3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_touch_priv_s
{
  struct btn_lowerhalf_s lower;       /* NuttX buttons lower-half object */
  struct bk7258_touch_config_s config;
  uint32_t channel_mask;              /* One immutable bit while initialized */
  clock_t poll_ticks;                 /* NuttX work-queue polling period */
  uint32_t poll_state;                /* Last sampled selected channel */
  bool poll_state_valid;
  bool poll_active;                   /* A periodic LPWORK chain is live */
  bool initialized;
  bool sdk_enabled;
  btn_handler_t handler;              /* Upper-half callback */
  FAR void *handler_arg;
  btn_buttonset_t press_mask;         /* Requested press transitions */
  btn_buttonset_t release_mask;       /* Requested release transitions */
  int last_error;
  struct work_s poll_work;
  spinlock_t lock;                    /* ISR/task state protection */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static btn_buttonset_t
bk7258_touch_supported(FAR const struct btn_lowerhalf_s *lower);
static btn_buttonset_t
bk7258_touch_buttons(FAR const struct btn_lowerhalf_s *lower);
static void bk7258_touch_enable(FAR const struct btn_lowerhalf_s *lower,
                                btn_buttonset_t press,
                                btn_buttonset_t release,
                                btn_handler_t handler, FAR void *arg);
static void bk7258_touch_poll_work(FAR void *arg);

static int bk7258_touch_map_error(bk_err_t error);
static int bk7258_touch_validate_config(
  FAR const struct bk7258_touch_config_s *config);
static void bk7258_touch_set_error(
  FAR struct bk7258_touch_priv_s *priv, int error);
static int bk7258_touch_cleanup_locked(
  FAR struct bk7258_touch_priv_s *priv, uint32_t channel_mask,
  bool disable_sdk);
static int bk7258_touch_setup_locked(
  FAR struct bk7258_touch_priv_s *priv,
  FAR const struct bk7258_touch_config_s *config);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_touch_mutex = NXMUTEX_INITIALIZER;

static struct bk7258_touch_priv_s g_bk7258_touch =
{
  .lower =
  {
    .bl_supported = bk7258_touch_supported,
    .bl_buttons   = bk7258_touch_buttons,
    .bl_enable    = bk7258_touch_enable,
    .bl_write     = NULL,
  },
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_touch_map_error
 ****************************************************************************/

static int bk7258_touch_map_error(bk_err_t error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  switch (error)
    {
      case BK_ERR_TOUCH_ID:
      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
        return -EINVAL;

      case BK_ERR_NOT_INIT:
      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case BK_ERR_BUSY:
        return -EBUSY;

      case BK_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case BK_ERR_IN_PROGRESS:
        return -EINPROGRESS;

      case BK_ERR_NOT_FOUND:
      case BK_ERR_NO_DEV:
        return -ENODEV;

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Name: bk7258_touch_validate_config
 ****************************************************************************/

static int bk7258_touch_validate_config(
  FAR const struct bk7258_touch_config_s *config)
{
  if (config == NULL || config->channel_mask == 0 ||
      (config->channel_mask & ~BK7258_TOUCH_CHANNEL_MASK) != 0)
    {
      return -EINVAL;
    }

  /* touch_driver_v1_1.c leaves the public multi-channel setter as a BK_OK
   * no-op.  One lower-half instance therefore owns exactly one channel.
   */

  if ((config->channel_mask & (config->channel_mask - 1u)) != 0)
    {
      return -ENOTSUP;
    }

  if (config->poll_interval_ms == 0 ||
      config->sensitivity_level > BK7258_TOUCH_SENSITIVITY_MAX ||
      config->detect_threshold > BK7258_TOUCH_THRESHOLD_MAX ||
      config->detect_range > BK7258_TOUCH_RANGE_MAX)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_touch_poll_delay
 ****************************************************************************/

static clock_t bk7258_touch_poll_delay(uint32_t milliseconds)
{
  clock_t ticks = MSEC2TICK(milliseconds);

  return ticks > 0 ? ticks : 1;
}

/****************************************************************************
 * Name: bk7258_touch_set_error
 ****************************************************************************/

static void bk7258_touch_set_error(FAR struct bk7258_touch_priv_s *priv,
                                   int error)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  priv->last_error = error;
  spin_unlock_irqrestore(&priv->lock, flags);
}

/****************************************************************************
 * Name: bk7258_touch_cleanup_locked
 ****************************************************************************/

static int bk7258_touch_cleanup_locked(
  FAR struct bk7258_touch_priv_s *priv, uint32_t channel_mask,
  bool disable_sdk)
{
  int first_error = OK;
  int ret;
  irqstate_t flags;

  /* Stop polling before touching SDK state.  The object is static, so an
   * already-running worker cannot become a use-after-free.
   */

  flags = spin_lock_irqsave(&priv->lock);
  priv->handler = NULL;
  priv->handler_arg = NULL;
  priv->press_mask = 0;
  priv->release_mask = 0;
  priv->poll_state_valid = false;
  priv->poll_active = false;
  priv->sdk_enabled = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Initialization/deinitialization run in task context, so wait for an
   * already-running poll worker before powering down the SDK block. */

  (void)work_cancel_sync(LPWORK, &priv->poll_work);

  if (channel_mask != 0)
    {
      /* Keep the SDK IRQ disabled.  touch_driver_v1_1.c starts TIMER_ID1 from
       * its ISR on every touch, and that timer is already owned by the board
       * timer lower-half.
       */

      ret = bk7258_touch_map_error(
        bk_touch_int_enable((touch_channel_t)channel_mask, 0));
      if (ret < 0 && first_error == OK)
        {
          first_error = ret;
        }

      if (disable_sdk)
        {
          ret = bk7258_touch_map_error(bk_touch_disable());
          if (ret < 0 && first_error == OK)
            {
              first_error = ret;
            }
        }
    }

  /* The public API has no driver-deinit or GPIO-unmap operation.  The SDK
   * touch GPIO mux therefore remains owned by this board integration after
   * deinitialize; no false restoration is attempted here.
   */

  return first_error;
}

/****************************************************************************
 * Name: bk7258_touch_setup_locked
 ****************************************************************************/

static int bk7258_touch_setup_locked(
  FAR struct bk7258_touch_priv_s *priv,
  FAR const struct bk7258_touch_config_s *config)
{
  touch_config_t sdk_config;
  touch_channel_t channel = (touch_channel_t)config->channel_mask;
  bool sdk_enabled = false;
  bool enable_attempted = false;
  int ret;

  sdk_config.sensitivity_level =
    (touch_sensitivity_level_t)config->sensitivity_level;
  sdk_config.detect_threshold =
    (touch_detect_threshold_t)config->detect_threshold;
  sdk_config.detect_range = (touch_detect_range_t)config->detect_range;

  /* GPIO init is per channel in the v3.1.1.9 SDK.  A multi-bit value is
   * rejected above because its driver switch has no multi-channel GPIO case.
   */

  ret = bk7258_touch_map_error(bk_touch_gpio_init(channel));
  if (ret < 0)
    {
      goto fail;
    }

  /* Calibration calls SDK delay(), so initialization must be called from task
   * context.  This sequence follows the official CP touch example.
   */

  /* bk_touch_enable() selects the channel and powers the block.  Remember
   * that the call was attempted before evaluating its result: the immutable
   * SDK may have partially changed hardware before returning an error, so
   * rollback must still call bk_touch_disable().
   */

  enable_attempted = true;
  ret = bk7258_touch_map_error(bk_touch_enable(channel));
  if (ret < 0)
    {
      goto fail;
    }

  sdk_enabled = true;

  ret = bk7258_touch_map_error(bk_touch_config(&sdk_config));
  if (ret < 0)
    {
      goto fail;
    }

  if (config->calibrate)
    {
      ret = bk7258_touch_map_error(bk_touch_calibration_start());
      if (ret < 0)
        {
          goto fail;
        }
    }

  /* Leave the SDK interrupt disabled.  State changes are sampled by the
   * NuttX LPWORK queue, avoiding the SDK TIMER_ID1 release-timer conflict.
   */

  ret = bk7258_touch_map_error(bk_touch_int_enable(channel, 0));
  if (ret < 0)
    {
      goto fail;
    }

  priv->config = *config;
  priv->channel_mask = config->channel_mask;
  priv->poll_ticks = bk7258_touch_poll_delay(config->poll_interval_ms);
  priv->poll_state = 0;
  priv->poll_state_valid = false;
  priv->poll_active = false;
  priv->sdk_enabled = sdk_enabled;
  priv->handler = NULL;
  priv->handler_arg = NULL;
  priv->press_mask = 0;
  priv->release_mask = 0;
  priv->last_error = OK;
  priv->initialized = true;
  return OK;

fail:
  bk7258_touch_cleanup_locked(priv, config->channel_mask,
                              enable_attempted);
  return ret;
}

/****************************************************************************
 * Name: bk7258_touch_supported
 ****************************************************************************/

static btn_buttonset_t
bk7258_touch_supported(FAR const struct btn_lowerhalf_s *lower)
{
  FAR struct bk7258_touch_priv_s *priv =
    (FAR struct bk7258_touch_priv_s *)lower;
  irqstate_t flags;
  btn_buttonset_t supported;

  flags = spin_lock_irqsave(&priv->lock);
  supported = priv->initialized ? (btn_buttonset_t)priv->channel_mask : 0;
  spin_unlock_irqrestore(&priv->lock, flags);
  return supported;
}

/****************************************************************************
 * Name: bk7258_touch_buttons
 ****************************************************************************/

static btn_buttonset_t
bk7258_touch_buttons(FAR const struct btn_lowerhalf_s *lower)
{
  FAR struct bk7258_touch_priv_s *priv =
    (FAR struct bk7258_touch_priv_s *)lower;
  irqstate_t flags;
  uint32_t channel_mask;
  bool initialized;

  /* The SDK getter is a direct status read.  No mutex or other blocking
   * operation is allowed on the NuttX buttons sampling path.
   */

  flags = spin_lock_irqsave(&priv->lock);
  initialized = priv->initialized;
  channel_mask = priv->channel_mask;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (!initialized)
    {
      return 0;
    }

  return (btn_buttonset_t)(bk_touch_get_touch_status() & channel_mask);
}

/****************************************************************************
 * Name: bk7258_touch_enable
 ****************************************************************************/

static void bk7258_touch_enable(FAR const struct btn_lowerhalf_s *lower,
                                btn_buttonset_t press,
                                btn_buttonset_t release,
                                btn_handler_t handler, FAR void *arg)
{
  FAR struct bk7258_touch_priv_s *priv =
    (FAR struct bk7258_touch_priv_s *)lower;
  btn_buttonset_t events = press | release;
  uint32_t channel_mask;
  uint32_t initial_state;
  bool initialized;
  int ret;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  initialized = priv->initialized;
  channel_mask = priv->channel_mask;

  if (!initialized)
    {
      priv->handler = NULL;
      priv->handler_arg = NULL;
      priv->press_mask = 0;
      priv->release_mask = 0;
      priv->poll_active = false;
      priv->poll_state_valid = false;
      priv->last_error = -EAGAIN;
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  if ((events & ~channel_mask) != 0)
    {
      priv->handler = NULL;
      priv->handler_arg = NULL;
      priv->press_mask = 0;
      priv->release_mask = 0;
      priv->poll_state_valid = false;
      priv->poll_active = false;
      priv->last_error = -EINVAL;
      spin_unlock_irqrestore(&priv->lock, flags);

      (void)work_cancel(LPWORK, &priv->poll_work);
      return;
    }

  if (events == 0 || handler == NULL)
    {
      priv->handler = NULL;
      priv->handler_arg = NULL;
      priv->press_mask = 0;
      priv->release_mask = 0;
      priv->poll_state_valid = false;
      priv->poll_active = false;
      priv->last_error = OK;
      spin_unlock_irqrestore(&priv->lock, flags);

      (void)work_cancel(LPWORK, &priv->poll_work);
      return;
    }

  /* The SDK IRQ was disabled during initialization and remains owned by this
   * lower-half.  A repeated enable only changes the NuttX subscription; it
   * must not restart the polling work item or issue SDK calls under the
   * spinlock.
   */

  if (priv->poll_active)
    {
      priv->handler = handler;
      priv->handler_arg = arg;
      priv->press_mask = press;
      priv->release_mask = release;
      priv->last_error = OK;
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  priv->handler = handler;
  priv->handler_arg = arg;
  priv->press_mask = press;
  priv->release_mask = release;
  priv->poll_active = true;
  priv->poll_state_valid = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Establish the baseline before polling so registration does not synthesize
   * a press for a channel that was already touched.
   */

  initial_state = bk_touch_get_touch_status() & channel_mask;

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->initialized || !priv->poll_active)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  priv->poll_state = initial_state;
  priv->poll_state_valid = true;
  priv->last_error = OK;
  spin_unlock_irqrestore(&priv->lock, flags);

  ret = work_queue(LPWORK, &priv->poll_work, bk7258_touch_poll_work,
                   priv, 0);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&priv->lock);
      if (priv->poll_active)
        {
          priv->handler = NULL;
          priv->handler_arg = NULL;
          priv->press_mask = 0;
          priv->release_mask = 0;
          priv->poll_state_valid = false;
          priv->poll_active = false;
          priv->last_error = ret;
        }
      spin_unlock_irqrestore(&priv->lock, flags);
      (void)work_cancel(LPWORK, &priv->poll_work);
    }
}

/****************************************************************************
 * Name: bk7258_touch_poll_work
 ****************************************************************************/

static void bk7258_touch_poll_work(FAR void *arg)
{
  FAR struct bk7258_touch_priv_s *priv =
    (FAR struct bk7258_touch_priv_s *)arg;
  btn_handler_t handler = NULL;
  FAR void *handler_arg = NULL;
  btn_buttonset_t press_mask;
  btn_buttonset_t release_mask;
  btn_buttonset_t change;
  btn_buttonset_t press;
  btn_buttonset_t release;
  uint32_t previous;
  uint32_t channel_mask;
  uint32_t sample;
  clock_t delay;
  bool reschedule;
  int ret;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->initialized || !priv->poll_active || priv->handler == NULL)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return;
    }

  channel_mask = priv->channel_mask;
  delay = priv->poll_ticks;
  spin_unlock_irqrestore(&priv->lock, flags);

  sample = bk_touch_get_touch_status() & channel_mask;

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->initialized && priv->poll_active && priv->handler != NULL)
    {
      if (!priv->poll_state_valid)
        {
          priv->poll_state = sample;
          priv->poll_state_valid = true;
        }
      else if (sample != priv->poll_state)
        {
          previous = priv->poll_state;
          change = sample ^ previous;
          press = change & sample;
          release = change & ~sample & priv->channel_mask;
          press_mask = priv->press_mask;
          release_mask = priv->release_mask;

          priv->poll_state = sample;

          if ((press & press_mask) != 0 ||
              (release & release_mask) != 0)
            {
              handler = priv->handler;
              handler_arg = priv->handler_arg;
            }
        }
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (handler != NULL)
    {
      /* This runs on LPWORK, never in the SDK ISR.  NuttX upper-half then
       * samples bl_buttons() and performs its normal press/release filtering.
       */

      handler(&priv->lower, handler_arg);
    }

  /* A handler may disable or replace the subscription.  Re-evaluate the
   * chain state immediately before rescheduling so a disabled chain cannot
   * resurrect itself.
   */

  flags = spin_lock_irqsave(&priv->lock);
  reschedule = priv->initialized && priv->poll_active &&
               priv->handler != NULL;
  delay = priv->poll_ticks;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (reschedule)
    {
      ret = work_queue(LPWORK, &priv->poll_work, bk7258_touch_poll_work,
                       priv, delay);
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&priv->lock);
          if (priv->poll_active)
            {
              priv->handler = NULL;
              priv->handler_arg = NULL;
              priv->press_mask = 0;
              priv->release_mask = 0;
              priv->poll_state_valid = false;
              priv->poll_active = false;
              priv->last_error = ret;
            }
          spin_unlock_irqrestore(&priv->lock, flags);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_touch_initialize
 ****************************************************************************/

int bk7258_touch_initialize(FAR struct btn_lowerhalf_s **lower,
                            FAR const struct bk7258_touch_config_s *config)
{
  FAR struct bk7258_touch_priv_s *priv = &g_bk7258_touch;
  int ret;
  int lockret;

  if (lower == NULL)
    {
      return -EINVAL;
    }

  *lower = NULL;
  ret = bk7258_touch_validate_config(config);
  if (ret < 0)
    {
      return ret;
    }

  lockret = nxmutex_lock(&g_bk7258_touch_mutex);
  if (lockret < 0)
    {
      return lockret;
    }

  if (priv->initialized)
    {
      if (priv->channel_mask == config->channel_mask &&
          priv->config.poll_interval_ms == config->poll_interval_ms &&
          priv->config.sensitivity_level == config->sensitivity_level &&
          priv->config.detect_threshold == config->detect_threshold &&
          priv->config.detect_range == config->detect_range &&
          priv->config.calibrate == config->calibrate)
        {
          *lower = &priv->lower;
          ret = OK;
        }
      else
        {
          ret = -EBUSY;
        }

      nxmutex_unlock(&g_bk7258_touch_mutex);
      return ret;
    }

  ret = bk7258_touch_setup_locked(priv, config);
  if (ret == OK)
    {
      *lower = &priv->lower;
    }
  else
    {
      bk7258_touch_set_error(priv, ret);
    }

  nxmutex_unlock(&g_bk7258_touch_mutex);
  return ret;
}

/****************************************************************************
 * Name: bk7258_touch_deinitialize
 ****************************************************************************/

int bk7258_touch_deinitialize(void)
{
  FAR struct bk7258_touch_priv_s *priv = &g_bk7258_touch;
  uint32_t channel_mask;
  bool sdk_enabled;
  int lockret;
  int ret;
  irqstate_t flags;

  lockret = nxmutex_lock(&g_bk7258_touch_mutex);
  if (lockret < 0)
    {
      return lockret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->initialized)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxmutex_unlock(&g_bk7258_touch_mutex);
      return OK;
    }

  channel_mask = priv->channel_mask;
  sdk_enabled = priv->sdk_enabled;
  priv->initialized = false;
  priv->channel_mask = 0;
  spin_unlock_irqrestore(&priv->lock, flags);

  ret = bk7258_touch_cleanup_locked(priv, channel_mask, sdk_enabled);

  flags = spin_lock_irqsave(&priv->lock);
  priv->config.channel_mask = 0;
  priv->sdk_enabled = false;
  priv->last_error = ret;
  spin_unlock_irqrestore(&priv->lock, flags);

  nxmutex_unlock(&g_bk7258_touch_mutex);
  return ret;
}

/****************************************************************************
 * Name: bk7258_touch_last_error
 ****************************************************************************/

int bk7258_touch_last_error(void)
{
  FAR struct bk7258_touch_priv_s *priv = &g_bk7258_touch;
  irqstate_t flags;
  int error;

  flags = spin_lock_irqsave(&priv->lock);
  error = priv->last_error;
  spin_unlock_irqrestore(&priv->lock, flags);
  return error;
}

#endif /* CONFIG_BK7258_TOUCH */
