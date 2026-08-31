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
#include <arch/chip/bk7258_pinmux.h>

#define AIDK_NFC_DEVPATH              "/dev/nfc0"
#define AIDK_NFC_UART_DEVPATH         "/dev/ttyS1"
#define AIDK_NFC_UART_BAUD            B9600
#define AIDK_NFC_UART_TIMEOUT_LOOPS   20
#define AIDK_NFC_POWER_SETTLE_US      10000
#define AIDK_NFC_UART_PROBE_ATTEMPTS  2
#define AIDK_NFC_UART_RESYNC_US       5000

#define AIDK_NFC_REG_SERIAL_SPEED     0x1f
#define AIDK_NFC_REG_TEST_PIN_EN      0x33
#define AIDK_NFC_REG_VERSION          0x37
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
  int ret;

  /* The external 3.3 V rail is owned by the SDK power manager and may be
   * serviced by CP.  Use the same module vote as the official AIDK NFC
   * implementation; direct AP GPIO ownership fails when CP owns LDO control.
   */

  ret = bk7258_shared_rail_vote(BK7258_SHARED_RAIL_NFC,
                                 BK7258_BOARD_PIN_LDO33_EN, true);
  if (ret < 0)
    {
      syslog(LOG_ERR, "AIDK MFRC522 LDO vote failed: %d\n", ret);
      return ret;
    }

  (void)nxsig_usleep(AIDK_NFC_POWER_SETTLE_US);
  return OK;
}

static int aidk_nfc_configure_sideband(void)
{
  /* The SDK default table maps P53-P55 as LCD outputs.  On AIDK they are
   * MFRC522 outputs: IRQ plus the unused UART MX/DTRQ handshake pair.  Keep
   * the SoC side high-impedance to avoid electrical contention.  The NuttX
   * protocol driver polls MFRC522 registers, so no GPIO ISR is required.
   */

  if (bk7258_gpio_configure_input(BK7258_BOARD_PIN_NFC_IRQ,
                                   BK7258_GPIO_PULL_UP) < 0)
    {
      return -EIO;
    }

  if (bk7258_gpio_configure_input(BK7258_BOARD_PIN_NFC_MX,
                                   BK7258_GPIO_PULL_NONE) < 0 ||
      bk7258_gpio_configure_input(BK7258_BOARD_PIN_NFC_DTRQ,
                                   BK7258_GPIO_PULL_NONE) < 0)
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

static bool aidk_nfc_version_supported(uint8_t version)
{
  switch (version)
    {
      case 0x88:
      case 0x90:
      case 0x91:
      case 0x92:
        return true;

      default:
        return false;
    }
}

static int aidk_nfc_uart_probe(FAR struct aidk_nfc_uart_s *priv,
                               FAR uint8_t *version)
{
  uint8_t speed[2] = {0, 0};
  int attempt;
  int ret = -ETIMEDOUT;

  for (attempt = 1; attempt <= AIDK_NFC_UART_PROBE_ATTEMPTS; attempt++)
    {
      speed[0] = 0;
      speed[1] = 0;
      *version = 0;

      /* Match the official reset path: consume two SerialSpeedReg replies
       * before the first software reset so that power-on UART noise cannot
       * be mistaken for a later register response.
       */

      (void)file_ioctl(&priv->uart, TCFLSH, TCIOFLUSH);
      ret = aidk_nfc_uart_read_reg(priv, AIDK_NFC_REG_SERIAL_SPEED,
                                   &speed[0]);
      if (ret >= 0)
        {
          ret = aidk_nfc_uart_read_reg(priv, AIDK_NFC_REG_SERIAL_SPEED,
                                       &speed[1]);
        }

      if (ret >= 0)
        {
          ret = aidk_nfc_uart_read_reg(priv, AIDK_NFC_REG_VERSION,
                                       version);
        }

      syslog(ret < 0 || !aidk_nfc_version_supported(*version) ?
             LOG_ERR : LOG_INFO,
             "AIDK MFRC522 UART probe: attempt=%d speed=%02x/%02x "
             "version=%02x ret=%d\n",
             attempt, speed[0], speed[1], *version, ret);

      if (ret >= 0 && aidk_nfc_version_supported(*version))
        {
          return OK;
        }

      if (ret >= 0)
        {
          ret = -ENODEV;
        }

      if (attempt < AIDK_NFC_UART_PROBE_ATTEMPTS)
        {
          (void)nxsig_usleep(AIDK_NFC_UART_RESYNC_US);
        }
    }

  return ret;
}

int bk7258_aidk_mfrc522_initialize(void)
{
  FAR struct aidk_nfc_uart_s *priv = &g_aidk_nfc_uart;
  uint8_t value;
  int ret;

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
      syslog(LOG_ERR, "AIDK MFRC522 UART1 open/config failed: %d\n", ret);
      return ret;
    }

  value = 0;
  ret = aidk_nfc_uart_probe(priv, &value);
  if (ret < 0)
    {
      file_close(&priv->uart);
      return ret;
    }

  priv->last_error = OK;
  ret = mfrc522_register(AIDK_NFC_DEVPATH, &priv->spi);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "AIDK MFRC522 protocol registration failed: ret=%d "
             "uart=%d probed_version=%02x\n",
             ret, priv->last_error, value);
      file_close(&priv->uart);
      return priv->last_error < 0 ? priv->last_error : ret;
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
