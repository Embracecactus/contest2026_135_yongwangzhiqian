/****************************************************************************
 * chips/bk7258/ap/bk7258_gpioe.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 general-purpose multi-pin GPIO — NuttX ioexpander
 * lower-half.
 *
 * Exposes BK7258 GPIO0..GPIO(CONFIG_BK7258_GPIOE_NPINS-1) through the
 * standard NuttX ioexpander interface.  Use gpio_lower_half() (drivers/
 * ioexpander/gpio_lower_half.c) to publish individual pins as /dev/gpioN,
 * or drive pins directly with the IOEXP_* helpers.
 *
 * AP role — see bk7258_gpioe.h for the SDK evidence.  On the AP core the
 * SDK's bk_gpio_driver_init() registers the GPIO ISR and enables CPU
 * forwarding, and bk_gpio_register_isr() gives exact per-pin dispatch
 * through the SDK's s_gpio_isr[] table.
 *
 * ioexpander -> SDK mapping:
 *   ioe_direction:
 *     IOEXPANDER_DIRECTION_IN           -> GPIO_INPUT_ENABLE,  pull disabled
 *     IOEXPANDER_DIRECTION_IN_PULLUP    -> GPIO_INPUT_ENABLE,  pull up
 *     IOEXPANDER_DIRECTION_IN_PULLDOWN  -> GPIO_INPUT_ENABLE,  pull down
 *     IOEXPANDER_DIRECTION_OUT          -> GPIO_OUTPUT_ENABLE, pull disabled
 *   ioe_option:
 *     IOEXPANDER_OPTION_INVERT          -> stored, XOR'ed on read/write
 *     IOEXPANDER_OPTION_INTCFG          -> bk_gpio_set_interrupt_type()
 *     IOEXPANDER_OPTION_WAKEUPCFG       -> bk_gpio_register_wakeup_source()
 *       using the last explicit interrupt polarity for the pin
 *   ioe_writepin / ioe_readpin / ioe_readbuf -> bk_gpio_set_output_value /
 *       bk_gpio_get_input() with invert applied
 *   ioe_attach / ioe_detach -> per-pin bk_gpio_register_isr() slots
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_GPIOE

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nuttx/ioexpander/ioexpander.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_gpioe.h>

#include <driver/gpio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_GPIOE_NPINS
#  define CONFIG_BK7258_GPIOE_NPINS     16
#endif

/* ioexpander pins are 1:1 with gpio_id_t GPIO_N, and the SDK upper bound
 * for a pin index is SOC_GPIO_NUM - 1.  Guard the Kconfig limit.
 */

#define BK7258_GPIOE_MAX_PINS            47
#if CONFIG_BK7258_GPIOE_NPINS > BK7258_GPIOE_MAX_PINS
#  error "CONFIG_BK7258_GPIOE_NPINS exceeds SOC_GPIO_NUM (48)"
#endif

/* The ioexpander bitmap (ioe_pinset_t) has CONFIG_IOEXPANDER_NPINS bits;
 * pin indexes above that would shift out of the type (UB).  Require the
 * exposed pin count not to exceed the ioexpander width so the bitmap
 * operations below stay well defined.
 */

#if CONFIG_BK7258_GPIOE_NPINS > CONFIG_IOEXPANDER_NPINS
#  error "CONFIG_BK7258_GPIOE_NPINS must not exceed CONFIG_IOEXPANDER_NPINS"
#endif

/* Per-pin SDK ISR handler signature: void (*)(gpio_id_t). */

typedef void (*bk7258_gpioe_sdk_isr_t)(gpio_id_t);

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* One slot per pin for the SDK ISR dispatch table.  Each registered pin
 * gets its own SDK handler that forwards to the ioexpander callback.
 */

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
struct bk7258_gpioe_isr_s
{
  ioe_callback_t callback;          /* ioexpander upper-half callback */
  FAR void *arg;                    /* callback argument */
  bool active;                      /* attach()ed and not detach()ed */
};
#endif

struct bk7258_gpioe_priv_s
{
  struct ioexpander_dev_s dev;      /* ioexpander anchor (ops first) */
  mutex_t lock;                     /* serialize pin config access */
  uint8_t npins;                    /* exposed pin count */
  bool driver_inited;               /* bk_gpio_driver_init() done */
  bool invert[CONFIG_BK7258_GPIOE_NPINS];   /* per-pin invert flag */
  bool intcfg_valid[CONFIG_BK7258_GPIOE_NPINS];
  gpio_int_type_t intcfg[CONFIG_BK7258_GPIOE_NPINS];
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  struct bk7258_gpioe_isr_s isr[CONFIG_BK7258_GPIOE_NPINS]; /* ISR slots */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_gpioe_direction(FAR struct ioexpander_dev_s *dev,
                                  uint8_t pin, int direction);
static int bk7258_gpioe_option(FAR struct ioexpander_dev_s *dev,
                               uint8_t pin, int opt, FAR void *val);
static int bk7258_gpioe_writepin(FAR struct ioexpander_dev_s *dev,
                                 uint8_t pin, bool value);
static int bk7258_gpioe_readpin(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value);
static int bk7258_gpioe_readbuf(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value);
#ifdef CONFIG_IOEXPANDER_MULTIPIN
static int bk7258_gpioe_multiwritepin(FAR struct ioexpander_dev_s *dev,
                                      FAR const uint8_t *pins,
                                      FAR const bool *values, int count);
static int bk7258_gpioe_multireadpin(FAR struct ioexpander_dev_s *dev,
                                     FAR const uint8_t *pins,
                                     FAR bool *values, int count);
static int bk7258_gpioe_multireadbuf(FAR struct ioexpander_dev_s *dev,
                                     FAR const uint8_t *pins,
                                     FAR bool *values, int count);
#endif
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static FAR void *bk7258_gpioe_attach(FAR struct ioexpander_dev_s *dev,
                                     ioe_pinset_t pinset,
                                     ioe_callback_t callback, FAR void *arg);
static int bk7258_gpioe_detach(FAR struct ioexpander_dev_s *dev,
                               FAR void *handle);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct ioexpander_ops_s g_bk7258_gpioe_ops =
{
  .ioe_direction = bk7258_gpioe_direction,
  .ioe_option    = bk7258_gpioe_option,
  .ioe_writepin  = bk7258_gpioe_writepin,
  .ioe_readpin   = bk7258_gpioe_readpin,
  .ioe_readbuf   = bk7258_gpioe_readbuf,
#ifdef CONFIG_IOEXPANDER_MULTIPIN
  .ioe_multiwritepin = bk7258_gpioe_multiwritepin,
  .ioe_multireadpin  = bk7258_gpioe_multireadpin,
  .ioe_multireadbuf  = bk7258_gpioe_multireadbuf,
#endif
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  .ioe_attach    = bk7258_gpioe_attach,
  .ioe_detach    = bk7258_gpioe_detach,
#endif
};

static struct bk7258_gpioe_priv_s g_bk7258_gpioe =
{
  .dev.ops       = &g_bk7258_gpioe_ops,
  .lock          = NXMUTEX_INITIALIZER,
  .npins         = CONFIG_BK7258_GPIOE_NPINS,
  .driver_inited = false,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_gpioe_ensure_driver
 *
 * Lazily run the SDK GPIO driver init (idempotent in the SDK).  On the AP
 * core this also enables GPIO interrupt forwarding to the CPU.
 ****************************************************************************/

static int bk7258_gpioe_ensure_driver(FAR struct bk7258_gpioe_priv_s *priv)
{
  bk_err_t ret;

  if (priv->driver_inited)
    {
      return OK;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  priv->driver_inited = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_gpioe_check_pin
 ****************************************************************************/

static int bk7258_gpioe_check_pin(FAR struct bk7258_gpioe_priv_s *priv,
                                  uint8_t pin)
{
  if (pin >= priv->npins)
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_gpioe_direction
 ****************************************************************************/

static int bk7258_gpioe_direction(FAR struct ioexpander_dev_s *dev,
                                  uint8_t pin, int direction)
{
  FAR struct bk7258_gpioe_priv_s *priv =
    (FAR struct bk7258_gpioe_priv_s *)dev;
  gpio_config_t cfg;
  bk_err_t ret;
  int rc;

  rc = bk7258_gpioe_check_pin(priv, pin);
  if (rc < 0)
    {
      return rc;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  rc = bk7258_gpioe_ensure_driver(priv);
  if (rc < 0)
    {
      goto out;
    }

  memset(&cfg, 0, sizeof(cfg));
  cfg.func_mode = GPIO_SECOND_FUNC_DISABLE;
  cfg.pull_mode = GPIO_PULL_DISABLE;

  switch (direction)
    {
      case IOEXPANDER_DIRECTION_IN:
        cfg.io_mode = GPIO_INPUT_ENABLE;
        break;

      case IOEXPANDER_DIRECTION_IN_PULLUP:
        cfg.io_mode = GPIO_INPUT_ENABLE;
        cfg.pull_mode = GPIO_PULL_UP_EN;
        break;

      case IOEXPANDER_DIRECTION_IN_PULLDOWN:
        cfg.io_mode = GPIO_INPUT_ENABLE;
        cfg.pull_mode = GPIO_PULL_DOWN_EN;
        break;

      case IOEXPANDER_DIRECTION_OUT_OPENDRAIN:
      case IOEXPANDER_DIRECTION_OUT_LED:
        /* The SDK API used here exposes neither open-drain drive nor LED
         * current semantics.  Silently selecting push-pull can damage a
         * shared bus, so unsupported electrical modes must fail closed.
         */

        rc = -ENOTSUP;
        goto out;

      case IOEXPANDER_DIRECTION_OUT:
        cfg.io_mode = GPIO_OUTPUT_ENABLE;
        break;

      default:
        rc = -EINVAL;
        goto out;
    }

  ret = bk_gpio_set_config((gpio_id_t)pin, &cfg);
  rc = (ret == BK_OK) ? OK : -EIO;

out:
  nxmutex_unlock(&priv->lock);
  return rc;
}

/****************************************************************************
 * Name: bk7258_gpioe_option
 ****************************************************************************/

static int bk7258_gpioe_option(FAR struct ioexpander_dev_s *dev,
                               uint8_t pin, int opt, FAR void *val)
{
  FAR struct bk7258_gpioe_priv_s *priv =
    (FAR struct bk7258_gpioe_priv_s *)dev;
  uintptr_t ival = (uintptr_t)val;
  bk_err_t ret;
  int rc;

  rc = bk7258_gpioe_check_pin(priv, pin);
  if (rc < 0)
    {
      return rc;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  rc = bk7258_gpioe_ensure_driver(priv);
  if (rc < 0)
    {
      goto out;
    }

  switch (opt)
    {
      case IOEXPANDER_OPTION_INVERT:
        priv->invert[pin] = (ival == IOEXPANDER_VAL_INVERT);
        break;

      case IOEXPANDER_OPTION_INTCFG:
        switch (ival)
          {
            case IOEXPANDER_VAL_DISABLE:
              ret = bk_gpio_disable_interrupt((gpio_id_t)pin);
              break;

            case IOEXPANDER_VAL_HIGH:
              ret = bk_gpio_set_interrupt_type((gpio_id_t)pin,
                                               GPIO_INT_TYPE_HIGH_LEVEL);
              if (ret == BK_OK)
                {
                  priv->intcfg[pin] = GPIO_INT_TYPE_HIGH_LEVEL;
                  priv->intcfg_valid[pin] = true;
                }
              break;

            case IOEXPANDER_VAL_LOW:
              ret = bk_gpio_set_interrupt_type((gpio_id_t)pin,
                                               GPIO_INT_TYPE_LOW_LEVEL);
              if (ret == BK_OK)
                {
                  priv->intcfg[pin] = GPIO_INT_TYPE_LOW_LEVEL;
                  priv->intcfg_valid[pin] = true;
                }
              break;

            case IOEXPANDER_VAL_RISING:
              ret = bk_gpio_set_interrupt_type((gpio_id_t)pin,
                                               GPIO_INT_TYPE_RISING_EDGE);
              if (ret == BK_OK)
                {
                  priv->intcfg[pin] = GPIO_INT_TYPE_RISING_EDGE;
                  priv->intcfg_valid[pin] = true;
                }
              break;

            case IOEXPANDER_VAL_FALLING:
              ret = bk_gpio_set_interrupt_type((gpio_id_t)pin,
                                               GPIO_INT_TYPE_FALLING_EDGE);
              if (ret == BK_OK)
                {
                  priv->intcfg[pin] = GPIO_INT_TYPE_FALLING_EDGE;
                  priv->intcfg_valid[pin] = true;
                }
              break;

            case IOEXPANDER_VAL_BOTH:
              rc = -ENOTSUP;
              goto out;

            case IOEXPANDER_VAL_LEVEL:
            case IOEXPANDER_VAL_EDGE:
              /* These values omit polarity and cannot be mapped without
               * inventing a trigger mode.
               */

              rc = -EINVAL;
              goto out;

            default:
              rc = -EINVAL;
              goto out;
          }

        if (ret != BK_OK)
          {
            rc = -EIO;
          }
        break;

      case IOEXPANDER_OPTION_WAKEUPCFG:
        /* Wake-source ownership belongs to the CP power-management domain.
         * The AP-side SDK helpers issue a synchronous GPIO IPC request and
         * wait forever when no matching CP wake-source service is present.
         * Do not let the generic NuttX GPIO upper half block AP bring-up.
         */

        rc = -ENOTSUP;
        goto out;

      default:
        rc = -ENOTSUP;
        break;
    }

out:
  nxmutex_unlock(&priv->lock);
  return rc;
}

/****************************************************************************
 * Name: bk7258_gpioe_writepin
 ****************************************************************************/

static int bk7258_gpioe_writepin(FAR struct ioexpander_dev_s *dev,
                                 uint8_t pin, bool value)
{
  FAR struct bk7258_gpioe_priv_s *priv =
    (FAR struct bk7258_gpioe_priv_s *)dev;
  bk_err_t ret;
  int rc;

  rc = bk7258_gpioe_check_pin(priv, pin);
  if (rc < 0)
    {
      return rc;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  rc = bk7258_gpioe_ensure_driver(priv);
  if (rc < 0)
    {
      goto out;
    }

  if (priv->invert[pin])
    {
      value = !value;
    }

  ret = bk_gpio_set_output_value((gpio_id_t)pin, value);
  rc = (ret == BK_OK) ? OK : -EIO;

out:
  nxmutex_unlock(&priv->lock);
  return rc;
}

/****************************************************************************
 * Name: bk7258_gpioe_readpin
 ****************************************************************************/

static int bk7258_gpioe_readpin(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value)
{
  FAR struct bk7258_gpioe_priv_s *priv =
    (FAR struct bk7258_gpioe_priv_s *)dev;
  int rc;

  rc = bk7258_gpioe_check_pin(priv, pin);
  if (rc < 0)
    {
      return rc;
    }

  if (value == NULL)
    {
      return -EINVAL;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  rc = bk7258_gpioe_ensure_driver(priv);
  if (rc < 0)
    {
      goto out;
    }

  *value = (bk_gpio_get_input((gpio_id_t)pin) != 0);
  if (priv->invert[pin])
    {
      *value = !*value;
    }

out:
  nxmutex_unlock(&priv->lock);
  return rc;
}

/****************************************************************************
 * Name: bk7258_gpioe_readbuf
 *
 * Same as readpin for BK7258: there is no hardware input buffer or latch
 * distinct from the live pin level.
 ****************************************************************************/

static int bk7258_gpioe_readbuf(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, FAR bool *value)
{
  return bk7258_gpioe_readpin(dev, pin, value);
}

#ifdef CONFIG_IOEXPANDER_MULTIPIN
static int bk7258_gpioe_multiwritepin(FAR struct ioexpander_dev_s *dev,
                                      FAR const uint8_t *pins,
                                      FAR const bool *values, int count)
{
  int ret;
  int i;

  if (pins == NULL || values == NULL || count < 0)
    {
      return -EINVAL;
    }

  for (i = 0; i < count; i++)
    {
      ret = bk7258_gpioe_writepin(dev, pins[i], values[i]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int bk7258_gpioe_multireadpin(FAR struct ioexpander_dev_s *dev,
                                     FAR const uint8_t *pins,
                                     FAR bool *values, int count)
{
  int ret;
  int i;

  if (pins == NULL || values == NULL || count < 0)
    {
      return -EINVAL;
    }

  for (i = 0; i < count; i++)
    {
      ret = bk7258_gpioe_readpin(dev, pins[i], &values[i]);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

static int bk7258_gpioe_multireadbuf(FAR struct ioexpander_dev_s *dev,
                                     FAR const uint8_t *pins,
                                     FAR bool *values, int count)
{
  return bk7258_gpioe_multireadpin(dev, pins, values, count);
}
#endif

/****************************************************************************
 * Name: bk7258_gpioe_sdk_isr
 *
 * SDK per-pin ISR handler.  bk_gpio_register_isr() dispatches exactly to
 * the slot for the triggering pin, so we look up that slot and forward to
 * the ioexpander callback.
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static void bk7258_gpioe_sdk_isr(gpio_id_t gpio_id)
{
  FAR struct bk7258_gpioe_priv_s *priv = &g_bk7258_gpioe;
  FAR struct bk7258_gpioe_isr_s *slot;
  ioe_callback_t callback;
  FAR void *arg;
  irqstate_t flags;

  if (gpio_id >= priv->npins)
    {
      return;
    }

  /* The slot may be torn down concurrently by attach()/detach() in task
   * context, so read it under a critical section.  The callback is
   * invoked outside the critical section to avoid re-entrancy issues.
   */

  slot = &priv->isr[gpio_id];

  flags = enter_critical_section();
  callback = slot->active ? slot->callback : NULL;
  arg      = slot->arg;
  leave_critical_section(flags);

  if (callback != NULL)
    {
      callback(&priv->dev, ((ioe_pinset_t)1 << gpio_id), arg);
    }
}
#endif

/****************************************************************************
 * Name: bk7258_gpioe_attach
 *
 * pinset is a bitmap: bit N selects GPIO_N.  Register the shared SDK
 * handler for each selected pin and record the callback.
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static FAR void *bk7258_gpioe_attach(FAR struct ioexpander_dev_s *dev,
                                     ioe_pinset_t pinset,
                                     ioe_callback_t callback, FAR void *arg)
{
  FAR struct bk7258_gpioe_priv_s *priv =
    (FAR struct bk7258_gpioe_priv_s *)dev;
  uint8_t pin;
  ioe_pinset_t attached = 0;
  ioe_pinset_t remaining = pinset;
  int rc;

  if (pinset == 0 || callback == NULL)
    {
      return NULL;
    }

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return NULL;
    }

  rc = bk7258_gpioe_ensure_driver(priv);
  if (rc < 0)
    {
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  for (pin = 0; pin < priv->npins; pin++)
    {
      irqstate_t flags;

      if ((pinset & ((ioe_pinset_t)1 << pin)) == 0)
        {
          continue;
        }

      remaining &= ~((ioe_pinset_t)1 << pin);

      flags = enter_critical_section();
      priv->isr[pin].callback = callback;
      priv->isr[pin].arg      = arg;
      priv->isr[pin].active   = true;
      leave_critical_section(flags);

      if (bk_gpio_register_isr((gpio_id_t)pin,
                               bk7258_gpioe_sdk_isr) != BK_OK)
        {
          flags = enter_critical_section();
          priv->isr[pin].active = false;
          leave_critical_section(flags);
          rc = -EIO;
          break;
        }
      else if (bk_gpio_enable_interrupt((gpio_id_t)pin) != BK_OK)
        {
          /* Roll back the ISR slot so the pin reports no callback. */
          flags = enter_critical_section();
          priv->isr[pin].active = false;
          priv->isr[pin].callback = NULL;
          priv->isr[pin].arg = NULL;
          leave_critical_section(flags);
          (void)bk_gpio_register_isr((gpio_id_t)pin, NULL);
          rc = -EIO;
          break;
        }

      else
        {
          attached |= (ioe_pinset_t)1 << pin;
        }
    }

  if (remaining != 0)
    {
      rc = -EINVAL;
    }

  if (rc < 0)
    {
      for (pin = 0; pin < priv->npins; pin++)
        {
          irqstate_t flags;

          if ((attached & ((ioe_pinset_t)1 << pin)) == 0)
            {
              continue;
            }

          (void)bk_gpio_disable_interrupt((gpio_id_t)pin);
          (void)bk_gpio_register_isr((gpio_id_t)pin, NULL);
          flags = enter_critical_section();
          priv->isr[pin].active = false;
          priv->isr[pin].callback = NULL;
          priv->isr[pin].arg = NULL;
          leave_critical_section(flags);
        }
    }

  nxmutex_unlock(&priv->lock);

  /* The caller uses this as an opaque handle for detach(). */
  return rc < 0 ? NULL : (FAR void *)(uintptr_t)pinset;
}
#endif

/****************************************************************************
 * Name: bk7258_gpioe_detach
 ****************************************************************************/

#ifdef CONFIG_IOEXPANDER_INT_ENABLE
static int bk7258_gpioe_detach(FAR struct ioexpander_dev_s *dev,
                               FAR void *handle)
{
  FAR struct bk7258_gpioe_priv_s *priv =
    (FAR struct bk7258_gpioe_priv_s *)dev;
  ioe_pinset_t pinset = (ioe_pinset_t)(uintptr_t)handle;
  uint8_t pin;
  int rc;

  rc = nxmutex_lock(&priv->lock);
  if (rc < 0)
    {
      return rc;
    }

  for (pin = 0; pin < priv->npins; pin++)
    {
      irqstate_t flags;

      if ((pinset & ((ioe_pinset_t)1 << pin)) == 0)
        {
          continue;
        }

      /* Order matters for the ISR race:
       *   1. disable the pin interrupt at the hardware so no new ISR for
       *      this pin can be dispatched;
       *   2. unregister the SDK handler;
       *   3. clear the slot under a critical section so a racing ISR that
       *      already passed the disable cannot observe a torn slot.
       */

      (void)bk_gpio_disable_interrupt((gpio_id_t)pin);
      (void)bk_gpio_register_isr((gpio_id_t)pin, NULL);

      flags = enter_critical_section();
      priv->isr[pin].active = false;
      priv->isr[pin].callback = NULL;
      priv->isr[pin].arg = NULL;
      leave_critical_section(flags);
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct ioexpander_dev_s *bk7258_gpioe_initialize(void)
{
  return &g_bk7258_gpioe.dev;
}

#endif /* CONFIG_BK7258_GPIOE */
