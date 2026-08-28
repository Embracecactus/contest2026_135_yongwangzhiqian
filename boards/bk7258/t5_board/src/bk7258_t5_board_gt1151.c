/****************************************************************************
 * board/bk7258/boards/t5_board/src/bk7258_t5_board_gt1151.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * T5-Board V1.0.2 binding for the GT1151 capacitive touch controller on
 * the T35P128CQ-02 LCD sub-board.
 *
 * The carrier routes SCL/SDA to GPIO13/GPIO15.  Those pins are not a valid
 * BK7258 hardware-I2C pair, so this binding deliberately uses NuttX's
 * standard i2c_bitbang lower half.  The Goodix protocol and raw input ABI
 * remain owned by NuttX's gt9xx driver.  This file owns the board wiring and
 * a narrow LVGL-facing capability adapter selected by this physical board.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_GT1151

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/fs/fs.h>
#include <nuttx/i2c/i2c_bitbang.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/gt9xx.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/irq.h>
#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include <arch/board/board.h>

#include <driver/gpio.h>

/* The immutable v3.1.1.9 bundle exports the GPIO device mapper but does not
 * publish its private declaration through the board wrapper headers.
 */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

#if CONFIG_INPUT_GT9XX_I2C_FREQUENCY > \
    BK7258_BOARD_TOUCH_I2C_MAX_FREQUENCY
#  error "GT1151 I2C frequency exceeds the T5-Board binding limit"
#endif

struct t5_gt1151_i2c_s
{
  struct i2c_master_s dev;
  FAR struct i2c_master_s *bitbang;
};

static void t5_gt1151_i2c_initialize(
  FAR struct i2c_bitbang_lower_dev_s *lower);
static void t5_gt1151_i2c_set_scl(
  FAR struct i2c_bitbang_lower_dev_s *lower, bool high);
static void t5_gt1151_i2c_set_sda(
  FAR struct i2c_bitbang_lower_dev_s *lower, bool high);
static bool t5_gt1151_i2c_get_scl(
  FAR struct i2c_bitbang_lower_dev_s *lower);
static bool t5_gt1151_i2c_get_sda(
  FAR struct i2c_bitbang_lower_dev_s *lower);
static int t5_gt1151_i2c_transfer(FAR struct i2c_master_s *dev,
                                  FAR struct i2c_msg_s *msgs, int count);
static int t5_gt1151_i2c_setup(FAR struct i2c_master_s *dev);
static int t5_gt1151_i2c_shutdown(FAR struct i2c_master_s *dev);
static int t5_gt1151_lvgl_open(FAR struct file *filep);
static int t5_gt1151_lvgl_close(FAR struct file *filep);
static ssize_t t5_gt1151_lvgl_read(FAR struct file *filep,
                                   FAR char *buffer, size_t buflen);
static int t5_gt1151_lvgl_ioctl(FAR struct file *filep, int cmd,
                                unsigned long arg);
static int t5_gt1151_lvgl_poll(FAR struct file *filep,
                               FAR struct pollfd *fds, bool setup);

static const struct i2c_bitbang_lower_ops_s g_t5_gt1151_bitbang_ops =
{
  .initialize = t5_gt1151_i2c_initialize,
  .set_scl    = t5_gt1151_i2c_set_scl,
  .set_sda    = t5_gt1151_i2c_set_sda,
  .get_scl    = t5_gt1151_i2c_get_scl,
  .get_sda    = t5_gt1151_i2c_get_sda,
};

static struct i2c_bitbang_lower_dev_s g_t5_gt1151_bitbang_lower =
{
  .ops = &g_t5_gt1151_bitbang_ops,
};

static const struct i2c_ops_s g_t5_gt1151_i2c_ops =
{
  .transfer = t5_gt1151_i2c_transfer,
  .setup    = t5_gt1151_i2c_setup,
  .shutdown = t5_gt1151_i2c_shutdown,
};

static struct t5_gt1151_i2c_s g_t5_gt1151_i2c =
{
  .dev.ops = &g_t5_gt1151_i2c_ops,
};

/* The upstream GT9XX character driver intentionally remains registered at
 * the board's generic touch path.  The selected LVGL backend additionally
 * requires TSIOC_GETMAXPOINTS, so expose a private board adapter rather than
 * changing the generic driver or replacing its public ABI.
 */

static const struct file_operations g_t5_gt1151_lvgl_fops =
{
  .open  = t5_gt1151_lvgl_open,
  .close = t5_gt1151_lvgl_close,
  .read  = t5_gt1151_lvgl_read,
  .ioctl = t5_gt1151_lvgl_ioctl,
  .poll  = t5_gt1151_lvgl_poll,
};

static xcpt_t g_t5_gt1151_isr;
static FAR void *g_t5_gt1151_isr_arg;
static bool g_t5_gt1151_registered;
static bool g_t5_gt1151_irq_registered;
static void t5_gt1151_invoke_isr(gpio_id_t gpio_id);
static void t5_gt1151_sdk_isr(gpio_id_t gpio_id);

static FAR struct file *t5_gt1151_lvgl_backend(FAR struct file *filep)
{
  return filep == NULL ? NULL : (FAR struct file *)filep->f_priv;
}

static int t5_gt1151_lvgl_open(FAR struct file *filep)
{
  FAR struct file *backend;
  int ret;

  if (filep == NULL || filep->f_priv != NULL)
    {
      return -EINVAL;
    }

  backend = kmm_zalloc(sizeof(*backend));
  if (backend == NULL)
    {
      return -ENOMEM;
    }

  ret = file_open(backend, BK7258_BOARD_TOUCH_DEVPATH, filep->f_oflags);
  if (ret < 0)
    {
      kmm_free(backend);
      return ret;
    }

  filep->f_priv = backend;
  return OK;
}

static int t5_gt1151_lvgl_close(FAR struct file *filep)
{
  FAR struct file *backend = t5_gt1151_lvgl_backend(filep);
  int ret;

  if (backend == NULL)
    {
      return -EBADF;
    }

  filep->f_priv = NULL;
  ret = file_close(backend);
  kmm_free(backend);
  return ret;
}

static ssize_t t5_gt1151_lvgl_read(FAR struct file *filep,
                                   FAR char *buffer, size_t buflen)
{
  FAR struct file *backend = t5_gt1151_lvgl_backend(filep);

  if (backend == NULL)
    {
      return -EBADF;
    }

  return file_read(backend, buffer, buflen);
}

static int t5_gt1151_lvgl_ioctl(FAR struct file *filep, int cmd,
                                unsigned long arg)
{
  FAR struct file *backend = t5_gt1151_lvgl_backend(filep);
  FAR uint8_t *maxpoints;

  if (backend == NULL)
    {
      return -EBADF;
    }

  if (cmd == TSIOC_GETMAXPOINTS)
    {
      if (arg == 0)
        {
          return -EINVAL;
        }

      maxpoints = (FAR uint8_t *)(uintptr_t)arg;
      *maxpoints = 1;
      return OK;
    }

  return file_ioctl(backend, cmd, arg);
}

static int t5_gt1151_lvgl_poll(FAR struct file *filep,
                               FAR struct pollfd *fds, bool setup)
{
  FAR struct file *backend = t5_gt1151_lvgl_backend(filep);

  if (backend == NULL)
    {
      return -EBADF;
    }

  return file_poll(backend, fds, setup);
}

#ifdef CONFIG_BK7258_GT1151_VALIDATION
static bool g_t5_gt1151_product_logged;
static volatile bool g_t5_gt1151_hw_irq_seen;

static void t5_gt1151_validation_trace(FAR struct i2c_msg_s *msgs,
                                       int count, int ret)
{
  uint16_t reg;

  if (ret < 0 || count != 2 || msgs[0].length != 2 ||
      (msgs[1].flags & I2C_M_READ) == 0 || msgs[1].length == 0)
    {
      return;
    }

  reg = (uint16_t)msgs[0].buffer[0] << 8 | msgs[0].buffer[1];
  if (reg == 0x8140 && msgs[1].length >= 4 &&
      !g_t5_gt1151_product_logged)
    {
      FAR const uint8_t *id = msgs[1].buffer;

      syslog(LOG_INFO,
             "bk7258-touch: product-id=%02x %02x %02x %02x\n",
             id[0], id[1], id[2], id[3]);
      g_t5_gt1151_product_logged = true;
    }
  else if (reg == 0x814e && msgs[1].buffer[0] != 0)
    {
      syslog(LOG_INFO, "bk7258-touch: status=0x%02x\n",
             msgs[1].buffer[0]);
    }
}

static int t5_gt1151_validation_thread(int argc, FAR char *argv[])
{
  struct touch_sample_s sample;
  struct pollfd pfd;
  ssize_t nread;
  uint8_t maxpoints = 0;
  int fd;
  int ret;
  bool hw_irq_logged = false;
  bool poll_fallback_logged = false;

  (void)argc;
  (void)argv;

  fd = open(BK7258_BOARD_TOUCH_LVGL_DEVPATH, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_ERR, "bk7258-touch: GT1151 probe failed: %d\n", errno);
      return -errno;
    }

  syslog(LOG_INFO, "bk7258-touch: GT1151 probe PASS at %s\n",
         BK7258_BOARD_TOUCH_LVGL_DEVPATH);

  ret = ioctl(fd, TSIOC_GETMAXPOINTS, (unsigned long)(uintptr_t)&maxpoints);
  if (ret < 0 || maxpoints != 1)
    {
      syslog(LOG_ERR,
             "bk7258-touch: LVGL capability adapter failed: %d/%u\n",
             ret < 0 ? errno : 0, (unsigned int)maxpoints);
      close(fd);
      return ret < 0 ? -errno : -EPROTO;
    }

  syslog(LOG_INFO,
         "bk7258-touch: LVGL capability adapter PASS maxpoints=%u\n",
         (unsigned int)maxpoints);

  pfd.fd = fd;
  pfd.events = POLLIN;

  for (;;)
    {
      if (g_t5_gt1151_hw_irq_seen && !hw_irq_logged)
        {
          syslog(LOG_INFO, "bk7258-touch: GPIO%u IRQ PASS\n",
                 (unsigned int)BK7258_BOARD_TOUCH_INTERRUPT_GPIO);
          hw_irq_logged = true;
        }

      pfd.revents = 0;
      ret = poll(&pfd, 1, 20);
      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          syslog(LOG_ERR, "bk7258-touch: poll failed: %d\n", errno);
          break;
        }

      if (ret == 0)
        {
          /* Drivercheck-only diagnostic: poll the NuttX GT9xx worker even
           * when the active-low INT line is not observed.  A resulting touch
           * sample proves that the controller and I2C path work and isolates
           * the remaining fault to the carrier's GPIO interrupt routing.
           * This is never compiled into production profiles.
           */

          gpio_id_t pin =
            (gpio_id_t)BK7258_BOARD_TOUCH_INTERRUPT_GPIO;

          if (!poll_fallback_logged)
            {
              syslog(LOG_WARNING,
                     "bk7258-touch: validating GPIO%u with polled fallback\n",
                     (unsigned int)pin);
              poll_fallback_logged = true;
            }

          t5_gt1151_invoke_isr(pin);
          continue;
        }

      if ((pfd.revents & POLLIN) == 0)
        {
          continue;
        }

      nread = read(fd, &sample, sizeof(sample));
      if (nread != sizeof(sample))
        {
          syslog(LOG_ERR, "bk7258-touch: read failed: %d/%ld\n",
                 errno, (long)nread);
          break;
        }

      if (sample.npoints > 0)
        {
          syslog(LOG_INFO,
                 "bk7258-touch: id=%u flags=0x%02x x=%d y=%d\n",
                 sample.point[0].id, sample.point[0].flags,
                 sample.point[0].x, sample.point[0].y);
        }
    }

  close(fd);
  return ERROR;
}
#endif

static void t5_gt1151_gpio_release(gpio_id_t pin)
{
  (void)bk_gpio_disable_output(pin);
  (void)bk_gpio_enable_input(pin);
}

static void t5_gt1151_gpio_drive_low(gpio_id_t pin)
{
  /* The output latch is primed low during initialize().  Open-drain I2C is
   * then implemented only by enabling the low driver or releasing it.
   */

  (void)bk_gpio_enable_output(pin);
}

static void t5_gt1151_i2c_initialize(
  FAR struct i2c_bitbang_lower_dev_s *lower)
{
  const gpio_id_t scl = (gpio_id_t)BK7258_BOARD_TOUCH_I2C_SCL_GPIO;
  const gpio_id_t sda = (gpio_id_t)BK7258_BOARD_TOUCH_I2C_SDA_GPIO;

  (void)lower;
  (void)gpio_dev_unmap(scl);
  (void)gpio_dev_unmap(sda);

  /* Prime both output latches low, then release the external 5.1-kohm
   * pull-ups.  Subsequent toggles never drive a high level onto the bus.
   */

  (void)bk_gpio_enable_output(scl);
  (void)bk_gpio_set_output_low(scl);
  (void)bk_gpio_enable_input(scl);
  (void)bk_gpio_enable_pull(scl);
  (void)bk_gpio_pull_up(scl);
  t5_gt1151_gpio_release(scl);

  (void)bk_gpio_enable_output(sda);
  (void)bk_gpio_set_output_low(sda);
  (void)bk_gpio_enable_input(sda);
  (void)bk_gpio_enable_pull(sda);
  (void)bk_gpio_pull_up(sda);
  t5_gt1151_gpio_release(sda);
}

static void t5_gt1151_i2c_set_scl(
  FAR struct i2c_bitbang_lower_dev_s *lower, bool high)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_TOUCH_I2C_SCL_GPIO;

  (void)lower;
  if (high)
    {
      t5_gt1151_gpio_release(pin);
    }
  else
    {
      t5_gt1151_gpio_drive_low(pin);
    }
}

static void t5_gt1151_i2c_set_sda(
  FAR struct i2c_bitbang_lower_dev_s *lower, bool high)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_TOUCH_I2C_SDA_GPIO;

  (void)lower;
  if (high)
    {
      t5_gt1151_gpio_release(pin);
    }
  else
    {
      t5_gt1151_gpio_drive_low(pin);
    }
}

static bool t5_gt1151_i2c_get_scl(
  FAR struct i2c_bitbang_lower_dev_s *lower)
{
  (void)lower;
  return bk_gpio_get_input(
           (gpio_id_t)BK7258_BOARD_TOUCH_I2C_SCL_GPIO);
}

static bool t5_gt1151_i2c_get_sda(
  FAR struct i2c_bitbang_lower_dev_s *lower)
{
  (void)lower;
  return bk_gpio_get_input(
           (gpio_id_t)BK7258_BOARD_TOUCH_I2C_SDA_GPIO);
}

static int t5_gt1151_i2c_transfer(FAR struct i2c_master_s *dev,
                                  FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct t5_gt1151_i2c_s *priv =
    (FAR struct t5_gt1151_i2c_s *)dev;
  struct i2c_msg_s fixed[2];
  int ret;

  if (msgs == NULL || count <= 0)
    {
      return -EINVAL;
    }

  /* NuttX gt9xx expresses a 16-bit register operation as two messages.  Its
   * first message omits NOSTOP while the second write uses NOSTART.  The
   * generic bitbang lower half needs the first flag made explicit so that
   * the pair remains one legal bus transaction.  Do not change any other
   * client's message semantics.
   */

  if (count == 2 && msgs[0].length == 2 &&
      (msgs[0].flags & I2C_M_READ) == 0 &&
      msgs[0].addr == msgs[1].addr)
    {
      fixed[0] = msgs[0];
      fixed[1] = msgs[1];
      fixed[0].flags |= I2C_M_NOSTOP;
      ret = I2C_TRANSFER(priv->bitbang, fixed, 2);
#ifdef CONFIG_BK7258_GT1151_VALIDATION
      t5_gt1151_validation_trace(fixed, 2, ret);
#endif
      return ret;
    }

  ret = I2C_TRANSFER(priv->bitbang, msgs, count);
#ifdef CONFIG_BK7258_GT1151_VALIDATION
  t5_gt1151_validation_trace(msgs, count, ret);
#endif
  return ret;
}

static int t5_gt1151_i2c_setup(FAR struct i2c_master_s *dev)
{
  (void)dev;
  return OK;
}

static int t5_gt1151_i2c_shutdown(FAR struct i2c_master_s *dev)
{
  (void)dev;
  return OK;
}

static void t5_gt1151_invoke_isr(gpio_id_t gpio_id)
{
  xcpt_t isr;
  FAR void *arg;
  irqstate_t flags;

  flags = enter_critical_section();
  isr = g_t5_gt1151_isr;
  arg = g_t5_gt1151_isr_arg;
  leave_critical_section(flags);

  if (isr != NULL)
    {
      (void)isr((int)gpio_id, NULL, arg);
    }
}

static void t5_gt1151_sdk_isr(gpio_id_t gpio_id)
{
#ifdef CONFIG_BK7258_GT1151_VALIDATION
  g_t5_gt1151_hw_irq_seen = true;
#endif
  t5_gt1151_invoke_isr(gpio_id);
}

static int t5_gt1151_irq_attach(const struct gt9xx_board_s *state,
                                xcpt_t isr, FAR void *arg)
{
  irqstate_t flags;

  (void)state;

  if (isr == NULL || arg == NULL || !g_t5_gt1151_irq_registered)
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  g_t5_gt1151_isr = isr;
  g_t5_gt1151_isr_arg = arg;
  leave_critical_section(flags);
  return OK;
}

static int t5_gt1151_irq_prepare(void)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_TOUCH_INTERRUPT_GPIO;

  if (g_t5_gt1151_irq_registered)
    {
      return OK;
    }

  (void)gpio_dev_unmap(pin);
  (void)bk_gpio_disable_interrupt(pin);
  (void)bk_gpio_disable_output(pin);
  (void)bk_gpio_enable_input(pin);
  (void)bk_gpio_enable_pull(pin);
  (void)bk_gpio_pull_up(pin);

  if (bk_gpio_set_interrupt_type(pin, GPIO_INT_TYPE_FALLING_EDGE) != BK_OK)
    {
      return -EIO;
    }

  if (bk_gpio_register_isr(pin, t5_gt1151_sdk_isr) != BK_OK)
    {
      return -EIO;
    }

  g_t5_gt1151_irq_registered = true;
  return OK;
}

static void t5_gt1151_irq_enable(const struct gt9xx_board_s *state,
                                 bool enable)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_TOUCH_INTERRUPT_GPIO;

  (void)state;
  if (enable)
    {
      (void)bk_gpio_clear_interrupt(pin);
      (void)bk_gpio_enable_interrupt(pin);
    }
  else
    {
      (void)bk_gpio_disable_interrupt(pin);
      (void)bk_gpio_clear_interrupt(pin);
    }
}

static int t5_gt1151_set_power(const struct gt9xx_board_s *state, bool on)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_TOUCH_RESET_GPIO;

  (void)state;
  (void)gpio_dev_unmap(pin);
  (void)bk_gpio_disable_input(pin);
  (void)bk_gpio_enable_output(pin);

  if (!on)
    {
      return bk_gpio_set_output_low(pin) == BK_OK ? OK : -EIO;
    }

  if (bk_gpio_set_output_low(pin) != BK_OK)
    {
      return -EIO;
    }

  nxsig_usleep(10 * 1000);
  if (bk_gpio_set_output_high(pin) != BK_OK)
    {
      return -EIO;
    }

  nxsig_usleep(50 * 1000);
  return OK;
}

static const struct gt9xx_board_s g_t5_gt1151_board =
{
  .irq_attach = t5_gt1151_irq_attach,
  .irq_enable = t5_gt1151_irq_enable,
  .set_power  = t5_gt1151_set_power,
};

int bk7258_board_gt1151_initialize(void)
{
  int ret;

  if (g_t5_gt1151_registered)
    {
      return OK;
    }

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  g_t5_gt1151_i2c.bitbang =
    i2c_bitbang_initialize(&g_t5_gt1151_bitbang_lower);
  if (g_t5_gt1151_i2c.bitbang == NULL)
    {
      return -ENOMEM;
    }

  /* NuttX gt9xx_register() currently discards irq_attach()'s return value
   * after publishing the character device.  Preflight every fallible SDK
   * GPIO operation so /dev/input0 is never registered without a usable IRQ.
   * The callback itself is installed by gt9xx_register() immediately after
   * the driver node is published.
   */

  ret = t5_gt1151_irq_prepare();
  if (ret < 0)
    {
      return ret;
    }

  /* Publish the adapter first.  No application can run during this board
   * initialization phase; doing this ordering lets a GT9XX registration
   * failure remove the adapter cleanly without relying on a GT9XX-specific
   * destructor that the generic driver does not expose.
   */

  ret = register_driver(BK7258_BOARD_TOUCH_LVGL_DEVPATH,
                        &g_t5_gt1151_lvgl_fops, 0666, NULL);
  if (ret < 0)
    {
      return ret;
    }

  ret = gt9xx_register(BK7258_BOARD_TOUCH_DEVPATH, &g_t5_gt1151_i2c.dev,
                       BK7258_BOARD_TOUCH_I2C_ADDRESS,
                       &g_t5_gt1151_board);
  if (ret < 0)
    {
      (void)unregister_driver(BK7258_BOARD_TOUCH_LVGL_DEVPATH);
      return ret;
    }

  g_t5_gt1151_registered = true;
  iinfo("T5-Board GT1151 registered at %s (LVGL adapter %s)\n",
        BK7258_BOARD_TOUCH_DEVPATH,
        BK7258_BOARD_TOUCH_LVGL_DEVPATH);

#ifdef CONFIG_BK7258_GT1151_VALIDATION
  ret = kthread_create("bk7258-touch", SCHED_PRIORITY_DEFAULT, 2048,
                       t5_gt1151_validation_thread, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "bk7258-touch: validation worker failed: %d\n", ret);
      return ret;
    }
#endif

  return OK;
}

#endif /* CONFIG_BK7258_GT1151 */
