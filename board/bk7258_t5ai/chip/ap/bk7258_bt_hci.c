/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/
 * bk7258_bt_hci.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX bt_driver_s lower half over the Beken AP-side Bluetooth mailbox IPC.
 * The Beken object owns MB_CHNL_BT_CMD, its pointer-return protocol and its
 * deferred receive thread.  This board wrapper owns only HCI framing and the
 * NuttX driver registration boundary.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_BT_IPC

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>

#include <arch/chip/bk7258_bt_ipc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BT_H4_COMMAND             0x01u
#define BK7258_BT_H4_ACL                 0x02u
#define BK7258_BT_H4_SCO                 0x03u
#define BK7258_BT_H4_EVENT               0x04u

#define BK7258_BT_VENDOR_INIT            0x0001u
#define BK7258_BT_VENDOR_DEINIT          0x0002u
#define BK7258_BT_CONTROL_TIMEOUT_MS     5000u

#define BK7258_BT_COMMAND_HEADER_SIZE    3u
#define BK7258_BT_EVENT_HEADER_SIZE      2u
#define BK7258_BT_ACL_HEADER_SIZE        4u

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*bk7258_bt_sdk_callback_t)(uint8_t *buffer, uint16_t length);

struct bk7258_bt_hci_s
{
  struct bt_driver_s driver;
  mutex_t            lock;
  sem_t              control_sem;
  bool               sem_ready;
  bool               opened;
  bool               registered;
};

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* These functions are exported by the AP SDK's libbk_bluetooth.a.  Keep the
 * declarations local: bt_ipc_core.h is an SDK-private header and is not part
 * of the copied public SDK include bundle.
 */

extern void bt_ipc_init(void);
extern void bt_ipc_hci_send_vendor_cmd(uint8_t *data, uint16_t length);
extern void bt_ipc_hci_send_cmd(uint16_t opcode, uint8_t *data,
                                uint16_t length);
extern void bt_ipc_hci_send_acl_data(uint16_t handle_flags, uint8_t *data,
                                     uint16_t length);
extern void bt_ipc_register_hci_send_callback(
  bk7258_bt_sdk_callback_t callback);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_bt_open(struct bt_driver_s *driver);
static int bk7258_bt_send(struct bt_driver_s *driver,
                          enum bt_buf_type_e type, void *data,
                          size_t length);
static void bk7258_bt_close(struct bt_driver_s *driver);
static int bk7258_bt_ioctl(struct bt_driver_s *driver, int command,
                           unsigned long argument);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_bt_hci_s g_bk7258_bt_hci =
{
  .lock = NXMUTEX_INITIALIZER,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint16_t bk7258_bt_get_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static void bk7258_bt_control_sem_drain(struct bk7258_bt_hci_s *priv)
{
  while (nxsem_trywait(&priv->control_sem) == OK)
    {
    }
}

static int bk7258_bt_control_request(struct bk7258_bt_hci_s *priv,
                                     uint16_t subopcode)
{
  uint8_t command[2];

  /* Match the vendor SDK wire ABI exactly.  Commands encode the vendor
   * subopcode most-significant byte first; the SDK consumes the matching
   * status event internally and calls bk_bluetooth_init_deinit_compelete().
   */

  command[0] = (uint8_t)(subopcode >> 8);
  command[1] = (uint8_t)subopcode;
  bk7258_bt_control_sem_drain(priv);
  bt_ipc_hci_send_vendor_cmd(command, sizeof(command));

  return nxsem_tickwait_uninterruptible(
           &priv->control_sem,
           MSEC2TICK(BK7258_BT_CONTROL_TIMEOUT_MS));
}

static void bk7258_bt_sdk_receive(uint8_t *buffer, uint16_t length)
{
  struct bk7258_bt_hci_s *priv = &g_bk7258_bt_hci;
  enum bt_buf_type_e type;
  uint16_t payload_length;

  /* bt_ipc_core invokes this callback from its bt_ipc_thd worker, then frees
   * buffer as soon as the callback returns.  bt_netdev_receive() copies the
   * complete HCI packet synchronously before deferring stack processing.
   */

  if (!__atomic_load_n(&priv->opened, __ATOMIC_ACQUIRE) ||
      buffer == NULL || length < 1 || priv->driver.receive == NULL)
    {
      return;
    }

  switch (buffer[0])
    {
      case BK7258_BT_H4_EVENT:
        if (length < 1 + BK7258_BT_EVENT_HEADER_SIZE)
          {
            return;
          }

        payload_length = buffer[2];
        if ((size_t)length != 1 + BK7258_BT_EVENT_HEADER_SIZE +
                              payload_length)
          {
            return;
          }

        type = BT_EVT;
        break;

      case BK7258_BT_H4_ACL:
        if (length < 1 + BK7258_BT_ACL_HEADER_SIZE)
          {
            return;
          }

        payload_length = bk7258_bt_get_le16(&buffer[3]);
        if ((size_t)length != 1 + BK7258_BT_ACL_HEADER_SIZE +
                              payload_length)
          {
            return;
          }

        type = BT_ACL_IN;
        break;

      case BK7258_BT_H4_SCO:
      default:
        return;
    }

  (void)bt_netdev_receive(&priv->driver, type, buffer + 1, length - 1);
}

static int bk7258_bt_open(struct bt_driver_s *driver)
{
  struct bk7258_bt_hci_s *priv = (struct bk7258_bt_hci_s *)driver;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  bt_ipc_register_hci_send_callback(bk7258_bt_sdk_receive);
  bt_ipc_init();

  ret = bk7258_bt_control_request(priv, BK7258_BT_VENDOR_INIT);
  if (ret < 0)
    {
      bt_ipc_register_hci_send_callback(NULL);
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  __atomic_store_n(&priv->opened, true, __ATOMIC_RELEASE);
  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_bt_send(struct bt_driver_s *driver,
                          enum bt_buf_type_e type, void *data, size_t length)
{
  struct bk7258_bt_hci_s *priv = (struct bk7258_bt_hci_s *)driver;
  uint8_t *packet = data;
  uint16_t payload_length;
  uint16_t value;

  if (!__atomic_load_n(&priv->opened, __ATOMIC_ACQUIRE))
    {
      return -ENETDOWN;
    }

  if (packet == NULL)
    {
      return -EINVAL;
    }

  switch (type)
    {
      case BT_CMD:
        if (length < BK7258_BT_COMMAND_HEADER_SIZE)
          {
            return -EMSGSIZE;
          }

        payload_length = packet[2];
        if (length != BK7258_BT_COMMAND_HEADER_SIZE + payload_length)
          {
            return -EMSGSIZE;
          }

        value = bk7258_bt_get_le16(packet);
        bt_ipc_hci_send_cmd(value,
                            packet + BK7258_BT_COMMAND_HEADER_SIZE,
                            payload_length);
        return OK;

      case BT_ACL_OUT:
        if (length < BK7258_BT_ACL_HEADER_SIZE)
          {
            return -EMSGSIZE;
          }

        payload_length = bk7258_bt_get_le16(&packet[2]);
        if (length != BK7258_BT_ACL_HEADER_SIZE + payload_length)
          {
            return -EMSGSIZE;
          }

        value = bk7258_bt_get_le16(packet);
        bt_ipc_hci_send_acl_data(value,
                                 packet + BK7258_BT_ACL_HEADER_SIZE,
                                 payload_length);
        return OK;

      case BT_ISO_OUT:
      default:
        return -EOPNOTSUPP;
    }
}

static void bk7258_bt_close(struct bt_driver_s *driver)
{
  struct bk7258_bt_hci_s *priv = (struct bk7258_bt_hci_s *)driver;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return;
    }

  if (priv->opened)
    {
      __atomic_store_n(&priv->opened, false, __ATOMIC_RELEASE);
      (void)bk7258_bt_control_request(priv, BK7258_BT_VENDOR_DEINIT);
    }

  bt_ipc_register_hci_send_callback(NULL);
  nxmutex_unlock(&priv->lock);
}

static int bk7258_bt_ioctl(struct bt_driver_s *driver, int command,
                           unsigned long argument)
{
  (void)driver;
  (void)command;
  (void)argument;
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* This misspelled symbol is the callback ABI exported by the official AP
 * bt_ipc_core object.  It is called only for a successful controller init or
 * deinit vendor response, from bt_ipc_thd rather than mailbox interrupt
 * context.
 */

void bk_bluetooth_init_deinit_compelete(void)
{
  struct bk7258_bt_hci_s *priv = &g_bk7258_bt_hci;

  if (__atomic_load_n(&priv->sem_ready, __ATOMIC_ACQUIRE))
    {
      nxsem_post(&priv->control_sem);
    }
}

int bk7258_bt_hci_initialize(void)
{
  struct bk7258_bt_hci_s *priv = &g_bk7258_bt_hci;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->registered)
    {
      nxmutex_unlock(&priv->lock);
      return OK;
    }

  ret = nxsem_init(&priv->control_sem, 0, 0);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  __atomic_store_n(&priv->sem_ready, true, __ATOMIC_RELEASE);
  priv->driver.head_reserve = 0;
  priv->driver.open         = bk7258_bt_open;
  priv->driver.send         = bk7258_bt_send;
  priv->driver.close        = bk7258_bt_close;
  priv->driver.ioctl        = bk7258_bt_ioctl;
  nxmutex_unlock(&priv->lock);

  /* bt_driver_register() enters bt_initialize() synchronously, which calls
   * bk7258_bt_open().  Do not hold priv->lock across this call.
   */

  ret = bt_driver_register(&priv->driver);
  if (ret < 0)
    {
      bk7258_bt_close(&priv->driver);
      __atomic_store_n(&priv->sem_ready, false, __ATOMIC_RELEASE);
      nxsem_destroy(&priv->control_sem);
      return ret;
    }

  priv->registered = true;
  return OK;
}

#endif /* CONFIG_BK7258_BT_IPC */
