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
#include <nuttx/signal.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>

#include <arch/chip/bk7258_bt_ipc.h>
#ifdef CONFIG_BK7258_RPTUN_MBOX
#  include <arch/chip/bk7258_rptun.h>
#endif
#ifdef CONFIG_BK7258_BLE_GATT
#  include <arch/chip/bk7258_ble_gatt.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BT_H4_COMMAND             0x01u
#define BK7258_BT_H4_ACL                 0x02u
#define BK7258_BT_H4_SCO                 0x03u
#define BK7258_BT_H4_EVENT               0x04u

#define BK7258_BT_VENDOR_INIT            0x0001u
#define BK7258_BT_VENDOR_DEINIT          0x0002u
#define BK7258_BT_CONTROL_TIMEOUT_MS     25000u
#define BK7258_BT_CP_IPC_READY_MS        30000u

#define BK7258_BT_COMMAND_HEADER_SIZE    3u
#define BK7258_BT_EVENT_HEADER_SIZE      2u
#define BK7258_BT_ACL_HEADER_SIZE        4u

#define BK7258_BT_L2CAP_HEADER_SIZE      4u
#define BK7258_BT_L2CAP_CID_ATT          0x0004u
#define BK7258_BT_ATT_OP_MTU_REQ         0x02u
#define BK7258_BT_ATT_MTU_REQ_SIZE       3u
#define BK7258_BT_ATT_MAX_LE_MTU         517u
#define BK7258_BT_OP_HOST_NUM_COMPLETED  0x0c35u

#define BK7258_BT_EVT_DISCONNECTED       0x05u
#define BK7258_BT_EVT_COMMAND_COMPLETE   0x0eu
#define BK7258_BT_EVT_COMMAND_STATUS     0x0fu
#define BK7258_BT_EVT_LE_META            0x3eu
#define BK7258_BT_SUBEVT_LE_CONNECTED    0x01u
#define BK7258_BT_SUBEVT_LE_ENH_CONNECTED 0x0au

#if defined(CONFIG_BK7258_BLE_GATT) && \
    !defined(CONFIG_BLUETOOTH_CNTRL_HOST_FLOW_DISABLE)
#  error "BK7258 N13 requires Controller-to-Host flow control disabled"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*bk7258_bt_sdk_callback_t)(uint8_t *buffer, uint16_t length);

struct bk7258_bt_hci_s
{
  struct bt_driver_s driver;
  mutex_t            lock;
  sem_t              control_sem;
  struct bk7258_bt_hci_stats_s stats;
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
extern void bk7258_os_bt_ipc_init_begin(void);
extern void bk7258_os_bt_ipc_init_end(void);

#ifdef CONFIG_BK7258_BT_IPC_TRACE
struct bt_conn_s;

extern void __real_bt_conn_receive(struct bt_conn_s *conn,
                                   struct bt_buf_s *buf, uint8_t flags);
extern void bt_conn_release(struct bt_conn_s *conn);
extern void __real_bt_l2cap_receive(struct bt_conn_s *conn,
                                    struct bt_buf_s *buf);
extern void __real_bt_l2cap_send(struct bt_conn_s *conn, uint16_t cid,
                                 struct bt_buf_s *buf);
extern void __real_bt_conn_send(struct bt_conn_s *conn,
                                struct bt_buf_s *buf);
extern int __real_bt_send(struct bt_driver_s *driver,
                          struct bt_buf_s *buf);
extern void __real_bt_gatt_connected(struct bt_conn_s *conn);
extern void __real_bt_gatt_disconnected(struct bt_conn_s *conn);
extern struct bt_buf_s *__real_bt_l2cap_create_pdu(
  struct bt_conn_s *conn);
#endif

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

static void bk7258_bt_put_le16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static uint32_t bk7258_bt_pack_bytes(const uint8_t *data, size_t length)
{
  uint32_t value = 0;
  size_t i;

  if (length > sizeof(value))
    {
      length = sizeof(value);
    }

  for (i = 0; i < length; i++)
    {
      value |= (uint32_t)data[i] << (i * 8u);
    }

  return value;
}

#ifdef CONFIG_BK7258_BT_IPC_TRACE
static void bk7258_bt_trace_att(bool tx, uint16_t cid,
                                const uint8_t *data, size_t length)
{
  struct bk7258_bt_hci_stats_s *stats = &g_bk7258_bt_hci.stats;
  struct bk7258_bt_att_trace_s *entry;
  uint32_t sequence;
  uint32_t meta;

  if (cid != BK7258_BT_L2CAP_CID_ATT || data == NULL)
    {
      return;
    }

  if (length > BK7258_BT_ATT_TRACE_LENGTH_MASK)
    {
      length = BK7258_BT_ATT_TRACE_LENGTH_MASK;
    }

  sequence = __atomic_fetch_add(&stats->host_att_trace_sequence, 1u,
                                __ATOMIC_RELAXED);
  entry = &stats->host_att_trace[sequence % BK7258_BT_ATT_TRACE_DEPTH];
  meta = (tx ? BK7258_BT_ATT_TRACE_TX : 0u) |
         (uint32_t)length << BK7258_BT_ATT_TRACE_LENGTH_SHIFT | cid;

  __atomic_store_n(&entry->data0, bk7258_bt_pack_bytes(data, length),
                   __ATOMIC_RELAXED);
  __atomic_store_n(&entry->data1,
                   length > sizeof(uint32_t) ?
                     bk7258_bt_pack_bytes(data + sizeof(uint32_t),
                                           length - sizeof(uint32_t)) : 0u,
                   __ATOMIC_RELAXED);
  __atomic_store_n(&entry->meta, meta, __ATOMIC_RELEASE);
}
#endif

static void bk7258_bt_count_invalid_rx(struct bk7258_bt_hci_s *priv)
{
  __atomic_fetch_add(&priv->stats.invalid_rx, 1u, __ATOMIC_RELAXED);
}

static void bk7258_bt_control_sem_drain(struct bk7258_bt_hci_s *priv)
{
  while (nxsem_trywait(&priv->control_sem) == OK)
    {
    }
}

#ifdef CONFIG_BK7258_RPTUN_MBOX
static int bk7258_bt_wait_cp_ipc_ready(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  clock_t start = clock_systime_ticks();
  clock_t timeout = MSEC2TICK(BK7258_BT_CP_IPC_READY_MS);
  uint32_t flags;

  __asm volatile ("dmb sy" ::: "memory");
  if (control->magic != BK7258_RPTUN_CONTROL_MAGIC ||
      control->version != BK7258_RPTUN_CONTROL_VERSION ||
      control->size != sizeof(*control) ||
      control->generation != state->generation)
    {
      return -EPROTO;
    }

  /* Publish the AP endpoint only after bt_ipc_init() and its receive callback
   * are installed.  CP acknowledges that its matching IPC worker is ready;
   * the vendor-init request below then starts the Controller through the
   * official CP bt_ipc message path.
   */

  __atomic_fetch_or(&control->flags,
                    BK7258_RPTUN_FLAG_AP_BT_IPC_READY,
                    __ATOMIC_RELEASE);

  for (;;)
    {
      flags = __atomic_load_n(&control->flags, __ATOMIC_ACQUIRE);
      if ((flags & BK7258_RPTUN_FLAG_CP_BT_READY) != 0)
        {
          return OK;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (control->magic != BK7258_RPTUN_CONTROL_MAGIC ||
          control->generation != state->generation)
        {
          return -EPROTO;
        }

      if ((clock_t)(clock_systime_ticks() - start) >= timeout)
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(1000);
    }
}
#endif

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
  int ret;

  /* bt_ipc_core invokes this callback from its bt_ipc_thd worker, then frees
   * buffer as soon as the callback returns.  bt_netdev_receive() copies the
   * complete HCI packet synchronously before deferring stack processing.
   */

  if (!__atomic_load_n(&priv->opened, __ATOMIC_ACQUIRE) ||
      buffer == NULL || length < 1 || priv->driver.receive == NULL)
    {
      bk7258_bt_count_invalid_rx(priv);
      return;
    }

  switch (buffer[0])
    {
      case BK7258_BT_H4_EVENT:
        if (length < 1 + BK7258_BT_EVENT_HEADER_SIZE)
          {
            bk7258_bt_count_invalid_rx(priv);
            return;
          }

        payload_length = buffer[2];
        if ((size_t)length != 1 + BK7258_BT_EVENT_HEADER_SIZE +
                              payload_length)
          {
            bk7258_bt_count_invalid_rx(priv);
            return;
          }

        __atomic_fetch_add(&priv->stats.event_rx, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_event_header,
                         (uint32_t)buffer[1] |
                         (uint32_t)payload_length << 8,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_event_payload,
                         bk7258_bt_pack_bytes(&buffer[3], payload_length),
                         __ATOMIC_RELAXED);
        if (buffer[1] == BK7258_BT_EVT_COMMAND_COMPLETE &&
            payload_length >= 4u)
          {
            __atomic_fetch_add(&priv->stats.hci_cmd_complete, 1u,
                               __ATOMIC_RELAXED);
            __atomic_store_n(
              &priv->stats.last_cmd_complete,
              (uint32_t)bk7258_bt_get_le16(&buffer[4]) |
              (uint32_t)buffer[6] << 16 |
              (uint32_t)buffer[3] << 24,
              __ATOMIC_RELAXED);
          }
        else if (buffer[1] == BK7258_BT_EVT_COMMAND_STATUS &&
                 payload_length >= 4u)
          {
            __atomic_fetch_add(&priv->stats.hci_cmd_status, 1u,
                               __ATOMIC_RELAXED);
            __atomic_store_n(
              &priv->stats.last_cmd_status,
              (uint32_t)bk7258_bt_get_le16(&buffer[5]) |
              (uint32_t)buffer[3] << 16 |
              (uint32_t)buffer[4] << 24,
              __ATOMIC_RELAXED);
          }
        else if (buffer[1] == BK7258_BT_EVT_LE_META &&
                 payload_length >= 4u &&
            (buffer[3] == BK7258_BT_SUBEVT_LE_CONNECTED ||
             buffer[3] == BK7258_BT_SUBEVT_LE_ENH_CONNECTED) &&
            buffer[4] == 0u)
          {
            __atomic_fetch_add(&priv->stats.hci_le_connected, 1u,
                               __ATOMIC_RELAXED);
          }
        else if (buffer[1] == BK7258_BT_EVT_DISCONNECTED &&
                 payload_length >= 4u)
          {
            __atomic_fetch_add(&priv->stats.hci_disconnected, 1u,
                               __ATOMIC_RELAXED);
            __atomic_store_n(
              &priv->stats.last_disconnection,
              (uint32_t)buffer[3] |
              (uint32_t)bk7258_bt_get_le16(&buffer[4]) << 8 |
              (uint32_t)buffer[6] << 24,
              __ATOMIC_RELAXED);
          }
        type = BT_EVT;
        break;

      case BK7258_BT_H4_ACL:
        if (length < 1 + BK7258_BT_ACL_HEADER_SIZE)
          {
            bk7258_bt_count_invalid_rx(priv);
            return;
          }

        payload_length = bk7258_bt_get_le16(&buffer[3]);
        if ((size_t)length != 1 + BK7258_BT_ACL_HEADER_SIZE +
                              payload_length)
          {
            bk7258_bt_count_invalid_rx(priv);
            return;
          }

        __atomic_fetch_add(&priv->stats.acl_rx, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_acl_rx,
                         (uint32_t)bk7258_bt_get_le16(&buffer[1]) |
                         (uint32_t)payload_length << 16,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_acl_rx_payload0,
                         bk7258_bt_pack_bytes(&buffer[5], payload_length),
                         __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_acl_rx_payload1,
                         payload_length > sizeof(uint32_t) ?
                           bk7258_bt_pack_bytes(&buffer[9],
                             payload_length - sizeof(uint32_t)) : 0u,
                         __ATOMIC_RELAXED);
        type = BT_ACL_IN;
        break;

      case BK7258_BT_H4_SCO:
      default:
        bk7258_bt_count_invalid_rx(priv);
        return;
    }

  ret = bt_netdev_receive(&priv->driver, type, buffer + 1, length - 1);
  if (ret < 0)
    {
      __atomic_fetch_add(&priv->stats.receive_errors, 1u,
                         __ATOMIC_RELAXED);
    }
#ifdef CONFIG_BK7258_BLE_GATT
  else if (type == BT_EVT)
    {
      /* bt_netdev_receive() has synchronously copied the SDK-owned H4 event.
       * The observer only copies a fixed lifecycle token and never replaces
       * stock Host processing.
       */

      bk7258_ble_gatt_hci_event(buffer, length);
    }
#endif
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
  bk7258_os_bt_ipc_init_begin();
  bt_ipc_init();
  bk7258_os_bt_ipc_init_end();

#ifdef CONFIG_BK7258_RPTUN_MBOX
  ret = bk7258_bt_wait_cp_ipc_ready();
  if (ret < 0)
    {
      bt_ipc_register_hci_send_callback(NULL);
      nxmutex_unlock(&priv->lock);
      return ret;
    }
#endif

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
#ifdef CONFIG_BLUETOOTH_CNTRL_HOST_FLOW_DISABLE
        /* NuttX releases every BT_ACL_IN buffer by issuing Host Number Of
         * Completed Packets even when Controller-to-Host flow control was
         * disabled during HCI initialization.  BK7258 returns a non-standard
         * Command Complete (status 0x07, ncmd 0) for that command; enough ATT
         * traffic then consumes the command credit and prevents the stock
         * disconnect path from re-enabling advertising.  The Controller was
         * explicitly told not to use Host flow control, so drop only this
         * now-unnecessary acknowledgement at the board wrapper boundary.
         */

        if (value == BK7258_BT_OP_HOST_NUM_COMPLETED)
          {
            __atomic_fetch_add(
              &priv->stats.host_num_completed_dropped, 1u,
              __ATOMIC_RELAXED);
            return OK;
          }
#endif
        __atomic_fetch_add(&priv->stats.command_tx, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_command,
                         (uint32_t)value | (uint32_t)payload_length << 16,
                         __ATOMIC_RELAXED);
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
        __atomic_fetch_add(&priv->stats.acl_tx, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&priv->stats.last_acl_tx,
                         (uint32_t)value | (uint32_t)payload_length << 16,
                         __ATOMIC_RELAXED);
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

int bk7258_bt_hci_get_stats(struct bk7258_bt_hci_stats_s *stats)
{
  struct bk7258_bt_hci_stats_s *source = &g_bk7258_bt_hci.stats;
  unsigned int i;

  if (stats == NULL)
    {
      return -EINVAL;
    }

  stats->command_tx =
    __atomic_load_n(&source->command_tx, __ATOMIC_RELAXED);
  stats->acl_tx = __atomic_load_n(&source->acl_tx, __ATOMIC_RELAXED);
  stats->event_rx = __atomic_load_n(&source->event_rx, __ATOMIC_RELAXED);
  stats->acl_rx = __atomic_load_n(&source->acl_rx, __ATOMIC_RELAXED);
  stats->invalid_rx =
    __atomic_load_n(&source->invalid_rx, __ATOMIC_RELAXED);
  stats->receive_errors =
    __atomic_load_n(&source->receive_errors, __ATOMIC_RELAXED);
  stats->last_command =
    __atomic_load_n(&source->last_command, __ATOMIC_RELAXED);
  stats->last_acl_tx =
    __atomic_load_n(&source->last_acl_tx, __ATOMIC_RELAXED);
  stats->last_event_header =
    __atomic_load_n(&source->last_event_header, __ATOMIC_RELAXED);
  stats->last_event_payload =
    __atomic_load_n(&source->last_event_payload, __ATOMIC_RELAXED);
  stats->last_acl_rx =
    __atomic_load_n(&source->last_acl_rx, __ATOMIC_RELAXED);
  stats->last_acl_rx_payload0 =
    __atomic_load_n(&source->last_acl_rx_payload0, __ATOMIC_RELAXED);
  stats->last_acl_rx_payload1 =
    __atomic_load_n(&source->last_acl_rx_payload1, __ATOMIC_RELAXED);
  stats->host_conn_rx =
    __atomic_load_n(&source->host_conn_rx, __ATOMIC_RELAXED);
  stats->host_l2cap_rx =
    __atomic_load_n(&source->host_l2cap_rx, __ATOMIC_RELAXED);
  stats->host_l2cap_tx =
    __atomic_load_n(&source->host_l2cap_tx, __ATOMIC_RELAXED);
  stats->host_conn_tx =
    __atomic_load_n(&source->host_conn_tx, __ATOMIC_RELAXED);
  stats->host_bt_send_acl =
    __atomic_load_n(&source->host_bt_send_acl, __ATOMIC_RELAXED);
  stats->host_bt_send_errors =
    __atomic_load_n(&source->host_bt_send_errors, __ATOMIC_RELAXED);
  stats->last_l2cap_rx =
    __atomic_load_n(&source->last_l2cap_rx, __ATOMIC_RELAXED);
  stats->last_l2cap_tx =
    __atomic_load_n(&source->last_l2cap_tx, __ATOMIC_RELAXED);
  stats->host_gatt_connected =
    __atomic_load_n(&source->host_gatt_connected, __ATOMIC_RELAXED);
  stats->host_gatt_disconnected =
    __atomic_load_n(&source->host_gatt_disconnected, __ATOMIC_RELAXED);
  stats->host_pdu_alloc =
    __atomic_load_n(&source->host_pdu_alloc, __ATOMIC_RELAXED);
  stats->host_pdu_fail =
    __atomic_load_n(&source->host_pdu_fail, __ATOMIC_RELAXED);
  stats->host_mtu_clamped =
    __atomic_load_n(&source->host_mtu_clamped, __ATOMIC_RELAXED);
  stats->last_mtu_clamp =
    __atomic_load_n(&source->last_mtu_clamp, __ATOMIC_RELAXED);
  stats->hci_cmd_complete =
    __atomic_load_n(&source->hci_cmd_complete, __ATOMIC_RELAXED);
  stats->hci_cmd_status =
    __atomic_load_n(&source->hci_cmd_status, __ATOMIC_RELAXED);
  stats->last_cmd_complete =
    __atomic_load_n(&source->last_cmd_complete, __ATOMIC_RELAXED);
  stats->last_cmd_status =
    __atomic_load_n(&source->last_cmd_status, __ATOMIC_RELAXED);
  stats->host_num_completed_dropped =
    __atomic_load_n(&source->host_num_completed_dropped,
                    __ATOMIC_RELAXED);
  stats->hci_le_connected =
    __atomic_load_n(&source->hci_le_connected, __ATOMIC_RELAXED);
  stats->hci_disconnected =
    __atomic_load_n(&source->hci_disconnected, __ATOMIC_RELAXED);
  stats->last_disconnection =
    __atomic_load_n(&source->last_disconnection, __ATOMIC_RELAXED);
  stats->host_att_trace_sequence =
    __atomic_load_n(&source->host_att_trace_sequence, __ATOMIC_ACQUIRE);
  for (i = 0; i < BK7258_BT_ATT_TRACE_DEPTH; i++)
    {
      stats->host_att_trace[i].meta =
        __atomic_load_n(&source->host_att_trace[i].meta, __ATOMIC_ACQUIRE);
      stats->host_att_trace[i].data0 =
        __atomic_load_n(&source->host_att_trace[i].data0, __ATOMIC_RELAXED);
      stats->host_att_trace[i].data1 =
        __atomic_load_n(&source->host_att_trace[i].data1, __ATOMIC_RELAXED);
    }

  return OK;
}

#ifdef CONFIG_BK7258_BT_IPC_TRACE
void __wrap_bt_conn_receive(struct bt_conn_s *conn, struct bt_buf_s *buf,
                            uint8_t flags)
{
  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_conn_rx, 1u,
                     __ATOMIC_RELAXED);
  __real_bt_conn_receive(conn, buf, flags);

#ifdef CONFIG_BK7258_BT_CONN_RX_REF_COMPAT
  /* hci_acl() obtained this caller-owned reference with
   * bt_conn_lookup_handle().  The current stock NuttX path does not release
   * it after bt_conn_receive() returns.  Keep this paired release behind a
   * source-verified compatibility option so an upstream ownership change
   * cannot silently become a double release.
   */

  bt_conn_release(conn);
#endif
}

void __wrap_bt_l2cap_receive(struct bt_conn_s *conn, struct bt_buf_s *buf)
{
  uint32_t value = 0;

  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_l2cap_rx, 1u,
                     __ATOMIC_RELAXED);
  if (buf != NULL && buf->len >= 4u)
    {
      uint16_t l2cap_length = bk7258_bt_get_le16(buf->data);
      uint16_t cid = bk7258_bt_get_le16(buf->data + 2);
      size_t att_length = buf->len - BK7258_BT_L2CAP_HEADER_SIZE;

      if (att_length > l2cap_length)
        {
          att_length = l2cap_length;
        }

      value = (uint32_t)cid | (uint32_t)l2cap_length << 16;
      bk7258_bt_trace_att(false, cid,
                          buf->data + BK7258_BT_L2CAP_HEADER_SIZE,
                          att_length);

#ifdef CONFIG_BK7258_BT_ATT_MTU_COMPAT
      if (buf->len >= BK7258_BT_L2CAP_HEADER_SIZE +
                      BK7258_BT_ATT_MTU_REQ_SIZE &&
          bk7258_bt_get_le16(buf->data) == BK7258_BT_ATT_MTU_REQ_SIZE &&
          bk7258_bt_get_le16(buf->data + 2) == BK7258_BT_L2CAP_CID_ATT &&
          buf->data[BK7258_BT_L2CAP_HEADER_SIZE] ==
            BK7258_BT_ATT_OP_MTU_REQ)
        {
          uint8_t *mtu_data = buf->data + BK7258_BT_L2CAP_HEADER_SIZE + 1u;
          uint16_t peer_mtu = bk7258_bt_get_le16(mtu_data);

          if (peer_mtu > BK7258_BT_ATT_MAX_LE_MTU)
            {
              bk7258_bt_put_le16(mtu_data, BK7258_BT_ATT_MAX_LE_MTU);
              __atomic_fetch_add(
                &g_bk7258_bt_hci.stats.host_mtu_clamped, 1u,
                __ATOMIC_RELAXED);
              __atomic_store_n(
                &g_bk7258_bt_hci.stats.last_mtu_clamp,
                (uint32_t)peer_mtu << 16 | BK7258_BT_ATT_MAX_LE_MTU,
                __ATOMIC_RELAXED);
            }
        }
#endif
    }

  __atomic_store_n(&g_bk7258_bt_hci.stats.last_l2cap_rx, value,
                   __ATOMIC_RELAXED);
  __real_bt_l2cap_receive(conn, buf);
}

void __wrap_bt_l2cap_send(struct bt_conn_s *conn, uint16_t cid,
                          struct bt_buf_s *buf)
{
  uint32_t length = buf == NULL ? 0u : (uint32_t)buf->len;

  if (buf != NULL)
    {
      bk7258_bt_trace_att(true, cid, buf->data, buf->len);
    }

  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_l2cap_tx, 1u,
                     __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_bt_hci.stats.last_l2cap_tx,
                   (length << 16) | cid, __ATOMIC_RELAXED);
  __real_bt_l2cap_send(conn, cid, buf);
}

void __wrap_bt_conn_send(struct bt_conn_s *conn, struct bt_buf_s *buf)
{
  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_conn_tx, 1u,
                     __ATOMIC_RELAXED);
  __real_bt_conn_send(conn, buf);
}

int __wrap_bt_send(struct bt_driver_s *driver, struct bt_buf_s *buf)
{
  bool acl = buf != NULL && buf->type == BT_ACL_OUT;
  int ret;

  if (acl)
    {
      __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_bt_send_acl, 1u,
                         __ATOMIC_RELAXED);
    }

  ret = __real_bt_send(driver, buf);
  if (acl && ret < 0)
    {
      __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_bt_send_errors, 1u,
                         __ATOMIC_RELAXED);
    }

  return ret;
}

void __wrap_bt_gatt_connected(struct bt_conn_s *conn)
{
  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_gatt_connected, 1u,
                     __ATOMIC_RELAXED);
  __real_bt_gatt_connected(conn);
}

void __wrap_bt_gatt_disconnected(struct bt_conn_s *conn)
{
  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_gatt_disconnected, 1u,
                     __ATOMIC_RELAXED);
  __real_bt_gatt_disconnected(conn);
}

struct bt_buf_s *__wrap_bt_l2cap_create_pdu(struct bt_conn_s *conn)
{
  struct bt_buf_s *buf;

  __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_pdu_alloc, 1u,
                     __ATOMIC_RELAXED);
  buf = __real_bt_l2cap_create_pdu(conn);
  if (buf == NULL)
    {
      __atomic_fetch_add(&g_bk7258_bt_hci.stats.host_pdu_fail, 1u,
                         __ATOMIC_RELAXED);
    }

  return buf;
}
#endif

#endif /* CONFIG_BK7258_BT_IPC */
