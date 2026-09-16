/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/spinlock.h>
#include <nuttx/wireless/bluetooth/bt_core.h>
#include <nuttx/wireless/bluetooth/bt_gatt.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/wireless/bluetooth/bt_uuid.h>
#include <errno.h>
#include <string.h>
#include <sys/random.h>
#include "bt_hcicore.h"
#include "bt_conn.h"
#include "bk7258_provision_gatt.h"

#ifdef CONFIG_BK7258_BLE_GATT
#error "Product provisioning and N13 test GATT tables are mutually exclusive"
#endif
#if CONFIG_BLUETOOTH_MAX_CONN != 1
#error "Product provisioning currently requires one BLE connection"
#endif

#define RX_BYTES 4096u
#define UUID128(n) { .type = BT_UUID_128, .u.u128 = \
  {0x31,0x25,0x39,0x4b,0xda,0xe6,0x62,0x9c,0x48,0x4c,0x31,0x9b,n,0x00,0xe7,0x81} }
static struct bt_uuid_s g_service = UUID128(1);
static struct bt_uuid_s g_tx = UUID128(2);
static struct bt_uuid_s g_rx = UUID128(3);
static struct bt_uuid_s g_gap = { .type = BT_UUID_16, .u.u16 = BT_UUID_GAP };
static struct bt_uuid_s g_name = { .type = BT_UUID_16, .u.u16 = BT_UUID_GAP_DEVICE_NAME };
static struct bt_uuid_s g_appearance = { .type = BT_UUID_16, .u.u16 = BT_UUID_GAP_APPEARANCE };
static spinlock_t g_lock = SP_UNLOCKED;
static struct bt_conn_s *g_peer;
static struct bt_conn_s *g_connection;
/* Only the serialized product worker changes advertising state. */
static bool g_advertising;
static bool g_locator_valid;
static uint8_t g_locator[BKPROV_GATT_LOCATOR_SIZE];
static int g_radio_error;
static uint8_t g_input[RX_BYTES];
static size_t g_head;
static size_t g_count;
static uint32_t g_generation;
static bool g_registered;
static bool g_window;
static bool g_ready;
static uint16_t g_ccc_value;

static int read_name(struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr,
                     void *buf, uint8_t len, uint16_t offset)
{
  return bt_gatt_attr_read(conn, attr, buf, len, offset, "Shaniu", 6);
}

static int read_appearance(struct bt_conn_s *conn,
                           const struct bt_gatt_attr_s *attr,
                           void *buf, uint8_t len, uint16_t offset)
{
  static const uint8_t value[2] = {0, 0};
  return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static void ccc_changed(uint16_t value)
{
  struct bt_conn_s *old;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  old = g_peer;
  g_peer = NULL;
  g_ccc_value = value;
  g_head = 0;
  g_count = 0;
  memset(g_input, 0, sizeof(g_input));
  g_ready = false;
  if (g_ccc_value == BT_GATT_CCC_NOTIFY && g_window &&
      g_connection != NULL && g_generation != UINT32_MAX)
    {
      g_generation++;
      g_ready = true;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  if (old != NULL)
    {
      bt_conn_release(old);
    }
}

/* HCI callbacks must not issue synchronous HCI commands. Hold the physical
 * connection independently of the TLS subscription so closing a window also
 * disconnects peers which never wrote a TLS byte.
 */
static void connected(struct bt_conn_s *conn, void *context)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  (void)context;
  if (g_connection == NULL)
    {
      g_connection = bt_conn_addref(conn);
      g_locator_valid = false;
      memset(g_locator, 0, sizeof(g_locator));
      if (g_ccc_value == BT_GATT_CCC_NOTIFY && g_window &&
          g_generation != UINT32_MAX)
        {
          g_generation++;
          g_ready = true;
        }
    }
  spin_unlock_irqrestore(&g_lock, flags);
}

static void disconnected(struct bt_conn_s *conn, void *context)
{
  struct bt_conn_s *old = NULL;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  (void)context;
  if (g_connection == conn)
    {
      old = g_connection;
      g_connection = NULL;
      g_window = false;
      g_ready = false;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  if (old != NULL)
    {
      ccc_changed(0);
      bt_conn_release(old);
    }
}

static struct bt_conn_cb_s g_callbacks =
{
  .connected = connected,
  .disconnected = disconnected,
};

static const struct bt_eir_s g_advertisement[] =
{
  { .len = 2, .type = BT_EIR_FLAGS,
    .data = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR } },
  { .len = 17, .type = BT_EIR_UUID128_ALL,
    .data = {0x31,0x25,0x39,0x4b,0xda,0xe6,0x62,0x9c,
             0x48,0x4c,0x31,0x9b,0x01,0x00,0xe7,0x81} },
  { .len = 7, .type = BT_EIR_NAME_COMPLETE, .data = "Shaniu" },
  { .len = 0 },
};

/* 128-bit service UUID plus an 8-byte window locator fits legacy scan data.
 * A missing random source leaves ordinary BLE discovery available.
 */
static struct bt_eir_s g_scan_response[] =
{
  { .len = 0, .type = BT_EIR_SVC_DATA128,
    .data = {0x31,0x25,0x39,0x4b,0xda,0xe6,0x62,0x9c,
             0x48,0x4c,0x31,0x9b,0x01,0x00,0xe7,0x81} },
  { .len = 0 },
};

static int write_tls(struct bt_conn_s *conn, const struct bt_gatt_attr_s *attr,
                     const void *data, uint8_t len, uint16_t offset)
{
  const uint8_t *bytes = data;
  irqstate_t flags;
  size_t i;
  int ret = len;
  (void)attr;
  if (offset != 0 || len == 0 || data == NULL || conn == NULL)
    {
      return -EINVAL;
    }
  flags = spin_lock_irqsave(&g_lock);
  if (!g_ready || conn != g_connection || (g_peer != NULL && g_peer != conn))
    {
      ret = -EACCES;
    }
  else if (len > RX_BYTES - g_count)
    {
      ret = -ENOBUFS;
    }
  else
    {
      if (g_peer == NULL)
        {
          g_peer = bt_conn_addref(conn);
        }
      for (i = 0; i < len; i++)
        {
          g_input[(g_head + g_count + i) % RX_BYTES] = bytes[i];
        }
      g_count += len;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

static struct bt_gatt_chrc_s g_name_chrc =
  { .properties = BT_GATT_CHRC_READ, .value_handle = 3, .uuid = &g_name };
static struct bt_gatt_chrc_s g_appearance_chrc =
  { .properties = BT_GATT_CHRC_READ, .value_handle = 5, .uuid = &g_appearance };
static struct bt_gatt_chrc_s g_tx_chrc =
  { .properties = BT_GATT_CHRC_WRITE, .value_handle = 0x12, .uuid = &g_tx };
static struct bt_gatt_chrc_s g_rx_chrc =
  { .properties = BT_GATT_CHRC_NOTIFY, .value_handle = 0x14, .uuid = &g_rx };
static struct bt_gatt_ccc_cfg_s g_ccc[1];
static const struct bt_gatt_attr_s g_attributes[] =
{
  BT_GATT_PRIMARY_SERVICE(1, &g_gap),
  BT_GATT_CHARACTERISTIC(2, &g_name_chrc),
  BT_GATT_DESCRIPTOR(3, &g_name, BT_GATT_PERM_READ, read_name, NULL, NULL),
  BT_GATT_CHARACTERISTIC(4, &g_appearance_chrc),
  BT_GATT_DESCRIPTOR(5, &g_appearance, BT_GATT_PERM_READ, read_appearance, NULL, NULL),
  BT_GATT_PRIMARY_SERVICE(0x10, &g_service),
  BT_GATT_CHARACTERISTIC(0x11, &g_tx_chrc),
  BT_GATT_DESCRIPTOR(0x12, &g_tx, BT_GATT_PERM_WRITE, NULL, write_tls, NULL),
  BT_GATT_CHARACTERISTIC(0x13, &g_rx_chrc),
  BT_GATT_DESCRIPTOR(0x14, &g_rx, 0, NULL, NULL, NULL),
  BT_GATT_CCC(0x15, 0x14, g_ccc, ccc_changed),
};

int bkprov_gatt_register(void)
{
  if (g_registered)
    {
      return -EALREADY;
    }
  bt_gatt_register(g_attributes, sizeof(g_attributes) / sizeof(g_attributes[0]));
  bt_conn_cb_register(&g_callbacks);
  g_registered = true;
  return 0;
}

int bkprov_gatt_poll(void)
{
  struct bt_conn_s *peer;
  bool open;
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  peer = g_connection == NULL ? NULL : bt_conn_addref(g_connection);
  open = g_window;
  spin_unlock_irqrestore(&g_lock, flags);
  int ret = g_radio_error;

  /* The controller stops advertising on connection; clear the Host flag too,
   * otherwise this Host automatically advertises again on disconnection.
   */
  if (g_advertising && (peer != NULL || !open))
    {
      int error = bt_stop_advertising();
      if (error == 0 || error == -EALREADY)
        {
          g_advertising = false;
        }
      else
        {
          /* This Host clears its enabled flag before the HCI reply. After
           * failure, -EALREADY on retry is not controller-state proof. Keep
           * future windows blocked until product restart instead of hiding it.
           */
          g_radio_error = error;
          ret = error;
          flags = spin_lock_irqsave(&g_lock);
          g_window = false;
          spin_unlock_irqrestore(&g_lock, flags);
          ccc_changed(0);
          open = false;
        }
    }
  if (peer != NULL)
    {
      if (!open)
        {
          int error = bt_conn_disconnect(peer, 0x13); /* Remote user ended */
          if (ret == 0 && error != -ENOTCONN)
            {
              ret = error;
            }
        }
      bt_conn_release(peer);
    }
  return ret;
}

int bkprov_gatt_locator(uint8_t locator[BKPROV_GATT_LOCATOR_SIZE])
{
  int ret = -EAGAIN;
  irqstate_t flags;
  if (locator == NULL) return -EINVAL;
  memset(locator, 0, BKPROV_GATT_LOCATOR_SIZE);
  flags = spin_lock_irqsave(&g_lock);
  if (g_window && g_locator_valid && g_connection == NULL)
    {
      memcpy(locator, g_locator, BKPROV_GATT_LOCATOR_SIZE);
      ret = 0;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bkprov_gatt_window(bool open)
{
  irqstate_t flags;
  int ret;
  if (!g_registered)
    {
      return -ENODEV;
    }
  if (!open)
    {
      flags = spin_lock_irqsave(&g_lock);
      g_window = false;
      g_locator_valid = false;
      memset(g_locator, 0, sizeof(g_locator));
      spin_unlock_irqrestore(&g_lock, flags);
      ccc_changed(0);
      return bkprov_gatt_poll();
    }
  ret = bkprov_gatt_poll();
  if (ret < 0)
    {
      return ret;
    }
  flags = spin_lock_irqsave(&g_lock);
  if (g_window || g_connection != NULL || g_generation == UINT32_MAX)
    {
      spin_unlock_irqrestore(&g_lock, flags);
      return -EBUSY;
    }
  g_window = true;
  g_locator_valid = false;
  memset(g_locator, 0, sizeof(g_locator));
  spin_unlock_irqrestore(&g_lock, flags);

  uint8_t locator[BKPROV_GATT_LOCATOR_SIZE];
  bool have_locator = getrandom(locator, sizeof(locator),
                               GRND_RANDOM | GRND_NONBLOCK) == sizeof(locator);
  g_scan_response[0].len = have_locator ? 25 : 0;
  memset(g_scan_response[0].data + 16, 0, sizeof(locator));
  if (have_locator)
    {
      memcpy(g_scan_response[0].data + 16, locator, sizeof(locator));
    }
  ret = bt_start_advertising(BT_LE_ADV_IND, g_advertisement, g_scan_response);
  /* Also reconcile a partially failed enable command on the error path. */
  g_advertising = true;
  flags = spin_lock_irqsave(&g_lock);
  if (ret == 0 && have_locator && g_window && g_connection == NULL)
    {
      memcpy(g_locator, locator, sizeof(locator));
      g_locator_valid = true;
    }
  spin_unlock_irqrestore(&g_lock, flags);
  memset(locator, 0, sizeof(locator));
  if (ret < 0)
    {
      (void)bkprov_gatt_window(false);
    }
  return ret;
}

bool bkprov_gatt_open(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  bool open = g_window;
  spin_unlock_irqrestore(&g_lock, flags);
  return open;
}

bool bkprov_gatt_idle(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  bool idle = !g_window && g_connection == NULL && !g_advertising;
  spin_unlock_irqrestore(&g_lock, flags);
  return idle;
}

uint32_t bkprov_gatt_generation(void)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  uint32_t value = g_ready ? g_generation : 0;
  spin_unlock_irqrestore(&g_lock, flags);
  return value;
}

ssize_t bkprov_gatt_read(uint32_t generation, void *data, size_t size)
{
  uint8_t *bytes = data;
  irqstate_t flags;
  size_t i;
  if (data == NULL || size == 0)
    {
      return -EINVAL;
    }
  flags = spin_lock_irqsave(&g_lock);
  if (!g_ready || generation != g_generation)
    {
      spin_unlock_irqrestore(&g_lock, flags);
      return -ESTALE;
    }
  size = size < g_count ? size : g_count;
  for (i = 0; i < size; i++)
    {
      bytes[i] = g_input[g_head];
      g_input[g_head] = 0;
      g_head = (g_head + 1) % RX_BYTES;
    }
  g_count -= size;
  spin_unlock_irqrestore(&g_lock, flags);
  return size == 0 ? -EAGAIN : (ssize_t)size;
}

ssize_t bkprov_gatt_send(uint32_t generation, const void *data, size_t size)
{
  struct bt_conn_s *peer;
  irqstate_t flags;
  if (data == NULL || size == 0 || size > 20)
    {
      return -EINVAL;
    }
  flags = spin_lock_irqsave(&g_lock);
  peer = g_ready && generation == g_generation && g_peer != NULL ?
         bt_conn_addref(g_peer) : NULL;
  spin_unlock_irqrestore(&g_lock, flags);
  if (peer == NULL)
    {
      return -ESTALE;
    }
  /* This fixed NuttX release exposes only the CCC-routed notification API.
   * CONFIG_BLUETOOTH_MAX_CONN=1 and the generation/ref checks above bind the
   * send to the authenticated product peer before the public call. */
  bt_gatt_notify(0x14, data, size);
  bt_conn_release(peer);
  return (ssize_t)size;
}
