/****************************************************************************
 * board/bk7258_t5ai/chip/ap/bk7258_qspi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP QSPI lower-half for the standard NuttX qspi_dev_s contract.
 *
 * The implementation uses only the public BK7258 SDK v3.1.1.9 QSPI API:
 * bk_qspi_driver_init(), bk_qspi_init(), bk_qspi_command(),
 * bk_qspi_read(), and bk_qspi_write().  It intentionally does not call the
 * SDK's chip-specific qspi_flash helpers, enable a Flash QE bit, or expose a
 * private character-device ABI.
 *
 * The SDK QSPI path is synchronous and polls command completion.  Its FIFO
 * helpers access 32-bit words and the official Flash helpers cap one
 * indirect operation at 256 bytes.  This lower-half stages unaligned or
 * short buffers and limits writes to that boundary.  Only read memory
 * windows may be issued as multiple independently addressed transactions.
 * qspi_ll_wait_cmd_done() only logs a hardware timeout and the public
 * bk_qspi_command() still returns BK_OK, so this wrapper cannot report that
 * hardware condition as -ETIMEDOUT.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_QSPI

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/qspi.h>

#include <arch/chip/bk7258_qspi.h>

/* The v3.1.1.9 SDK bundle exports the QSPI data types, but its public
 * driver/qspi.h -> qspi_types.h include chain leaks the private qspi_hal.h
 * header, which is not shipped in the immutable bundle.  Keep this wrapper
 * on the exported types and declare only the five public ABI symbols it uses.
 * The symbols below are provided by the immutable libdriver.a; no SDK
 * implementation or private type is reproduced here.
 */

#include <common/bk_err.h>
#include <driver/hal/hal_qspi_types.h>

extern bk_err_t bk_qspi_driver_init(void);
extern bk_err_t bk_qspi_init(qspi_id_t id, const qspi_config_t *config);
extern bk_err_t bk_qspi_command(qspi_id_t id, const qspi_cmd_t *cmd);
extern bk_err_t bk_qspi_read(qspi_id_t id, void *data, uint32_t size);
extern bk_err_t bk_qspi_write(qspi_id_t id, const void *data, uint32_t size);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* qspi_flash.c in the immutable SDK uses this FIFO transaction size. */

#define BK7258_QSPI_FIFO_BYTES          256u
#define BK7258_QSPI_WORD_BYTES          4u
#define BK7258_QSPI_ADDRESS_LIMIT       0x01000000u
#define BK7258_QSPI_ADDRESS_MASK        (BK7258_QSPI_ADDRESS_LIMIT - 1u)

/* qspi_hw_t exposes a 7-bit dummy-clock field and qspi_cmd_t carries an
 * opcode in an 8-bit hardware field, even though the public types are wider.
 */

#define BK7258_QSPI_DUMMY_MAX           0x7fu
#define BK7258_QSPI_OPCODE_MAX          0xffu

/* qspi_ll_init_command() has explicit no-address encodings for only these
 * Flash opcodes.  All other QSPI_FLASH indirect commands unconditionally
 * transmit three address bytes, regardless of qspi_cmd_t contents.
 */

#define BK7258_QSPI_CMD_READ_STATUS_0   0x05u
#define BK7258_QSPI_CMD_READ_STATUS_1   0x35u
#define BK7258_QSPI_CMD_READ_ID         0x9fu
#define BK7258_QSPI_CMD_WRITE_STATUS_0  0x01u
#define BK7258_QSPI_CMD_WRITE_STATUS_1  0x31u
#define BK7258_QSPI_CMD_WRITE_ENABLE    0x06u

/* qspi_types.h owns these values, but cannot be included from the bundle
 * because of its private qspi_hal.h dependency.  Keep the values board-owned
 * and derive them only from the public QSPI error base.
 */

#define BK7258_SDK_ERR_QSPI_NOT_INIT    (BK_ERR_QSPI_BASE - 1)
#define BK7258_SDK_ERR_QSPI_ID_NOT_INIT (BK_ERR_QSPI_BASE - 2)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_qspi_priv_s
{
  struct qspi_dev_s dev;             /* NuttX-visible object */
  qspi_id_t         id;              /* BK7258 QSPI unit */
  uint32_t          frequency;       /* Fixed SDK configuration target */
  enum qspi_mode_e  mode;            /* Last NuttX mode request */
  int               bits;            /* Last NuttX bits request */
  bool              mode_valid;      /* SDK supports only mode 0 */
  bool              bits_valid;      /* SDK supports only 8 bits */
  bool              initialized;     /* bk_qspi_init() completed */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_qspi_lock(FAR struct qspi_dev_s *dev, bool lock);
static uint32_t bk7258_qspi_setfrequency(FAR struct qspi_dev_s *dev,
                                         uint32_t frequency);
static void bk7258_qspi_setmode(FAR struct qspi_dev_s *dev,
                                enum qspi_mode_e mode);
static void bk7258_qspi_setbits(FAR struct qspi_dev_s *dev, int nbits);
static int bk7258_qspi_command(FAR struct qspi_dev_s *dev,
                               FAR struct qspi_cmdinfo_s *cmdinfo);
static int bk7258_qspi_memory(FAR struct qspi_dev_s *dev,
                              FAR struct qspi_meminfo_s *meminfo);
static FAR void *bk7258_qspi_alloc(FAR struct qspi_dev_s *dev,
                                   size_t buflen);
static void bk7258_qspi_free(FAR struct qspi_dev_s *dev,
                             FAR void *buffer);

static int bk7258_qspi_map_error(bk_err_t error);
static void bk7258_qspi_invalidate_all(void);
static int bk7258_qspi_map_priv_error(
  FAR struct bk7258_qspi_priv_s *priv, bk_err_t error);
static int bk7258_qspi_validate_config(
  FAR const struct bk7258_qspi_priv_s *priv);
static int bk7258_qspi_init_locked(FAR struct bk7258_qspi_priv_s *priv);
static int bk7258_qspi_write_data(
                                  FAR struct bk7258_qspi_priv_s *priv,
                                  FAR const void *buffer,
                                  size_t buflen);
static int bk7258_qspi_read_data(
                                 FAR struct bk7258_qspi_priv_s *priv,
                                 FAR void *buffer,
                                 size_t buflen);
static bool bk7258_qspi_no_address_command(uint32_t opcode, bool read);
static bool bk7258_qspi_is_status_write(uint32_t opcode);
static int bk7258_qspi_validate_memory(FAR const struct qspi_meminfo_s *meminfo,
                                       qspi_wire_mode_t *wire_mode);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The SDK's qspi_hal.c keeps operation state globally and exposes no bus
 * mutex.  One NuttX mutex therefore serializes both QSPI units and all
 * lower-half transactions.  SDK LCD/PSRAM users remain outside this lock;
 * the board integration must not run those clients concurrently without a
 * corresponding board-level ownership policy.
 */

static mutex_t g_bk7258_qspi_lock = NXMUTEX_INITIALIZER;

static const struct qspi_ops_s g_bk7258_qspi_ops =
{
  .lock         = bk7258_qspi_lock,
  .setfrequency = bk7258_qspi_setfrequency,
  .setmode      = bk7258_qspi_setmode,
  .setbits      = bk7258_qspi_setbits,
#ifdef CONFIG_QSPI_HWFEATURES
  .hwfeatures   = NULL,
#endif
  .command      = bk7258_qspi_command,
  .memory       = bk7258_qspi_memory,
  .alloc        = bk7258_qspi_alloc,
  .free         = bk7258_qspi_free,
};

static struct bk7258_qspi_priv_s g_bk7258_qspi[BK7258_QSPI_UNIT_COUNT] =
{
  {
    .dev          = { .ops = &g_bk7258_qspi_ops },
    .id           = (qspi_id_t)0,
    .frequency    = BK7258_QSPI_DEFAULT_FREQUENCY,
    .mode         = BK7258_QSPI_DEFAULT_MODE,
    .bits         = BK7258_QSPI_DEFAULT_BITS,
    .mode_valid   = (BK7258_QSPI_DEFAULT_MODE == QSPIDEV_MODE0),
    .bits_valid   = (BK7258_QSPI_DEFAULT_BITS == 8),
    .initialized  = false,
  },
  {
    .dev          = { .ops = &g_bk7258_qspi_ops },
    .id           = (qspi_id_t)1,
    .frequency    = BK7258_QSPI_DEFAULT_FREQUENCY,
    .mode         = BK7258_QSPI_DEFAULT_MODE,
    .bits         = BK7258_QSPI_DEFAULT_BITS,
    .mode_valid   = (BK7258_QSPI_DEFAULT_MODE == QSPIDEV_MODE0),
    .bits_valid   = (BK7258_QSPI_DEFAULT_BITS == 8),
    .initialized  = false,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_qspi_map_error
 ****************************************************************************/

static int bk7258_qspi_map_error(bk_err_t error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  switch (error)
    {
      case BK7258_SDK_ERR_QSPI_NOT_INIT:
      case BK7258_SDK_ERR_QSPI_ID_NOT_INIT:
      case BK_ERR_NOT_INIT:
      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case BK_ERR_NULL_PARAM:
      case BK_ERR_PARAM:
        return -EINVAL;

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

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Name: bk7258_qspi_invalidate_all
 ****************************************************************************/

static void bk7258_qspi_invalidate_all(void)
{
  unsigned int i;

  for (i = 0; i < BK7258_QSPI_UNIT_COUNT; i++)
    {
      g_bk7258_qspi[i].initialized = false;
    }
}

/****************************************************************************
 * Name: bk7258_qspi_map_priv_error
 ****************************************************************************/

static int bk7258_qspi_map_priv_error(
  FAR struct bk7258_qspi_priv_s *priv, bk_err_t error)
{
  /* The SDK exposes no ownership-safe query for its global or per-unit
   * state.  If another AP client deinitializes either layer, invalidate our
   * cached state so a later transfer or explicit bk7258_qspi_initialize() can
   * retry.
   */

  if (error == BK7258_SDK_ERR_QSPI_NOT_INIT)
    {
      /* A global SDK deinit invalidates every unit's cached state. */

      bk7258_qspi_invalidate_all();
    }
  else if (error == BK7258_SDK_ERR_QSPI_ID_NOT_INIT)
    {
      priv->initialized = false;
    }

  return bk7258_qspi_map_error(error);
}

/****************************************************************************
 * Name: bk7258_qspi_validate_config
 ****************************************************************************/

static int bk7258_qspi_validate_config(
  FAR const struct bk7258_qspi_priv_s *priv)
{
  if (!priv->mode_valid || !priv->bits_valid)
    {
      /* The public SDK has no runtime CPOL/CPHA or word-width setter. */

      return -ENOTSUP;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_qspi_lock
 ****************************************************************************/

static int bk7258_qspi_lock(FAR struct qspi_dev_s *dev, bool lock)
{
  (void)dev;

  if (lock)
    {
      return nxmutex_lock(&g_bk7258_qspi_lock);
    }

  return nxmutex_unlock(&g_bk7258_qspi_lock);
}

/****************************************************************************
 * Name: bk7258_qspi_setfrequency
 ****************************************************************************/

static uint32_t bk7258_qspi_setfrequency(FAR struct qspi_dev_s *dev,
                                         uint32_t frequency)
{
  FAR struct bk7258_qspi_priv_s *priv =
    (FAR struct bk7258_qspi_priv_s *)dev;

  /* The public SDK has no runtime QSPI frequency setter.  bk_qspi_init()
   * uses the official generic Flash configuration (480M source, source
   * divider 4, controller divider 2), so preserve and report that fixed
   * target instead of claiming an unperformed reconfiguration.
   */

  (void)frequency;
  return priv->frequency;
}

/****************************************************************************
 * Name: bk7258_qspi_setmode
 ****************************************************************************/

static void bk7258_qspi_setmode(FAR struct qspi_dev_s *dev,
                                enum qspi_mode_e mode)
{
  /* qspi_config_t exposes no CPOL/CPHA fields and the public SDK has no
   * runtime mode setter.  The BK7258 QSPI reset/default mode is mode 0.
   */

  FAR struct bk7258_qspi_priv_s *priv =
    (FAR struct bk7258_qspi_priv_s *)dev;

  priv->mode = mode;
  priv->mode_valid = mode == QSPIDEV_MODE0;
}

/****************************************************************************
 * Name: bk7258_qspi_setbits
 ****************************************************************************/

static void bk7258_qspi_setbits(FAR struct qspi_dev_s *dev, int nbits)
{
  /* qspi_cmd_t is byte-oriented; the SDK has no word-width setter. */

  FAR struct bk7258_qspi_priv_s *priv =
    (FAR struct bk7258_qspi_priv_s *)dev;

  priv->bits = nbits;
  priv->bits_valid = nbits == 8;
}

/****************************************************************************
 * Name: bk7258_qspi_init_locked
 ****************************************************************************/

static int bk7258_qspi_init_locked(FAR struct bk7258_qspi_priv_s *priv)
{
  qspi_config_t config;
  bk_err_t error;

  if (priv->initialized)
    {
      return OK;
    }

  /* The SDK driver resource is global and idempotent.  Always repeat this
   * public init before a per-unit init: an external SDK deinit can make a
   * cached wrapper state stale, while the SDK call itself is safe to repeat.
   * There is deliberately no matching global deinit: the SDK state is shared
   * with other AP clients and NuttX QSPI has no ownership-safe shutdown
   * callback.
   */

  error = bk_qspi_driver_init();
  if (error != BK_OK)
    {
      return bk7258_qspi_map_error(error);
    }

  /* Match the v3.1.1.9 bk_qspi_flash_init() controller configuration without
   * selecting a Flash vendor, QE policy, address map, or command set.
   */

  config.src_clk     = QSPI_SCLK_480M;
  config.src_clk_div = 0x4;
  config.clk_div     = 0x2;

  error = bk_qspi_init(priv->id, &config);
  if (error != BK_OK)
    {
      /* bk_qspi_init() returns before changing hardware for its documented
       * parameter/driver-not-init errors.  Do not deinit the shared SDK
       * driver here; another AP client may own it.
       */

      return bk7258_qspi_map_error(error);
    }

  priv->initialized = true;
  return OK;
}

/****************************************************************************
 * Name: bk7258_qspi_write_data
 ****************************************************************************/

static int bk7258_qspi_write_data(
                                  FAR struct bk7258_qspi_priv_s *priv,
                                  FAR const void *buffer,
                                  size_t buflen)
{
  FAR void *staged = NULL;
  bool allocated = false;
  size_t padded;
  bk_err_t error;
  int ret;

  if (buffer == NULL || buflen == 0 || buflen > BK7258_QSPI_FIFO_BYTES)
    {
      return -EINVAL;
    }

  if ((((uintptr_t)buffer & (BK7258_QSPI_WORD_BYTES - 1u)) == 0) &&
      ((buflen & (BK7258_QSPI_WORD_BYTES - 1u)) == 0))
    {
      staged = (FAR void *)buffer;
    }
  else
    {
      if (buflen > SIZE_MAX - (BK7258_QSPI_WORD_BYTES - 1u))
        {
          return -EOVERFLOW;
        }

      padded = (buflen + (BK7258_QSPI_WORD_BYTES - 1u)) &
               ~(BK7258_QSPI_WORD_BYTES - 1u);
      staged = kmm_malloc(padded);
      if (staged == NULL)
        {
          return -ENOMEM;
        }

      memset(staged, 0, padded);
      memcpy(staged, buffer, buflen);
      allocated = true;
    }

  error = bk_qspi_write(priv->id, staged, (uint32_t)buflen);
  ret = bk7258_qspi_map_priv_error(priv, error);

  if (allocated)
    {
      kmm_free(staged);
    }

  return ret;
}

/****************************************************************************
 * Name: bk7258_qspi_read_data
 ****************************************************************************/

static int bk7258_qspi_read_data(
                                 FAR struct bk7258_qspi_priv_s *priv,
                                 FAR void *buffer,
                                 size_t buflen)
{
  FAR void *staged = NULL;
  bool allocated = false;
  size_t padded;
  bk_err_t error;
  int ret;

  if (buffer == NULL || buflen == 0 || buflen > BK7258_QSPI_FIFO_BYTES)
    {
      return -EINVAL;
    }

  if ((((uintptr_t)buffer & (BK7258_QSPI_WORD_BYTES - 1u)) == 0) &&
      ((buflen & (BK7258_QSPI_WORD_BYTES - 1u)) == 0))
    {
      staged = buffer;
    }
  else
    {
      if (buflen > SIZE_MAX - (BK7258_QSPI_WORD_BYTES - 1u))
        {
          return -EOVERFLOW;
        }

      padded = (buflen + (BK7258_QSPI_WORD_BYTES - 1u)) &
               ~(BK7258_QSPI_WORD_BYTES - 1u);
      staged = kmm_malloc(padded);
      if (staged == NULL)
        {
          return -ENOMEM;
        }

      allocated = true;
    }

  error = bk_qspi_read(priv->id, staged, (uint32_t)buflen);
  ret = bk7258_qspi_map_priv_error(priv, error);

  if (ret >= 0 && allocated)
    {
      memcpy(buffer, staged, buflen);
    }

  if (allocated)
    {
      kmm_free(staged);
    }

  return ret;
}

/****************************************************************************
 * Name: bk7258_qspi_no_address_command
 ****************************************************************************/

static bool bk7258_qspi_no_address_command(uint32_t opcode, bool read)
{
  if (read)
    {
      return opcode == BK7258_QSPI_CMD_READ_STATUS_0 ||
             opcode == BK7258_QSPI_CMD_READ_STATUS_1 ||
             opcode == BK7258_QSPI_CMD_READ_ID;
    }

  return opcode == BK7258_QSPI_CMD_WRITE_STATUS_0 ||
         opcode == BK7258_QSPI_CMD_WRITE_STATUS_1 ||
         opcode == BK7258_QSPI_CMD_WRITE_ENABLE;
}

/****************************************************************************
 * Name: bk7258_qspi_is_status_write
 ****************************************************************************/

static bool bk7258_qspi_is_status_write(uint32_t opcode)
{
  return opcode == BK7258_QSPI_CMD_WRITE_STATUS_0 ||
         opcode == BK7258_QSPI_CMD_WRITE_STATUS_1;
}

/****************************************************************************
 * Name: bk7258_qspi_command
 ****************************************************************************/

static int bk7258_qspi_command(FAR struct qspi_dev_s *dev,
                               FAR struct qspi_cmdinfo_s *cmdinfo)
{
  FAR struct bk7258_qspi_priv_s *priv =
    (FAR struct bk7258_qspi_priv_s *)dev;
  qspi_cmd_t command;
  FAR const uint8_t *status_data;
  uint8_t data_flags;
  bool encoded_status_write = false;
  bk_err_t error;
  int ret;

  if (cmdinfo == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_qspi_validate_config(priv);
  if (ret < 0)
    {
      return ret;
    }

  if ((cmdinfo->flags & ~(QSPICMD_ADDRESS | QSPICMD_READDATA |
                          QSPICMD_WRITEDATA | QSPICMD_IDUAL |
                          QSPICMD_IQUAD)) != 0)
    {
      return -EINVAL;
    }

  data_flags = cmdinfo->flags & (QSPICMD_READDATA | QSPICMD_WRITEDATA);
  if (data_flags == (QSPICMD_READDATA | QSPICMD_WRITEDATA))
    {
      return -EINVAL;
    }

  if (cmdinfo->cmd > BK7258_QSPI_OPCODE_MAX)
    {
      return -ENOTSUP;
    }

  if ((cmdinfo->flags & (QSPICMD_IDUAL | QSPICMD_IQUAD)) != 0)
    {
      /* qspi_cmd_t selects the data lines only; the SDK cannot express a
       * separate dual/quad instruction line.
       */

      return -ENOTSUP;
    }

  if (cmdinfo->buflen > BK7258_QSPI_FIFO_BYTES)
    {
      /* A command is one chip-select transaction; it cannot be split here. */

      return -E2BIG;
    }

  if (data_flags != 0 &&
      (cmdinfo->buflen == 0 || cmdinfo->buffer == NULL))
    {
      return -EINVAL;
    }

  if (QSPICMD_ISADDRESS(cmdinfo->flags))
    {
      if (bk7258_qspi_no_address_command(cmdinfo->cmd,
                                         QSPICMD_ISREAD(cmdinfo->flags)))
        {
          /* These SDK encodings intentionally omit the address. */

          return -ENOTSUP;
        }

      if (cmdinfo->addrlen != 3)
        {
          /* BK7258 qspi_ll_init_command() always emits exactly 3 address
           * bytes for QSPI_FLASH indirect commands.
           */

          return -ENOTSUP;
        }

      if (cmdinfo->addr > BK7258_QSPI_ADDRESS_MASK)
        {
          return -EOVERFLOW;
        }
    }
  else if (cmdinfo->addrlen != 0 || cmdinfo->addr != 0)
    {
      return -EINVAL;
    }
  else if (!bk7258_qspi_no_address_command(
             cmdinfo->cmd, QSPICMD_ISREAD(cmdinfo->flags)))
    {
      /* For every other opcode the immutable SDK emits three address bytes
       * even when NuttX requested none.  Refuse that mismatch instead of
       * silently putting an unintended address on the bus.
       */

      return -ENOTSUP;
    }

  if (cmdinfo->cmd == BK7258_QSPI_CMD_WRITE_ENABLE &&
      QSPICMD_ISDATA(cmdinfo->flags))
    {
      return -EINVAL;
    }

  if (!priv->initialized)
    {
      /* QSPI upper-half callers hold g_bk7258_qspi_lock around command
       * sequences.  Retry an SDK/per-unit initialization that was
       * unavailable earlier or was invalidated by a shared-client deinit.
       */

      ret = bk7258_qspi_init_locked(priv);
      if (ret < 0)
        {
          return ret;
        }
    }

  memset(&command, 0, sizeof(command));
  command.device    = QSPI_FLASH;
  command.work_mode = INDIRECT_MODE;
  command.op        = QSPICMD_ISREAD(cmdinfo->flags) ? QSPI_READ : QSPI_WRITE;
  command.cmd       = cmdinfo->cmd;
  command.addr      = cmdinfo->addr;
  command.data_len  = cmdinfo->buflen;
  command.wire_mode = QSPI_1WIRE;

  /* qspi_ll_init_command() implements WRSR/WRVSR specially: the status byte
   * is carried in qspi_cmd_t.cmd[15:8], and the command has no FIFO payload.
   * Adapt the standard NuttX write-data form to that immutable SDK encoding
   * so MTD status-register writes retain their normal semantics.  The SDK's
   * second status-byte form is selected only when cmd[23:16] is nonzero, so
   * it cannot represent a two-byte value whose high byte is zero without
   * changing the data on the wire; reject multi-byte requests here.
   */

  if (QSPICMD_ISWRITE(cmdinfo->flags) &&
      bk7258_qspi_is_status_write(cmdinfo->cmd))
    {
      if (cmdinfo->buflen > 1)
        {
          return -ENOTSUP;
        }

      status_data = (FAR const uint8_t *)cmdinfo->buffer;
      if (cmdinfo->buflen > 0)
        {
          command.cmd |= (uint32_t)status_data[0] << 8;
        }

      command.data_len = 0;
      encoded_status_write = true;
    }

  /* The SDK API requires write FIFO data before command start. */

  if (QSPICMD_ISWRITE(cmdinfo->flags) && !encoded_status_write)
    {
      ret = bk7258_qspi_write_data(priv, cmdinfo->buffer,
                                   cmdinfo->buflen);
      if (ret < 0)
        {
          return ret;
        }
    }

  error = bk_qspi_command(priv->id, &command);
  ret = bk7258_qspi_map_priv_error(priv, error);
  if (ret < 0)
    {
      return ret;
    }

  /* Read FIFO data is valid after the synchronous command completes. */

  if (QSPICMD_ISREAD(cmdinfo->flags))
    {
      ret = bk7258_qspi_read_data(priv, cmdinfo->buffer,
                                  cmdinfo->buflen);
    }

  return ret;
}

/****************************************************************************
 * Name: bk7258_qspi_validate_memory
 ****************************************************************************/

static int bk7258_qspi_validate_memory(FAR const struct qspi_meminfo_s *meminfo,
                                       qspi_wire_mode_t *wire_mode)
{
  if (meminfo == NULL || wire_mode == NULL || meminfo->buffer == NULL ||
      meminfo->buflen == 0)
    {
      return -EINVAL;
    }

  if ((meminfo->flags & ~(QSPIMEM_WRITE | QSPIMEM_DUALIO |
                          QSPIMEM_QUADIO | QSPIMEM_SCRAMBLE |
                          QSPIMEM_RANDOM | QSPIMEM_IDUAL |
                          QSPIMEM_IQUAD)) != 0)
    {
      return -EINVAL;
    }

  if (meminfo->cmd > BK7258_QSPI_OPCODE_MAX)
    {
      return -ENOTSUP;
    }

  if (bk7258_qspi_no_address_command(meminfo->cmd,
                                     QSPIMEM_ISREAD(meminfo->flags)))
    {
      /* qspi_meminfo_s describes an addressed memory operation here. */

      return -ENOTSUP;
    }

  if (meminfo->addrlen != 3)
    {
      /* The BK7258 SDK QSPI_FLASH path has no 4-byte address mode. */

      return -ENOTSUP;
    }

  if (meminfo->addr > BK7258_QSPI_ADDRESS_MASK ||
      meminfo->buflen > BK7258_QSPI_ADDRESS_LIMIT - meminfo->addr)
    {
      return -EOVERFLOW;
    }

  if (meminfo->dummies > BK7258_QSPI_DUMMY_MAX)
    {
      return -ENOTSUP;
    }

  if (QSPIMEM_ISWRITE(meminfo->flags) &&
      meminfo->buflen > BK7258_QSPI_FIFO_BYTES)
    {
      /* A write must remain one chip-select/FIFO transaction.  Splitting it
       * here would require a per-page WREN/WIP protocol that this generic
       * lower-half cannot infer from qspi_meminfo_s.
       */

      return -E2BIG;
    }

  if ((meminfo->flags & (QSPIMEM_SCRAMBLE | QSPIMEM_RANDOM)) != 0)
    {
      /* The public BK7258 qspi_cmd_t has no scrambler controls. */

      return -ENOTSUP;
    }

  if ((meminfo->flags & (QSPIMEM_IDUAL | QSPIMEM_IQUAD)) != 0)
    {
      /* Instruction width is not independently expressible in qspi_cmd_t. */

      return -ENOTSUP;
    }

  if (QSPIMEM_ISDUALIO(meminfo->flags))
    {
      /* BK7258 qspi_ll selects dual data lines only as part of a command
       * encoding that is not the NuttX DUALIO address/data contract.
       */

      return -ENOTSUP;
    }

  if (QSPIMEM_ISWRITE(meminfo->flags) &&
      QSPIMEM_ISQUADIO(meminfo->flags))
    {
      /* NuttX defines DUALIO/QUADIO as read transfer modes. */

      return -EINVAL;
    }

  *wire_mode = QSPIMEM_ISQUADIO(meminfo->flags) ? QSPI_4WIRE : QSPI_1WIRE;
  return OK;
}

/****************************************************************************
 * Name: bk7258_qspi_memory
 ****************************************************************************/

static int bk7258_qspi_memory(FAR struct qspi_dev_s *dev,
                              FAR struct qspi_meminfo_s *meminfo)
{
  FAR struct bk7258_qspi_priv_s *priv =
    (FAR struct bk7258_qspi_priv_s *)dev;
  FAR uint8_t *buffer;
  qspi_wire_mode_t wire_mode;
  qspi_cmd_t command;
  uint32_t offset = 0;
  uint32_t remaining;
  uint32_t chunk;
  bk_err_t error;
  int ret;

  ret = bk7258_qspi_validate_memory(meminfo, &wire_mode);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_qspi_validate_config(priv);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->initialized)
    {
      /* See bk7258_qspi_command(): the standard QSPI contract serializes
       * this path with lock(), allowing safe lazy retry under the mutex.
       */

      ret = bk7258_qspi_init_locked(priv);
      if (ret < 0)
        {
          return ret;
        }
    }

  buffer = (FAR uint8_t *)meminfo->buffer;
  remaining = meminfo->buflen;

  /* Writes were limited to one 256-byte transaction by validation above.
   * Reads alone may use multiple independently addressed transactions: each
   * chunk is a new, re-addressed read of a contiguous memory window, not one
   * persistent chip-select transaction.
   */

  while (remaining > 0)
    {
      chunk = remaining > BK7258_QSPI_FIFO_BYTES ?
              BK7258_QSPI_FIFO_BYTES : remaining;

      memset(&command, 0, sizeof(command));
      command.device     = QSPI_FLASH;
      command.work_mode  = INDIRECT_MODE;
      command.op         = QSPIMEM_ISREAD(meminfo->flags) ? QSPI_READ :
                           QSPI_WRITE;
      command.cmd        = meminfo->cmd;
      command.addr       = meminfo->addr + offset;
      command.dummy_cycle = meminfo->dummies;
      command.data_len   = chunk;
      command.wire_mode  = wire_mode;

      if (QSPIMEM_ISWRITE(meminfo->flags))
        {
          ret = bk7258_qspi_write_data(priv, buffer + offset, chunk);
          if (ret < 0)
            {
              return ret;
            }
        }

      error = bk_qspi_command(priv->id, &command);
      ret = bk7258_qspi_map_priv_error(priv, error);
      if (ret < 0)
        {
          return ret;
        }

      if (QSPIMEM_ISREAD(meminfo->flags))
        {
          ret = bk7258_qspi_read_data(priv, buffer + offset, chunk);
          if (ret < 0)
            {
              return ret;
            }
        }

      offset += chunk;
      remaining -= chunk;
    }

  return OK;
}

/****************************************************************************
 * Name: bk7258_qspi_alloc
 ****************************************************************************/

static FAR void *bk7258_qspi_alloc(FAR struct qspi_dev_s *dev,
                                   size_t buflen)
{
  size_t padded;

  (void)dev;

  if (buflen == 0 || buflen > SIZE_MAX - (BK7258_QSPI_WORD_BYTES - 1u))
    {
      return NULL;
    }

  padded = (buflen + (BK7258_QSPI_WORD_BYTES - 1u)) &
           ~(BK7258_QSPI_WORD_BYTES - 1u);
  return kmm_malloc(padded);
}

/****************************************************************************
 * Name: bk7258_qspi_free
 ****************************************************************************/

static void bk7258_qspi_free(FAR struct qspi_dev_s *dev, FAR void *buffer)
{
  (void)dev;
  if (buffer != NULL)
    {
      kmm_free(buffer);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_qspi_initialize
 ****************************************************************************/

FAR struct qspi_dev_s *bk7258_qspi_initialize(int intf)
{
  FAR struct bk7258_qspi_priv_s *priv;
  int ret;

  if (intf < 0 || intf >= BK7258_QSPI_UNIT_COUNT)
    {
      return NULL;
    }

  priv = &g_bk7258_qspi[intf];

  ret = nxmutex_lock(&g_bk7258_qspi_lock);
  if (ret < 0)
    {
      return NULL;
    }

  ret = bk7258_qspi_init_locked(priv);
  nxmutex_unlock(&g_bk7258_qspi_lock);

  return ret < 0 ? NULL : &priv->dev;
}

#endif /* CONFIG_BK7258_QSPI */
