/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_mfrc522.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK MFRC522 UART transport for the standard NuttX MFRC522 driver.
 *
 * NuttX's protocol driver is bus-independent above its small SPI register
 * access layer, but exposes only an SPI registration entry point.  The AIDK
 * module is strapped for the MFRC522 serial-UART host protocol instead.  This
 * file therefore presents a virtual SPI device that translates register
 * transactions to the official UART sequence:
 *
 *   read:  send (0x80 | register), receive data
 *   write: send register, receive the echoed register, send data
 *
 * ISO14443-A discovery, anticollision, cascade selection, CRC and the public
 * MFRC522IOC_* ABI remain owned by drivers/contactless/mfrc522.c.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_MFRC522

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>

#include <nuttx/contactless/mfrc522.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/serial/tioctl.h>
#include <nuttx/signal.h>
#include <nuttx/spi/spi.h>

#include <arch/board/board.h>

#include <driver/gpio.h>

#define AIDK_NFC_DEVPATH              "/dev/nfc0"
#define AIDK_NFC_UART_DEVPATH         "/dev/ttyS1"
#define AIDK_NFC_UART_BAUD            B9600
#define AIDK_NFC_UART_TIMEOUT_LOOPS   20
#define AIDK_NFC_POWER_SETTLE_US      10000

#define AIDK_NFC_REG_SERIAL_SPEED     0x1f
#define AIDK_NFC_REG_TEST_PIN_EN      0x33
#define AIDK_NFC_TEST_RS232_LINE_EN   0x80

#if BK7258_BOARD_HAS_MFRC522 != 1 || \
    BK7258_BOARD_PIN_UART1_TXD != 0 || \
    BK7258_BOARD_PIN_UART1_RXD != 1 || \
    BK7258_BOARD_PIN_LDO33_EN != 52 || \
    BK7258_BOARD_PIN_NFC_IRQ != 53 || \
    BK7258_BOARD_PIN_NFC_MX != 54 || \
    BK7258_BOARD_PIN_NFC_DTRQ != 55
#  error "AIDK MFRC522 UART/power binding no longer matches the board"
#endif

struct aidk_nfc_uart_s
{
  struct spi_dev_s spi;
  mutex_t lock;
  struct file uart;
  uint8_t reg;
  bool selected;
  bool have_address;
  bool read;
  int last_error;
};

/* This public SDK function is declared locally just as the official AIDK
 * NFC component does.  Including driver/pwr_clk.h would pull private SDK PM
 * headers that are intentionally outside the NuttX board include surface.
 */

extern bk_err_t bk_pm_module_vote_ctrl_external_ldo(
  uint32_t module, gpio_id_t gpio_id, gpio_output_state_e value);
extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);

static int aidk_nfc_spi_lock(FAR struct spi_dev_s *spi, bool lock);
static void aidk_nfc_spi_select(FAR struct spi_dev_s *spi, uint32_t devid,
                                bool selected);
static uint32_t aidk_nfc_spi_setfrequency(FAR struct spi_dev_s *spi,
                                          uint32_t frequency);
#ifdef CONFIG_SPI_DELAY_CONTROL
static int aidk_nfc_spi_setdelay(FAR struct spi_dev_s *spi, uint32_t a,
                                 uint32_t b, uint32_t c, uint32_t i);
#endif
static void aidk_nfc_spi_setmode(FAR struct spi_dev_s *spi,
                                 enum spi_mode_e mode);
static void aidk_nfc_spi_setbits(FAR struct spi_dev_s *spi, int nbits);
#ifdef CONFIG_SPI_HWFEATURES
static int aidk_nfc_spi_hwfeatures(FAR struct spi_dev_s *spi,
                                   spi_hwfeatures_t features);
#endif
static uint8_t aidk_nfc_spi_status(FAR struct spi_dev_s *spi,
                                   uint32_t devid);
#ifdef CONFIG_SPI_CMDDATA
static int aidk_nfc_spi_cmddata(FAR struct spi_dev_s *spi, uint32_t devid,
                                bool cmd);
#endif
static uint32_t aidk_nfc_spi_send(FAR struct spi_dev_s *spi, uint32_t word);
#ifdef CONFIG_SPI_EXCHANGE
static void aidk_nfc_spi_exchange(FAR struct spi_dev_s *spi,
                                  FAR const void *txbuffer,
                                  FAR void *rxbuffer, size_t nwords);
#else
static void aidk_nfc_spi_sndblock(FAR struct spi_dev_s *spi,
                                  FAR const void *buffer, size_t nwords);
static void aidk_nfc_spi_recvblock(FAR struct spi_dev_s *spi,
                                   FAR void *buffer, size_t nwords);
#endif

static const struct spi_ops_s g_aidk_nfc_spi_ops =
{
  .lock         = aidk_nfc_spi_lock,
  .select       = aidk_nfc_spi_select,
  .setfrequency = aidk_nfc_spi_setfrequency,
#ifdef CONFIG_SPI_DELAY_CONTROL
  .setdelay     = aidk_nfc_spi_setdelay,
#endif
  .setmode      = aidk_nfc_spi_setmode,
  .setbits      = aidk_nfc_spi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures   = aidk_nfc_spi_hwfeatures,
#endif
  .status       = aidk_nfc_spi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata      = aidk_nfc_spi_cmddata,
#endif
  .send         = aidk_nfc_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange     = aidk_nfc_spi_exchange,
#else
  .sndblock     = aidk_nfc_spi_sndblock,
  .recvblock    = aidk_nfc_spi_recvblock,
#endif
};

static struct aidk_nfc_uart_s g_aidk_nfc_uart =
{
  .spi.ops = &g_aidk_nfc_spi_ops,
  .lock = NXMUTEX_INITIALIZER,
};

static int aidk_nfc_uart_write_byte(FAR struct aidk_nfc_uart_s *priv,
                                    uint8_t value)
{
  int i;

  for (i = 0; i < AIDK_NFC_UART_TIMEOUT_LOOPS; i++)
    {
      ssize_t ret = file_write(&priv->uart, (FAR const char *)&value, 1);

      if (ret == 1)
        {
          return OK;
        }

      if (ret < 0 && ret != -EAGAIN && ret != -EINTR)
        {
          return (int)ret;
        }

      (void)nxsig_usleep(1000);
    }

  return -ETIMEDOUT;
}

static int aidk_nfc_uart_read_byte(FAR struct aidk_nfc_uart_s *priv,
                                   FAR uint8_t *value)
{
  int i;

  for (i = 0; i < AIDK_NFC_UART_TIMEOUT_LOOPS; i++)
    {
      ssize_t ret = file_read(&priv->uart, (FAR char *)value, 1);

      if (ret == 1)
        {
          return OK;
        }

      if (ret < 0 && ret != -EAGAIN && ret != -EINTR)
        {
          return (int)ret;
        }

      (void)nxsig_usleep(1000);
    }

  return -ETIMEDOUT;
}

static int aidk_nfc_uart_read_reg(FAR struct aidk_nfc_uart_s *priv,
                                  uint8_t reg, FAR uint8_t *value)
{
  uint8_t address = reg | 0x80;
  int ret;

  ret = aidk_nfc_uart_write_byte(priv, address);
  if (ret < 0)
    {
      return ret;
    }

  return aidk_nfc_uart_read_byte(priv, value);
}

static int aidk_nfc_uart_write_reg(FAR struct aidk_nfc_uart_s *priv,
                                   uint8_t reg, uint8_t value)
{
  uint8_t echo;
  int ret;
  int i;

  ret = aidk_nfc_uart_write_byte(priv, reg & 0x3f);
  if (ret < 0)
    {
      return ret;
    }

  /* The MFRC522 echoes the address before accepting the data byte. */

  for (i = 0; i < AIDK_NFC_UART_TIMEOUT_LOOPS; i++)
    {
      ret = aidk_nfc_uart_read_byte(priv, &echo);
      if (ret < 0)
        {
          return ret;
        }

      if (echo == (reg & 0x3f))
        {
          return aidk_nfc_uart_write_byte(priv, value);
        }
    }

  return -EPROTO;
}

static int aidk_nfc_spi_lock(FAR struct spi_dev_s *spi, bool lock)
{
  FAR struct aidk_nfc_uart_s *priv = (FAR struct aidk_nfc_uart_s *)spi;

  return lock ? nxmutex_lock(&priv->lock) : nxmutex_unlock(&priv->lock);
}

static void aidk_nfc_spi_select(FAR struct spi_dev_s *spi, uint32_t devid,
                                bool selected)
{
  FAR struct aidk_nfc_uart_s *priv = (FAR struct aidk_nfc_uart_s *)spi;

  (void)devid;
  priv->selected = selected;
  priv->have_address = false;
}

static uint32_t aidk_nfc_spi_setfrequency(FAR struct spi_dev_s *spi,
                                          uint32_t frequency)
{
  (void)spi;
  return frequency;
}

#ifdef CONFIG_SPI_DELAY_CONTROL
static int aidk_nfc_spi_setdelay(FAR struct spi_dev_s *spi, uint32_t a,
                                 uint32_t b, uint32_t c, uint32_t i)
{
  (void)spi;
  (void)a;
  (void)b;
  (void)c;
  (void)i;
  return OK;
}
#endif

static void aidk_nfc_spi_setmode(FAR struct spi_dev_s *spi,
                                 enum spi_mode_e mode)
{
  (void)spi;
  (void)mode;
}

static void aidk_nfc_spi_setbits(FAR struct spi_dev_s *spi, int nbits)
{
  (void)spi;
  (void)nbits;
}

#ifdef CONFIG_SPI_HWFEATURES
static int aidk_nfc_spi_hwfeatures(FAR struct spi_dev_s *spi,
                                   spi_hwfeatures_t features)
{
  (void)spi;
  return features == 0 ? OK : -ENOSYS;
}
#endif

static uint8_t aidk_nfc_spi_status(FAR struct spi_dev_s *spi,
                                   uint32_t devid)
{
  (void)spi;
  (void)devid;
  return SPI_STATUS_PRESENT;
}

#ifdef CONFIG_SPI_CMDDATA
static int aidk_nfc_spi_cmddata(FAR struct spi_dev_s *spi, uint32_t devid,
                                bool cmd)
{
  (void)spi;
  (void)devid;
  (void)cmd;
  return -ENOSYS;
}
#endif

static uint32_t aidk_nfc_spi_send(FAR struct spi_dev_s *spi, uint32_t word)
{
  FAR struct aidk_nfc_uart_s *priv = (FAR struct aidk_nfc_uart_s *)spi;
  uint8_t value = 0;
  int ret;

  if (!priv->selected)
    {
      priv->last_error = -EIO;
      return 0;
    }

  if (!priv->have_address)
    {
      uint8_t address = (uint8_t)word;

      priv->reg = (address & 0x7e) >> 1;
      priv->read = (address & 0x80) != 0;
      priv->have_address = true;
      return 0;
    }

  if (priv->read)
    {
      ret = aidk_nfc_uart_read_reg(priv, priv->reg, &value);
    }
  else
    {
      ret = aidk_nfc_uart_write_reg(priv, priv->reg, (uint8_t)word);
    }

  priv->last_error = ret;
  return ret < 0 ? 0 : value;
}

#ifdef CONFIG_SPI_EXCHANGE
static void aidk_nfc_spi_exchange(FAR struct spi_dev_s *spi,
                                  FAR const void *txbuffer,
                                  FAR void *rxbuffer, size_t nwords)
{
  FAR const uint8_t *tx = txbuffer;
  FAR uint8_t *rx = rxbuffer;
  size_t i;

  for (i = 0; i < nwords; i++)
    {
      uint8_t value = (uint8_t)aidk_nfc_spi_send(spi, tx ? tx[i] : 0xff);

      if (rx)
        {
          rx[i] = value;
        }
    }
}
#else
static void aidk_nfc_spi_sndblock(FAR struct spi_dev_s *spi,
                                  FAR const void *buffer, size_t nwords)
{
  FAR const uint8_t *data = buffer;
  size_t i;

  for (i = 0; i < nwords; i++)
    {
      (void)aidk_nfc_spi_send(spi, data[i]);
    }
}

static void aidk_nfc_spi_recvblock(FAR struct spi_dev_s *spi,
                                   FAR void *buffer, size_t nwords)
{
  FAR uint8_t *data = buffer;
  size_t i;

  for (i = 0; i < nwords; i++)
    {
      data[i] = (uint8_t)aidk_nfc_spi_send(spi, 0xff);
    }
}
#endif

static int aidk_nfc_power_on(void)
{
  gpio_id_t pin = (gpio_id_t)BK7258_BOARD_PIN_LDO33_EN;
  bk_err_t error;

  /* The external 3.3 V rail is owned by the SDK power manager and may be
   * serviced by CP.  Use the same module vote as the official AIDK NFC
   * implementation; direct AP GPIO ownership fails when CP owns LDO control.
   */

  error = bk_pm_module_vote_ctrl_external_ldo(
            GPIO_CTRL_LDO_MODULE_NFC, pin, GPIO_OUTPUT_STATE_HIGH);
  if (error != BK_OK)
    {
      syslog(LOG_ERR, "AIDK MFRC522 LDO vote failed: %d\n", error);
      return -EIO;
    }

  (void)nxsig_usleep(AIDK_NFC_POWER_SETTLE_US);
  return OK;
}

static int aidk_nfc_configure_sideband(void)
{
  gpio_config_t config =
  {
    .io_mode = GPIO_INPUT_ENABLE,
    .pull_mode = GPIO_PULL_UP_EN,
    .func_mode = GPIO_SECOND_FUNC_DISABLE,
  };

  /* The SDK default table maps P53-P55 as LCD outputs.  On AIDK they are
   * MFRC522 outputs: IRQ plus the unused UART MX/DTRQ handshake pair.  Keep
   * the SoC side high-impedance to avoid electrical contention.  The NuttX
   * protocol driver polls MFRC522 registers, so no GPIO ISR is required.
   */

  if (gpio_dev_unmap((gpio_id_t)BK7258_BOARD_PIN_NFC_IRQ) != BK_OK ||
      bk_gpio_set_config((gpio_id_t)BK7258_BOARD_PIN_NFC_IRQ,
                         &config) != BK_OK)
    {
      return -EIO;
    }

  config.pull_mode = GPIO_PULL_DISABLE;
  if (gpio_dev_unmap((gpio_id_t)BK7258_BOARD_PIN_NFC_MX) != BK_OK ||
      bk_gpio_set_config((gpio_id_t)BK7258_BOARD_PIN_NFC_MX,
                         &config) != BK_OK ||
      gpio_dev_unmap((gpio_id_t)BK7258_BOARD_PIN_NFC_DTRQ) != BK_OK ||
      bk_gpio_set_config((gpio_id_t)BK7258_BOARD_PIN_NFC_DTRQ,
                         &config) != BK_OK)
    {
      return -EIO;
    }

  return OK;
}

static int aidk_nfc_uart_open(FAR struct aidk_nfc_uart_s *priv)
{
  struct termios term;
  int ret;

  ret = file_open(&priv->uart, AIDK_NFC_UART_DEVPATH,
                  O_RDWR | O_NONBLOCK);
  if (ret < 0)
    {
      return ret;
    }

  ret = file_ioctl(&priv->uart, TCGETS,
                   (unsigned long)(uintptr_t)&term);
  if (ret < 0)
    {
      goto errout;
    }

  cfmakeraw(&term);
  cfsetispeed(&term, AIDK_NFC_UART_BAUD);
  cfsetospeed(&term, AIDK_NFC_UART_BAUD);
  term.c_cflag |= CREAD | CLOCAL;

  ret = file_ioctl(&priv->uart, TCSETS,
                   (unsigned long)(uintptr_t)&term);
  if (ret < 0)
    {
      goto errout;
    }

  (void)file_ioctl(&priv->uart, TCFLSH, TCIOFLUSH);
  return OK;

errout:
  file_close(&priv->uart);
  return ret;
}

int bk7258_aidk_mfrc522_initialize(void)
{
  FAR struct aidk_nfc_uart_s *priv = &g_aidk_nfc_uart;
  uint8_t value;
  int ret;

  ret = bk_gpio_driver_init();
  if (ret != BK_OK)
    {
      return -EIO;
    }

  ret = aidk_nfc_configure_sideband();
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK MFRC522 sideband GPIO setup failed: %d\n", ret);
      return ret;
    }

  ret = aidk_nfc_power_on();
  if (ret < 0)
    {
      return ret;
    }

  ret = aidk_nfc_uart_open(priv);
  if (ret < 0)
    {
      return ret;
    }

  /* Match the official board driver: discard the two startup bytes seen on
   * SerialSpeedReg before the first software reset.
   */

  (void)aidk_nfc_uart_read_reg(priv, AIDK_NFC_REG_SERIAL_SPEED, &value);
  (void)aidk_nfc_uart_read_reg(priv, AIDK_NFC_REG_SERIAL_SPEED, &value);

  ret = mfrc522_register(AIDK_NFC_DEVPATH, &priv->spi);
  if (ret < 0)
    {
      file_close(&priv->uart);
      return ret;
    }

  /* The reset value exposes MX/DTRQ as RS232 handshake outputs.  The AIDK
   * design does not use hardware flow control; disable those outputs exactly
   * as the official SDK implementation does.
   */

  if (aidk_nfc_uart_read_reg(priv, AIDK_NFC_REG_TEST_PIN_EN, &value) == OK)
    {
      (void)aidk_nfc_uart_write_reg(
        priv, AIDK_NFC_REG_TEST_PIN_EN,
        value & ~AIDK_NFC_TEST_RS232_LINE_EN);
    }

  syslog(LOG_INFO, "AIDK MFRC522 registered: %s via UART1\n",
         AIDK_NFC_DEVPATH);
  return OK;
}

#endif /* CONFIG_BK7258_AIDK_MFRC522 */
