/****************************************************************************
 * chips/bk7258/cp/bk7258_irda.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 IrDA NEC receiver character device (register-level).
 *
 * The v3.1.1.9 CP SDK IrDA driver (middleware/driver/irda) is compiled
 * against the legacy Beken base 0x00802400; the BK7258 hardware locates IrDA
 * at SOC_IRDA_REG_BASE (0x458b0000) and the SDK exposes no register
 * definitions for that base.  This wrapper therefore implements the NEC
 * decode path board-owned against 0x458b0000, mirroring the SDK decoder
 * (same register offsets and bit layout: CTRL+0, INT_MASK+4, INT+8,
 * RX_FIFO+12), and publishes /dev/irda0:
 *
 *   - open:  GPIO_25 -> GPIO_DEV_IRDA, INT_SRC_IRDA hook, receiver config,
 *            key ring buffer + semaphore + debounce wdog
 *   - read:  one uint32_t GENERATE_KEY(type, value)
 *   - ioctl: BKIOC_IRDA_* (SET_USERCODE supported)
 *   - close: receiver teardown
 *
 * The BK7258 ICU clock-power and interrupt-enable ll hooks are empty
 * implementations in the SDK (IRQ delivery is NVIC-managed), so no extra
 * ICU register writes are required.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_IRDA

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wdog.h>

#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <driver/int.h>
#include <driver/int_types.h>

#include <arch/chip/bk7258_irda.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* BK7258 hardware base (SOC_IRDA_REG_BASE, reg_base.h).  Register offsets
 * mirror the legacy irda.h layout assumed identical for the same Beken IP.
 */

#define BK7258_IRDA_REG_BASE   0x458b0000u
#define BK7258_IRDA_CTRL       (BK7258_IRDA_REG_BASE + 0x00u)
#define BK7258_IRDA_INT_MASK   (BK7258_IRDA_REG_BASE + 0x04u)
#define BK7258_IRDA_INT        (BK7258_IRDA_REG_BASE + 0x08u)
#define BK7258_IRDA_RX_FIFO    (BK7258_IRDA_REG_BASE + 0x0cu)

#define BK7258_IRDA_NEC_EN     (1u << 0)
#define BK7258_IRDA_POLARITY   (1u << 1)
#define BK7258_IRDA_CLK_POSI   8
#define BK7258_IRDA_CLK_MASK   0xffffu

#define BK7258_IRDA_END_INT    (1u << 0)
#define BK7258_IRDA_RIGHT_INT  (1u << 1)
#define BK7258_IRDA_REPEAT_INT (1u << 2)
#define BK7258_IRDA_INT_MASK_EN 0x3fu

#define BK7258_IRDA_REPEAT_PERIOD_MS  112u

#define BK7258_IRDA_USERCODE_MASK     0xffffu
#define BK7258_IRDA_KEY_CODE_MASK     0xff0000u
#define BK7258_IRDA_KEY_CODE_SHIFT    16
#define BK7258_IRDA_KEY_CODE_INV_MASK 0xff000000u
#define BK7258_IRDA_KEY_CODE_INV_SHIFT 24

#define BK7258_IRDA_KEY_SHORT_CNT     3
#define BK7258_IRDA_KEY_LONG_CNT      8
#define BK7258_IRDA_KEY_HOLD_CNT      11

#define BK7258_IRDA_GEN_KEY(type, v)  (((uint32_t)(type) << 24) | (uint32_t)(v))

#define BK7258_IRDA_RING_SIZE         16

#define BK7258_IRDA_READ_TIMEOUT_MS   1000u

/* SDK GPIO device-mux API is not exported in the bundle headers. */

extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, uint32_t gpio_dev);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_irda_priv_s
{
  mutex_t lock;
  sem_t keysem;
  struct wdog_s keydog;
  uint32_t ring[BK7258_IRDA_RING_SIZE];
  uint8_t ring_head;
  uint8_t ring_tail;
  uint16_t usercode;
  uint8_t valid_flag;
  uint8_t repeat_flag;
  uint8_t repeat_cnt;
  uint8_t timer_cnt;
  uint8_t key_code;
  uint8_t hold_flag;
  bool dog_running;
  bool inited;
  bool opened;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_irda_priv_s g_bk7258_irda =
{
  .lock       = NXMUTEX_INITIALIZER,
  .keysem     = SEM_INITIALIZER(0),
  .usercode   = 0,
  .inited     = false,
  .opened     = false,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t bk7258_irda_reg_read(uint32_t reg)
{
  return *(FAR volatile uint32_t *)reg;
}

static inline void bk7258_irda_reg_write(uint32_t reg, uint32_t value)
{
  *(FAR volatile uint32_t *)reg = value;
}

static void bk7258_irda_push_key(uint32_t key)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  irqstate_t flags;
  uint8_t next;

  flags = enter_critical_section();
  next = (priv->ring_head + 1u) % BK7258_IRDA_RING_SIZE;
  if (next != priv->ring_tail)
    {
      priv->ring[priv->ring_head] = key;
      priv->ring_head = next;
      nxsem_post(&priv->keysem);
    }

  leave_critical_section(flags);
}

static void bk7258_irda_keydog(wdparm_t arg)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  uint8_t key_type = 0;
  uint8_t op = 0;

  (void)arg;

#define SND_KEY  (1u << 0)
#define STOP_KEY (1u << 1)

  priv->timer_cnt++;
  if (priv->timer_cnt > priv->repeat_cnt)
    {
      if (priv->repeat_cnt < BK7258_IRDA_KEY_SHORT_CNT)
        {
          key_type = BK7258_IRDA_KEY_SHORT;
          op = SND_KEY | STOP_KEY;
        }
      else if (priv->repeat_cnt < BK7258_IRDA_KEY_LONG_CNT)
        {
          key_type = BK7258_IRDA_KEY_LONG;
          op = SND_KEY | STOP_KEY;
        }
      else
        {
          if (priv->hold_flag == 0)
            {
              key_type = BK7258_IRDA_KEY_HOLD;
              op = SND_KEY | STOP_KEY;
            }
          else
            {
              op = STOP_KEY;
            }
        }

      priv->hold_flag = 0;
    }
  else
    {
      if (priv->repeat_cnt >= BK7258_IRDA_KEY_HOLD_CNT)
        {
          priv->hold_flag = 1;
          op = SND_KEY;
          key_type = BK7258_IRDA_KEY_HOLD;
          priv->repeat_cnt = BK7258_IRDA_KEY_LONG_CNT;
          priv->timer_cnt = BK7258_IRDA_KEY_LONG_CNT;
        }
    }

  if ((op & SND_KEY) != 0)
    {
      bk7258_irda_push_key(BK7258_IRDA_GEN_KEY(key_type, priv->key_code));
    }

  if ((op & STOP_KEY) != 0)
    {
      priv->dog_running = false;
    }
  else if (priv->dog_running)
    {
      (void)wd_start(&priv->keydog,
                     MSEC2TICK(BK7258_IRDA_REPEAT_PERIOD_MS),
                     bk7258_irda_keydog, 0);
    }
}

static void bk7258_irda_isr(void)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  uint32_t irda_int;
  uint32_t end_int;
  uint32_t right_int;
  uint32_t repeat_int;
  uint32_t tmp;

  irda_int = bk7258_irda_reg_read(BK7258_IRDA_INT);
  end_int   = irda_int & BK7258_IRDA_END_INT;
  right_int = irda_int & BK7258_IRDA_RIGHT_INT;
  repeat_int = irda_int & BK7258_IRDA_REPEAT_INT;
  bk7258_irda_reg_write(BK7258_IRDA_INT, irda_int);

  if (right_int != 0)
    {
      priv->valid_flag = 1;
      priv->repeat_flag = 0;
      priv->repeat_cnt = 0;
    }

  if (end_int != 0 && priv->valid_flag)
    {
      tmp = bk7258_irda_reg_read(BK7258_IRDA_RX_FIFO);

      if (((tmp & BK7258_IRDA_USERCODE_MASK) != priv->usercode) ||
          ((((tmp & BK7258_IRDA_KEY_CODE_INV_MASK) >>
              BK7258_IRDA_KEY_CODE_INV_SHIFT) ^
            ((tmp & BK7258_IRDA_KEY_CODE_MASK) >>
             BK7258_IRDA_KEY_CODE_SHIFT)) != 0xffu))
        {
          priv->valid_flag = 0;
          return;
        }

      priv->key_code = (tmp & BK7258_IRDA_KEY_CODE_MASK) >>
                       BK7258_IRDA_KEY_CODE_SHIFT;
      priv->timer_cnt = 0;

      if (priv->dog_running)
        {
          (void)wd_cancel(&priv->keydog);
        }

      priv->dog_running = true;
      (void)wd_start(&priv->keydog,
                     MSEC2TICK(BK7258_IRDA_REPEAT_PERIOD_MS),
                     bk7258_irda_keydog, 0);
    }

  if (repeat_int != 0)
    {
      priv->repeat_flag = 1;
      priv->repeat_cnt++;
    }
}

static int bk7258_irda_open(FAR struct file *filep);
static int bk7258_irda_close(FAR struct file *filep);
static ssize_t bk7258_irda_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen);
static ssize_t bk7258_irda_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen);
static int bk7258_irda_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg);

static const struct file_operations g_bk7258_irda_fops =
{
  .open  = bk7258_irda_open,
  .close = bk7258_irda_close,
  .read  = bk7258_irda_read,
  .write = bk7258_irda_write,
  .ioctl = bk7258_irda_ioctl,
};

static int bk7258_irda_open(FAR struct file *filep)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  uint32_t ctrl;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  if (priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  /* Receiver GPIO: GPIO_25 carries GPIO_DEV_IRDA (gpio_map.h). */

  (void)gpio_dev_map(GPIO_25, GPIO_DEV_IRDA);

  /* Interrupt: INT_SRC_IRDA -> NEC decoder.  ICU ll enable hook is empty on
   * BK7258 (NVIC-managed), so bk_int_isr_register is sufficient.
   */

  ret = bk_int_isr_register(INT_SRC_IRDA, (int_group_isr_t)bk7258_irda_isr,
                            NULL);
  if (ret != OK)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  /* Receiver configuration mirroring the SDK Irda_init_app(): polarity 0
   * (low active), NEC enable, 26 MHz -> ~562.5 divider, RX interrupt mask.
   */

  bk7258_irda_reg_write(BK7258_IRDA_INT_MASK, 0);
  bk7258_irda_reg_write(BK7258_IRDA_INT, 0);

  ctrl = bk7258_irda_reg_read(BK7258_IRDA_CTRL);
  ctrl &= ~BK7258_IRDA_POLARITY;
  ctrl |= BK7258_IRDA_NEC_EN;
  ctrl = (ctrl & ~(BK7258_IRDA_CLK_MASK << BK7258_IRDA_CLK_POSI)) |
         (0x3921u << BK7258_IRDA_CLK_POSI);
  bk7258_irda_reg_write(BK7258_IRDA_CTRL, ctrl);

  bk7258_irda_reg_write(BK7258_IRDA_INT_MASK,
                        BK7258_IRDA_RIGHT_INT | BK7258_IRDA_REPEAT_INT |
                        BK7258_IRDA_END_INT);

  priv->opened = true;

  syslog(LOG_INFO, "BK7258 IRDA: open %s (reg 0x%08x)\n",
         BK7258_IRDA_DEVPATH, BK7258_IRDA_REG_BASE);

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_irda_close(FAR struct file *filep)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  uint32_t ctrl;
  int ret;

  (void)filep;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->dog_running)
    {
      (void)wd_cancel(&priv->keydog);
      priv->dog_running = false;
    }

  bk7258_irda_reg_write(BK7258_IRDA_INT_MASK, 0);
  bk7258_irda_reg_write(BK7258_IRDA_INT, 0);

  ctrl = bk7258_irda_reg_read(BK7258_IRDA_CTRL);
  ctrl &= ~BK7258_IRDA_NEC_EN;
  bk7258_irda_reg_write(BK7258_IRDA_CTRL, ctrl);

  priv->opened = false;

  nxmutex_unlock(&priv->lock);
  return OK;
}

static ssize_t bk7258_irda_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  uint32_t key;
  irqstate_t flags;
  int ret;

  (void)filep;

  if (buffer == NULL || buflen < sizeof(key))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  nxmutex_unlock(&priv->lock);

  ret = nxsem_wait_uninterruptible(&priv->keysem);
  if (ret < 0)
    {
      return ret;
    }

  flags = enter_critical_section();
  key = priv->ring[priv->ring_tail];
  priv->ring_tail = (priv->ring_tail + 1u) % BK7258_IRDA_RING_SIZE;
  leave_critical_section(flags);

  memcpy(buffer, &key, sizeof(key));
  return (ssize_t)sizeof(key);
}

static ssize_t bk7258_irda_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen)
{
  (void)filep;
  (void)buffer;
  (void)buflen;

  /* NEC receive only. */

  return -ENOTSUP;
}

static int bk7258_irda_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  int ret;

  (void)filep;

  switch (cmd)
    {
      case BKIOC_IRDA_SET_USERCODE:
        ret = nxmutex_lock(&priv->lock);
        if (ret < 0)
          {
            return ret;
          }

        if (!priv->opened)
          {
            nxmutex_unlock(&priv->lock);
            return -ENODEV;
          }

        priv->usercode = (uint16_t)arg;
        nxmutex_unlock(&priv->lock);
        return OK;

      /* ACTIVE / SET_POLARITY / SET_CLK / SET_INT_MASK are applied by the
       * receiver defaults; per-frame runtime control is not exposed.
       */

      default:
        return -ENOTTY;
    }
}

int bk7258_irda_initialize(void)
{
  FAR struct bk7258_irda_priv_s *priv = &g_bk7258_irda;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  ret = register_driver(BK7258_IRDA_DEVPATH, &g_bk7258_irda_fops, 0666,
                        NULL);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->inited = true;

  syslog(LOG_INFO, "BK7258 IRDA: ready %s (register-level 0x%08x)\n",
         BK7258_IRDA_DEVPATH, BK7258_IRDA_REG_BASE);

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_IRDA */
