/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/
 * bk7258_ble_gatt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned N13 BLE peripheral service over the unmodified NuttX GATT
 * server.  ATT callbacks are bounded producers; all lifecycle commands and
 * notification work run in one logical-CPU0 kernel thread.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_BLE_GATT

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/wireless/bluetooth/bt_core.h>
#include <nuttx/wireless/bluetooth/bt_gatt.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/wireless/bluetooth/bt_uuid.h>

#include <arch/chip/bk7258_ble_gatt.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BLE_GATT_WORKER_NAME          "bk7258-ble-gatt"
#define BK7258_BLE_GATT_EVENT_QUEUE_DEPTH    8u
#define BK7258_BLE_GATT_INIT_TIMEOUT_MS      5000u

/* The pinned v3.1.1.9 AP SDK maps BT_IPC_TASK_PRIO=4 to NuttX priority 98.
 * The transport callback must return its SDK-owned pointer before LPWORK can
 * process the copied HCI event and enqueue a command back to CP.
 */

#define BK7258_BLE_GATT_BT_IPC_PRIORITY      98

#define BK7258_BLE_H4_EVENT                  0x04u
#define BK7258_BLE_H4_EVENT_HEADER_SIZE      3u
#define BK7258_BLE_EVT_DISCONNECTED          0x05u
#define BK7258_BLE_EVT_LE_META               0x3eu
#define BK7258_BLE_SUBEVT_LE_CONNECTED       0x01u
#define BK7258_BLE_SUBEVT_LE_ENH_CONNECTED   0x0au

#define BK7258_BLE_FRAME_MAGIC_OFFSET        0u
#define BK7258_BLE_FRAME_VERSION_OFFSET      4u
#define BK7258_BLE_FRAME_OPCODE_OFFSET       5u
#define BK7258_BLE_FRAME_COUNT_OFFSET        6u
#define BK7258_BLE_FRAME_SEQUENCE_OFFSET     8u
#define BK7258_BLE_FRAME_VALUE_OFFSET        12u

static_assert(sizeof(CONFIG_DEVICE_LOCAL_NAME) <= 29u,
              "N13 local name exceeds a legacy scan-response element");
static_assert(sizeof(CONFIG_DEVICE_NAME) <= UINT8_MAX,
              "N13 GAP Device Name exceeds the current GATT read ABI");
static_assert(CONFIG_BLUETOOTH_MAX_CONN == 1,
              "N13 current CCC/notify contract is single-connection only");

#ifdef CONFIG_SCHED_LPWORKPRIORITY
static_assert(CONFIG_SCHED_LPWORKPRIORITY <
              BK7258_BLE_GATT_BT_IPC_PRIORITY,
              "N13 LPWORK must remain below the SDK Bluetooth IPC thread");
static_assert(CONFIG_BK7258_BLE_GATT_PRIORITY <
              CONFIG_SCHED_LPWORKPRIORITY,
              "N13 worker must remain below Bluetooth LPWORK");
#endif

static_assert(CONFIG_BLUETOOTH_TXCMD_PRIORITY >
              BK7258_BLE_GATT_BT_IPC_PRIORITY,
              "N13 HCI TX must remain above the SDK Bluetooth IPC thread");

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum bk7258_ble_gatt_event_type_e
{
  BK7258_BLE_GATT_EVENT_INITIALIZE = 1,
  BK7258_BLE_GATT_EVENT_CONNECTED,
  BK7258_BLE_GATT_EVENT_DISCONNECTED,
  BK7258_BLE_GATT_EVENT_REQUEST
};

struct bk7258_ble_gatt_event_s
{
  uint8_t type;
  uint8_t reason;
  uint16_t handle;
  struct bk7258_ble_gatt_frame_s frame;
};

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* The current NuttX checkout exposes its GATT database API publicly but
 * keeps advertising lifecycle declarations in bt_hcicore.h.  Declare only
 * the two pinned symbols used by this board wrapper; do not include or edit
 * the private upstream header.
 */

extern int bt_start_advertising(uint8_t type,
                                const struct bt_eir_s *ad,
                                const struct bt_eir_s *sd);
extern int bt_stop_advertising(void);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_ble_gatt_read_name(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset);
static int bk7258_ble_gatt_read_appearance(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset);
static int bk7258_ble_gatt_read_control(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset);
static int bk7258_ble_gatt_write_control(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr,
  const void *buffer, uint8_t length, uint16_t offset);
static int bk7258_ble_gatt_read_status(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset);
static void bk7258_ble_gatt_ccc_changed(uint16_t value);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bt_uuid_s g_gap_uuid =
{
  .type = BT_UUID_16,
  .u.u16 = BT_UUID_GAP,
};

static struct bt_uuid_s g_name_uuid =
{
  .type = BT_UUID_16,
  .u.u16 = BT_UUID_GAP_DEVICE_NAME,
};

static struct bt_uuid_s g_appearance_uuid =
{
  .type = BT_UUID_16,
  .u.u16 = BT_UUID_GAP_APPEARANCE,
};

/* Bluetooth transmits UUID128 values least-significant octet first. */

static struct bt_uuid_s g_service_uuid =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x00, 0x45, 0x4c, 0x42, 0x5f, 0x54, 0x54, 0x41,
    0x47, 0x33, 0x31, 0x4e, 0x01, 0x00, 0x58, 0x72
  },
};

static struct bt_uuid_s g_control_uuid =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x00, 0x45, 0x4c, 0x42, 0x5f, 0x54, 0x54, 0x41,
    0x47, 0x33, 0x31, 0x4e, 0x02, 0x00, 0x58, 0x72
  },
};

static struct bt_uuid_s g_status_uuid =
{
  .type = BT_UUID_128,
  .u.u128 =
  {
    0x00, 0x45, 0x4c, 0x42, 0x5f, 0x54, 0x54, 0x41,
    0x47, 0x33, 0x31, 0x4e, 0x03, 0x00, 0x58, 0x72
  },
};

static struct bt_gatt_chrc_s g_name_chrc =
{
  .properties = BT_GATT_CHRC_READ,
  .value_handle = BK7258_BLE_GATT_NAME_VALUE_HANDLE,
  .uuid = &g_name_uuid,
};

static struct bt_gatt_chrc_s g_appearance_chrc =
{
  .properties = BT_GATT_CHRC_READ,
  .value_handle = BK7258_BLE_GATT_APPEARANCE_VALUE_HANDLE,
  .uuid = &g_appearance_uuid,
};

static struct bt_gatt_chrc_s g_control_chrc =
{
  .properties = BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
  .value_handle = BK7258_BLE_GATT_CONTROL_VALUE_HANDLE,
  .uuid = &g_control_uuid,
};

static struct bt_gatt_chrc_s g_status_chrc =
{
  .properties = BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
  .value_handle = BK7258_BLE_GATT_STATUS_VALUE_HANDLE,
  .uuid = &g_status_uuid,
};

static struct bt_gatt_ccc_cfg_s g_status_ccc[1];

static const struct bt_gatt_attr_s g_attributes[] =
{
  BT_GATT_PRIMARY_SERVICE(BK7258_BLE_GATT_GAP_SERVICE_HANDLE,
                          &g_gap_uuid),
  BT_GATT_CHARACTERISTIC(BK7258_BLE_GATT_NAME_CHRC_HANDLE,
                         &g_name_chrc),
  BT_GATT_DESCRIPTOR(BK7258_BLE_GATT_NAME_VALUE_HANDLE, &g_name_uuid,
                     BT_GATT_PERM_READ, bk7258_ble_gatt_read_name, NULL,
                     (void *)CONFIG_DEVICE_NAME),
  BT_GATT_CHARACTERISTIC(BK7258_BLE_GATT_APPEARANCE_CHRC_HANDLE,
                         &g_appearance_chrc),
  BT_GATT_DESCRIPTOR(BK7258_BLE_GATT_APPEARANCE_VALUE_HANDLE,
                     &g_appearance_uuid, BT_GATT_PERM_READ,
                     bk7258_ble_gatt_read_appearance, NULL, NULL),
  BT_GATT_PRIMARY_SERVICE(BK7258_BLE_GATT_SERVICE_HANDLE,
                          &g_service_uuid),
  BT_GATT_CHARACTERISTIC(BK7258_BLE_GATT_CONTROL_CHRC_HANDLE,
                         &g_control_chrc),
  BT_GATT_DESCRIPTOR(BK7258_BLE_GATT_CONTROL_VALUE_HANDLE,
                     &g_control_uuid,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     bk7258_ble_gatt_read_control,
                     bk7258_ble_gatt_write_control, NULL),
  BT_GATT_CHARACTERISTIC(BK7258_BLE_GATT_STATUS_CHRC_HANDLE,
                         &g_status_chrc),
  BT_GATT_DESCRIPTOR(BK7258_BLE_GATT_STATUS_VALUE_HANDLE,
                     &g_status_uuid, BT_GATT_PERM_READ,
                     bk7258_ble_gatt_read_status, NULL, NULL),
  BT_GATT_CCC(BK7258_BLE_GATT_STATUS_CCC_HANDLE,
              BK7258_BLE_GATT_STATUS_VALUE_HANDLE,
              g_status_ccc, bk7258_ble_gatt_ccc_changed),
};

static const struct bt_eir_s g_advertising_data[] =
{
  {
    .len = 2,
    .type = BT_EIR_FLAGS,
    .data = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR },
  },
  {
    .len = 17,
    .type = BT_EIR_UUID128_ALL,
    .data =
    {
      0x00, 0x45, 0x4c, 0x42, 0x5f, 0x54, 0x54, 0x41,
      0x47, 0x33, 0x31, 0x4e, 0x01, 0x00, 0x58, 0x72
    },
  },
  {
    .len = 0,
  },
};

static const struct bt_eir_s g_scan_response_data[] =
{
  {
    .len = sizeof(CONFIG_DEVICE_LOCAL_NAME),
    .type = BT_EIR_NAME_COMPLETE,
    .data = CONFIG_DEVICE_LOCAL_NAME,
  },
  {
    .len = 0,
  },
};

static sem_t g_event_sem;
static sem_t g_init_sem;
static spinlock_t g_lock = SP_UNLOCKED;
static struct bk7258_ble_gatt_event_s
  g_events[BK7258_BLE_GATT_EVENT_QUEUE_DEPTH];
static struct bk7258_ble_gatt_frame_s g_control_frame;
static struct bk7258_ble_gatt_frame_s g_status_frame;
static uint8_t g_event_head;
static uint8_t g_event_tail;
static uint8_t g_event_count;
static int g_init_result;
static pid_t g_worker = INVALID_PROCESS_ID;
static bool g_queue_ready;
static bool g_initialized;
static struct bk7258_ble_gatt_stats_s g_stats =
{
  .state = BK7258_BLE_GATT_STATE_DISABLED,
  .worker_cpu = UINT32_MAX,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint16_t bk7258_ble_gatt_get_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t bk7258_ble_gatt_get_le32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         (uint32_t)data[1] << 8 |
         (uint32_t)data[2] << 16 |
         (uint32_t)data[3] << 24;
}

static void bk7258_ble_gatt_put_le16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static void bk7258_ble_gatt_put_le32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

static uint32_t bk7258_ble_gatt_crc32(const uint8_t *data, size_t length)
{
  uint32_t crc = UINT32_MAX;
  unsigned int bit;
  size_t i;

  for (i = 0; i < length; i++)
    {
      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^
                (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }

  return crc ^ UINT32_MAX;
}

static void bk7258_ble_gatt_frame_finalize(
  struct bk7258_ble_gatt_frame_s *frame)
{
  bk7258_ble_gatt_put_le32(
    &frame->data[BK7258_BLE_GATT_FRAME_CRC_OFFSET],
    bk7258_ble_gatt_crc32(frame->data,
                         BK7258_BLE_GATT_FRAME_CRC_OFFSET));
}

static void bk7258_ble_gatt_frame_initialize(
  struct bk7258_ble_gatt_frame_s *frame, uint8_t opcode, uint16_t count,
  uint32_t sequence, uint32_t value)
{
  memset(frame, 0, sizeof(*frame));
  bk7258_ble_gatt_put_le32(&frame->data[BK7258_BLE_FRAME_MAGIC_OFFSET],
                          BK7258_BLE_GATT_FRAME_MAGIC);
  frame->data[BK7258_BLE_FRAME_VERSION_OFFSET] =
    BK7258_BLE_GATT_FRAME_VERSION;
  frame->data[BK7258_BLE_FRAME_OPCODE_OFFSET] = opcode;
  bk7258_ble_gatt_put_le16(&frame->data[BK7258_BLE_FRAME_COUNT_OFFSET],
                           count);
  bk7258_ble_gatt_put_le32(&frame->data[BK7258_BLE_FRAME_SEQUENCE_OFFSET],
                           sequence);
  bk7258_ble_gatt_put_le32(&frame->data[BK7258_BLE_FRAME_VALUE_OFFSET],
                           value);
  bk7258_ble_gatt_frame_finalize(frame);
}

static void bk7258_ble_gatt_set_state(enum bk7258_ble_gatt_state_e state,
                                      int error)
{
  __atomic_store_n(&g_stats.last_error, error, __ATOMIC_RELAXED);
  __atomic_store_n(&g_stats.state, (uint32_t)state, __ATOMIC_RELEASE);
}

static bool bk7258_ble_gatt_queue(
  const struct bk7258_ble_gatt_event_s *event)
{
  irqstate_t flags;
  bool queued = false;

  if (!__atomic_load_n(&g_queue_ready, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  flags = spin_lock_irqsave(&g_lock);
  if (g_event_count < BK7258_BLE_GATT_EVENT_QUEUE_DEPTH)
    {
      g_events[g_event_tail] = *event;
      g_event_tail = (uint8_t)((g_event_tail + 1u) %
                               BK7258_BLE_GATT_EVENT_QUEUE_DEPTH);
      g_event_count++;
      queued = true;
    }
  spin_unlock_irqrestore(&g_lock, flags);

  if (queued)
    {
      nxsem_post(&g_event_sem);
    }
  else
    {
      __atomic_fetch_add(&g_stats.queue_full, 1u, __ATOMIC_RELAXED);
    }

  return queued;
}

static bool bk7258_ble_gatt_dequeue(struct bk7258_ble_gatt_event_s *event)
{
  irqstate_t flags;
  bool dequeued = false;

  flags = spin_lock_irqsave(&g_lock);
  if (g_event_count > 0)
    {
      *event = g_events[g_event_head];
      g_event_head = (uint8_t)((g_event_head + 1u) %
                               BK7258_BLE_GATT_EVENT_QUEUE_DEPTH);
      g_event_count--;
      dequeued = true;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  return dequeued;
}

static void bk7258_ble_gatt_store_frame(
  struct bk7258_ble_gatt_frame_s *destination,
  const struct bk7258_ble_gatt_frame_s *source)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_lock);
  *destination = *source;
  spin_unlock_irqrestore(&g_lock, flags);
}

static void bk7258_ble_gatt_load_frame(
  struct bk7258_ble_gatt_frame_s *destination,
  const struct bk7258_ble_gatt_frame_s *source)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_lock);
  *destination = *source;
  spin_unlock_irqrestore(&g_lock, flags);
}

static int bk7258_ble_gatt_read_name(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset)
{
  const char *name = attr->user_data;

  return bt_gatt_attr_read(conn, attr, buffer, length, offset, name,
                           (uint8_t)strlen(name));
}

static int bk7258_ble_gatt_read_appearance(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset)
{
  uint16_t appearance = BT_HOST2LE16(CONFIG_DEVICE_APPEARANCE);

  return bt_gatt_attr_read(conn, attr, buffer, length, offset, &appearance,
                           sizeof(appearance));
}

static int bk7258_ble_gatt_read_frame(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset,
  const struct bk7258_ble_gatt_frame_s *source)
{
  struct bk7258_ble_gatt_frame_s snapshot;

  bk7258_ble_gatt_load_frame(&snapshot, source);
  return bt_gatt_attr_read(conn, attr, buffer, length, offset,
                           snapshot.data, sizeof(snapshot.data));
}

static int bk7258_ble_gatt_read_control(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset)
{
  return bk7258_ble_gatt_read_frame(conn, attr, buffer, length, offset,
                                    &g_control_frame);
}

static int bk7258_ble_gatt_read_status(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr, void *buffer,
  uint8_t length, uint16_t offset)
{
  return bk7258_ble_gatt_read_frame(conn, attr, buffer, length, offset,
                                    &g_status_frame);
}

static int bk7258_ble_gatt_write_control(
  struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr,
  const void *buffer, uint8_t length, uint16_t offset)
{
  const uint8_t *data = buffer;
  struct bk7258_ble_gatt_event_s event;
  uint16_t count;
  uint32_t expected_crc;
  uint32_t actual_crc;
  uint8_t opcode;

  (void)conn;
  (void)attr;

  if (offset != 0)
    {
      __atomic_fetch_add(&g_stats.writes_bad_offset, 1u,
                         __ATOMIC_RELAXED);
      return -EINVAL;
    }

  if (length != BK7258_BLE_GATT_FRAME_SIZE)
    {
      __atomic_fetch_add(&g_stats.writes_bad_length, 1u,
                         __ATOMIC_RELAXED);
      return -EFBIG;
    }

  if (bk7258_ble_gatt_get_le32(&data[BK7258_BLE_FRAME_MAGIC_OFFSET]) !=
      BK7258_BLE_GATT_FRAME_MAGIC)
    {
      __atomic_fetch_add(&g_stats.writes_bad_magic, 1u, __ATOMIC_RELAXED);
      return -EBADMSG;
    }

  if (data[BK7258_BLE_FRAME_VERSION_OFFSET] !=
      BK7258_BLE_GATT_FRAME_VERSION)
    {
      __atomic_fetch_add(&g_stats.writes_bad_version, 1u,
                         __ATOMIC_RELAXED);
      return -EBADMSG;
    }

  opcode = data[BK7258_BLE_FRAME_OPCODE_OFFSET];
  if (opcode != BK7258_BLE_GATT_OPCODE_ECHO &&
      opcode != BK7258_BLE_GATT_OPCODE_BURST)
    {
      __atomic_fetch_add(&g_stats.writes_bad_opcode, 1u,
                         __ATOMIC_RELAXED);
      return -EOPNOTSUPP;
    }

  count = bk7258_ble_gatt_get_le16(
    &data[BK7258_BLE_FRAME_COUNT_OFFSET]);
  if ((opcode == BK7258_BLE_GATT_OPCODE_ECHO && count != 1u) ||
      (opcode == BK7258_BLE_GATT_OPCODE_BURST &&
       (count == 0u || count > BK7258_BLE_GATT_MAX_BURST)))
    {
      __atomic_fetch_add(&g_stats.writes_bad_count, 1u,
                         __ATOMIC_RELAXED);
      return -ERANGE;
    }

  expected_crc = bk7258_ble_gatt_get_le32(
    &data[BK7258_BLE_GATT_FRAME_CRC_OFFSET]);
  actual_crc = bk7258_ble_gatt_crc32(
    data, BK7258_BLE_GATT_FRAME_CRC_OFFSET);
  if (expected_crc != actual_crc)
    {
      __atomic_fetch_add(&g_stats.writes_bad_crc, 1u, __ATOMIC_RELAXED);
      return -EBADMSG;
    }

  memset(&event, 0, sizeof(event));
  event.type = BK7258_BLE_GATT_EVENT_REQUEST;
  memcpy(event.frame.data, data, sizeof(event.frame.data));
  if (!bk7258_ble_gatt_queue(&event))
    {
      return -EAGAIN;
    }

  bk7258_ble_gatt_store_frame(&g_control_frame, &event.frame);
  __atomic_fetch_add(&g_stats.writes_accepted, 1u, __ATOMIC_RELAXED);
  return length;
}

static void bk7258_ble_gatt_ccc_changed(uint16_t value)
{
  __atomic_store_n(&g_stats.subscribed,
                   value == BT_GATT_CCC_NOTIFY ? 1u : 0u,
                   __ATOMIC_RELEASE);
  __atomic_fetch_add(&g_stats.ccc_changes, 1u, __ATOMIC_RELAXED);
}

static int bk7258_ble_gatt_start(void)
{
  return bt_start_advertising(BT_LE_ADV_IND, g_advertising_data,
                              g_scan_response_data);
}

static void bk7258_ble_gatt_process_initialize(void)
{
  int ret;

  bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_INITIALIZING, 0);

  ret = bt_stop_advertising();
  if (ret != OK && ret != -EALREADY)
    {
      goto failed;
    }

  bt_gatt_register(g_attributes, nitems(g_attributes));
  ret = bk7258_ble_gatt_start();
  if (ret != OK)
    {
      goto failed;
    }

  bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_ADVERTISING, 0);
  g_init_result = OK;
  nxsem_post(&g_init_sem);
  return;

failed:
  bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_FAULTED, ret);
  g_init_result = ret;
  nxsem_post(&g_init_sem);
}

static void bk7258_ble_gatt_process_connected(
  const struct bk7258_ble_gatt_event_s *event)
{
  int ret;

  /* Legacy advertising stops automatically when the Controller accepts a
   * connection, but the current NuttX Host leaves g_btdev.adv_enable set.
   * Synchronize that Host-side state here, after stock LPWORK has processed
   * Connection Complete.  Otherwise hci_disconn_complete() races us by
   * sending an automatic enable before the board worker can perform the
   * official-sample-style restart.
   */

  ret = bt_stop_advertising();
  if (ret != OK && ret != -EALREADY && ret != -EIO)
    {
      bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_FAULTED, ret);
      return;
    }

  /* A successful LE Connection Complete proves that legacy advertising has
   * already stopped in the Controller.  The stock API clears its stale
   * g_btdev.adv_enable flag before sending a redundant disable command, but
   * maps every non-zero HCI status for that command to -EIO.  Accept -EIO only
   * at this post-connection synchronization point; allocation, queue and
   * timeout failures remain fatal.
   */

  __atomic_store_n(&g_stats.active_handle, event->handle,
                   __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_stats.connected, 1u, __ATOMIC_RELAXED);
  bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_CONNECTED, 0);
}

static void bk7258_ble_gatt_process_disconnected(
  const struct bk7258_ble_gatt_event_s *event)
{
  int ret;

  (void)event;
  __atomic_store_n(&g_stats.active_handle, 0u, __ATOMIC_RELAXED);
  __atomic_store_n(&g_stats.subscribed, 0u, __ATOMIC_RELEASE);
  __atomic_fetch_add(&g_stats.disconnected, 1u, __ATOMIC_RELAXED);

  /* The connection worker cleared NuttX's stale advertising-enabled state,
   * so stock hci_disconn_complete() only performs ATT/connection cleanup and
   * does not send an automatic enable.  Match the official SDK sample by
   * starting advertising exactly once from this deferred worker.
   *
   * The priority chain HCI TX > SDK Bluetooth IPC > LPWORK > this worker
   * ensures the SDK-owned receive pointer is returned before any restart
   * command enters the mailbox wrapper.  Controller-to-Host flow control is
   * disabled and its redundant 0x0c35 acknowledgements are filtered by the
   * board HCI lower half, so this synchronous sequence retains command credit.
   */

  ret = bk7258_ble_gatt_start();
  if (ret != OK)
    {
      bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_FAULTED, ret);
      return;
    }

  __atomic_fetch_add(&g_stats.readvertised, 1u, __ATOMIC_RELAXED);
  bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_ADVERTISING, 0);
}

static void bk7258_ble_gatt_publish_response(
  struct bk7258_ble_gatt_frame_s *response)
{
  bk7258_ble_gatt_frame_finalize(response);
  bk7258_ble_gatt_store_frame(&g_status_frame, response);

  if (__atomic_load_n(&g_stats.subscribed, __ATOMIC_ACQUIRE) != 0u)
    {
      bt_gatt_notify(BK7258_BLE_GATT_STATUS_VALUE_HANDLE,
                     response->data, sizeof(response->data));
      __atomic_fetch_add(&g_stats.notify_attempted, 1u,
                         __ATOMIC_RELAXED);
    }
}

static void bk7258_ble_gatt_process_request(
  const struct bk7258_ble_gatt_event_s *event)
{
  struct bk7258_ble_gatt_frame_s response = event->frame;
  uint32_t sequence;
  uint32_t value;
  uint16_t count;
  uint16_t i;
  uint8_t opcode;

  opcode = event->frame.data[BK7258_BLE_FRAME_OPCODE_OFFSET];
  count = bk7258_ble_gatt_get_le16(
    &event->frame.data[BK7258_BLE_FRAME_COUNT_OFFSET]);
  sequence = bk7258_ble_gatt_get_le32(
    &event->frame.data[BK7258_BLE_FRAME_SEQUENCE_OFFSET]);
  value = bk7258_ble_gatt_get_le32(
    &event->frame.data[BK7258_BLE_FRAME_VALUE_OFFSET]);
  response.data[BK7258_BLE_FRAME_OPCODE_OFFSET] =
    opcode | BK7258_BLE_GATT_RESPONSE_BIT;

  if (opcode == BK7258_BLE_GATT_OPCODE_ECHO)
    {
      bk7258_ble_gatt_publish_response(&response);
      return;
    }

  for (i = 0; i < count; i++)
    {
      if (__atomic_load_n(&g_stats.subscribed, __ATOMIC_ACQUIRE) == 0u)
        {
          break;
        }

      bk7258_ble_gatt_put_le32(
        &response.data[BK7258_BLE_FRAME_SEQUENCE_OFFSET], sequence + i);
      bk7258_ble_gatt_put_le32(
        &response.data[BK7258_BLE_FRAME_VALUE_OFFSET], value + i);
      bk7258_ble_gatt_publish_response(&response);
      if (i + 1u < count)
        {
          (void)nxsig_usleep(
            CONFIG_BK7258_BLE_GATT_NOTIFY_INTERVAL_MS * 1000u);
        }
    }
}

static int bk7258_ble_gatt_worker(int argc, char *argv[])
{
  struct bk7258_ble_gatt_event_s event;
  int cpu;

  (void)argc;
  (void)argv;
  cpu = sched_getcpu();
  __atomic_store_n(&g_stats.worker_cpu,
                   cpu >= 0 ? (uint32_t)cpu : UINT32_MAX,
                   __ATOMIC_RELAXED);

  for (; ; )
    {
      if (nxsem_wait_uninterruptible(&g_event_sem) < 0)
        {
          continue;
        }

      while (bk7258_ble_gatt_dequeue(&event))
        {
          switch (event.type)
            {
              case BK7258_BLE_GATT_EVENT_INITIALIZE:
                bk7258_ble_gatt_process_initialize();
                break;

              case BK7258_BLE_GATT_EVENT_CONNECTED:
                bk7258_ble_gatt_process_connected(&event);
                break;

              case BK7258_BLE_GATT_EVENT_DISCONNECTED:
                bk7258_ble_gatt_process_disconnected(&event);
                break;

              case BK7258_BLE_GATT_EVENT_REQUEST:
                bk7258_ble_gatt_process_request(&event);
                break;

              default:
                break;
            }
        }
    }

  return EXIT_SUCCESS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ble_gatt_initialize(void)
{
  struct bk7258_ble_gatt_event_s event;
  cpu_set_t cpuset;
  int ret;

  if (g_initialized)
    {
      return -EALREADY;
    }

  memset(g_status_ccc, 0, sizeof(g_status_ccc));
  memset(&g_control_frame, 0, sizeof(g_control_frame));
  bk7258_ble_gatt_frame_initialize(
    &g_status_frame,
    BK7258_BLE_GATT_OPCODE_ECHO | BK7258_BLE_GATT_RESPONSE_BIT,
    0, 0, BK7258_BLE_GATT_STATE_INITIALIZING);
  g_event_head = 0;
  g_event_tail = 0;
  g_event_count = 0;
  g_init_result = -EINPROGRESS;

  ret = nxsem_init(&g_event_sem, 0, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = nxsem_init(&g_init_sem, 0, 0);
  if (ret < 0)
    {
      nxsem_destroy(&g_event_sem);
      return ret;
    }

  __atomic_store_n(&g_queue_ready, true, __ATOMIC_RELEASE);
  g_worker = kthread_create(BK7258_BLE_GATT_WORKER_NAME,
                            CONFIG_BK7258_BLE_GATT_PRIORITY,
                            CONFIG_BK7258_BLE_GATT_STACKSIZE,
                            bk7258_ble_gatt_worker, NULL);
  if (g_worker < 0)
    {
      ret = (int)g_worker;
      __atomic_store_n(&g_queue_ready, false, __ATOMIC_RELEASE);
      nxsem_destroy(&g_init_sem);
      nxsem_destroy(&g_event_sem);
      return ret;
    }

#ifdef CONFIG_SMP
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);
  ret = sched_setaffinity(g_worker, sizeof(cpuset), &cpuset);
  if (ret < 0)
    {
      kthread_delete(g_worker);
      g_worker = INVALID_PROCESS_ID;
      __atomic_store_n(&g_queue_ready, false, __ATOMIC_RELEASE);
      nxsem_destroy(&g_init_sem);
      nxsem_destroy(&g_event_sem);
      return ret;
    }
#else
  (void)cpuset;
#endif

  memset(&event, 0, sizeof(event));
  event.type = BK7258_BLE_GATT_EVENT_INITIALIZE;
  if (!bk7258_ble_gatt_queue(&event))
    {
      return -EAGAIN;
    }

  ret = nxsem_tickwait_uninterruptible(
    &g_init_sem, MSEC2TICK(BK7258_BLE_GATT_INIT_TIMEOUT_MS));
  if (ret < 0)
    {
      bk7258_ble_gatt_set_state(BK7258_BLE_GATT_STATE_FAULTED, ret);
      return ret;
    }

  if (g_init_result != OK)
    {
      return g_init_result;
    }

  g_initialized = true;
  return OK;
}

void bk7258_ble_gatt_hci_event(const uint8_t *buffer, uint16_t length)
{
  struct bk7258_ble_gatt_event_s event;
  uint8_t payload_length;

  if (!__atomic_load_n(&g_queue_ready, __ATOMIC_ACQUIRE) ||
      buffer == NULL || length < BK7258_BLE_H4_EVENT_HEADER_SIZE ||
      buffer[0] != BK7258_BLE_H4_EVENT)
    {
      return;
    }

  payload_length = buffer[2];
  if ((size_t)length != BK7258_BLE_H4_EVENT_HEADER_SIZE + payload_length)
    {
      return;
    }

  memset(&event, 0, sizeof(event));
  if (buffer[1] == BK7258_BLE_EVT_LE_META && payload_length >= 4u &&
      (buffer[3] == BK7258_BLE_SUBEVT_LE_CONNECTED ||
       buffer[3] == BK7258_BLE_SUBEVT_LE_ENH_CONNECTED) &&
      buffer[4] == 0u)
    {
      event.type = BK7258_BLE_GATT_EVENT_CONNECTED;
      event.handle = bk7258_ble_gatt_get_le16(&buffer[5]);
      (void)bk7258_ble_gatt_queue(&event);
    }
  else if (buffer[1] == BK7258_BLE_EVT_DISCONNECTED &&
           payload_length >= 4u && buffer[3] == 0u)
    {
      event.type = BK7258_BLE_GATT_EVENT_DISCONNECTED;
      event.handle = bk7258_ble_gatt_get_le16(&buffer[4]);
      event.reason = buffer[6];
      (void)bk7258_ble_gatt_queue(&event);
    }
}

int bk7258_ble_gatt_get_stats(struct bk7258_ble_gatt_stats_s *stats)
{
  if (stats == NULL)
    {
      return -EINVAL;
    }

#define BK7258_BLE_LOAD_STAT(field, order) \
  stats->field = __atomic_load_n(&g_stats.field, order)
  BK7258_BLE_LOAD_STAT(state, __ATOMIC_ACQUIRE);
  BK7258_BLE_LOAD_STAT(last_error, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(worker_cpu, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(connected, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(disconnected, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(readvertised, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(queue_full, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_accepted, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_offset, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_length, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_magic, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_version, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_opcode, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_count, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(writes_bad_crc, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(ccc_changes, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(notify_attempted, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(active_handle, __ATOMIC_RELAXED);
  BK7258_BLE_LOAD_STAT(subscribed, __ATOMIC_ACQUIRE);
#undef BK7258_BLE_LOAD_STAT
  return OK;
}

#endif /* CONFIG_BK7258_BLE_GATT */
