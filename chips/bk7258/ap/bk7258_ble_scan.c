/****************************************************************************
 * chips/bk7258/ap/bk7258_ble_scan.c
 * SPDX-License-Identifier: Apache-2.0
 * AP-local bounded diagnostics over the existing NuttX Bluetooth Host.
 ****************************************************************************/
#include <nuttx/config.h>
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_WIRELESS_BLUETOOTH_HOST)
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <nuttx/spinlock.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <arch/chip/bk7258_ble_scan.h>
#include <arch/chip/bk7258_radio_mode.h>
#include <arch/chip/bk7258_smp_placement.h>

/* Current NuttX declares these Host entry points in bt_hcicore.h (internal).
 * Keep the version-specific declarations at this chip boundary, not in UI. */
extern int bt_start_scanning(uint8_t, void (*)(const bt_addr_le_t *, int8_t,
                             uint8_t, const uint8_t *, uint8_t));
extern int bt_stop_scanning(void);
extern int bt_le_scan_update(void);

static spinlock_t g_lock BK7258_SMP_LOCK_BSS = SP_UNLOCKED;
static struct bk7258_ble_scan_snapshot_s g_snapshot;
static bool g_worker;
static bool g_stop_pending;
static bool g_host_callback;

static int scan_error(int ret)
{
  return ret > 0 ? -EIO : ret;
}

static void scan_report(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                        const uint8_t *data, uint8_t length)
{
  irqstate_t flags;
  struct bk7258_ble_scan_result_s *result;
  uint32_t index;
  if (addr == NULL || (data == NULL && length != 0) ||
      length > BK7258_BLE_SCAN_MAX_PAYLOAD) return;
  flags = spin_lock_irqsave(&g_lock);
  if (g_snapshot.state != BK7258_BLE_SCAN_ACTIVE) goto out;
  for (index = 0; index < g_snapshot.count; index++)
    if (g_snapshot.results[index].address_type == addr->type &&
        g_snapshot.results[index].advertising_type == type &&
        memcmp(g_snapshot.results[index].address, addr->val, 6) == 0) break;
  if (index == BK7258_BLE_SCAN_MAX_RESULTS)
    {
      if (g_snapshot.dropped != UINT32_MAX) g_snapshot.dropped++;
      goto out;
    }
  result = &g_snapshot.results[index];
  memset(result, 0, sizeof(*result));
  memcpy(result->address, addr->val, 6);
  result->address_type = addr->type;
  result->rssi = rssi;
  result->advertising_type = type;
  result->payload_length = length;
  if (length) memcpy(result->payload, data, length);
  if (index == g_snapshot.count) g_snapshot.count++;
out:
  spin_unlock_irqrestore(&g_lock, flags);
}

static void *scan_worker(void *arg)
{
  bool starting = arg != NULL;
  irqstate_t flags;
  int ret = 0;
  int original = 0;
  if (starting)
    {
      ret = scan_error(bt_start_scanning(BT_LE_SCAN_FILTER_DUP_ENABLE, scan_report));
      if (ret != -EALREADY) g_host_callback = true;
      flags = spin_lock_irqsave(&g_lock);
      if (ret == 0 && !g_stop_pending)
        {
          g_snapshot.state = BK7258_BLE_SCAN_ACTIVE;
          g_worker = false;
          spin_unlock_irqrestore(&g_lock, flags);
          return NULL;
        }
      g_snapshot.state = BK7258_BLE_SCAN_STOPPING;
      spin_unlock_irqrestore(&g_lock, flags);
      original = ret;
    }
  /* A foreign Host callback must never be stopped. All other failed starts
   * installed our callback; stop clears it before issuing HCI disable. */
  if (original != -EALREADY)
    {
      ret = scan_error(g_host_callback ? bt_stop_scanning() : bt_le_scan_update());
      g_host_callback = false;
    }
  else ret = 0;
  if (ret == 0) ret = bk7258_radio_mode_release(BK7258_RADIO_MODE_BLE_SCAN);
  flags = spin_lock_irqsave(&g_lock);
  g_snapshot.state = ret == 0 ? BK7258_BLE_SCAN_IDLE : BK7258_BLE_SCAN_FAULTED;
  g_snapshot.active = ret == 0 ? 0 : 1;
  g_snapshot.last_error = ret == 0 ? original : ret;
  g_worker = false;
  g_stop_pending = false;
  uint32_t count = g_snapshot.count;
  uint32_t state = g_snapshot.state;
  spin_unlock_irqrestore(&g_lock, flags);
  syslog(LOG_INFO, "ble-scan: state=%lu status=%d reports=%lu\n",
         (unsigned long)state, ret == 0 ? original : ret, (unsigned long)count);
  return NULL;
}

static int scan_launch(bool starting)
{
  pthread_attr_t attr;
  pthread_t thread;
  cpu_set_t cpu = (cpu_set_t)1u;
  int ret = pthread_attr_init(&attr);
  if (ret != 0) return -ret;
  ret = pthread_attr_setstacksize(&attr, 16384);
  if (ret == 0) ret = pthread_attr_setaffinity_np(&attr, sizeof(cpu), &cpu);
  if (ret == 0) ret = pthread_create(&thread, &attr, scan_worker,
                                    starting ? (void *)1 : NULL);
  pthread_attr_destroy(&attr);
  if (ret == 0) pthread_detach(thread);
  return -ret;
}

int bk7258_ble_scan_start(void)
{
  irqstate_t flags;
  uint32_t generation;
  int ret;
  flags = spin_lock_irqsave(&g_lock);
  if (g_worker || g_snapshot.active)
    { spin_unlock_irqrestore(&g_lock, flags); return -EBUSY; }
  g_worker = true;
  spin_unlock_irqrestore(&g_lock, flags);
  ret = bk7258_radio_mode_acquire(BK7258_RADIO_MODE_BLE_SCAN);
  if (ret != 0)
    {
      flags = spin_lock_irqsave(&g_lock);
      g_worker = false;
      spin_unlock_irqrestore(&g_lock, flags);
      return ret;
    }
  flags = spin_lock_irqsave(&g_lock);
  generation = g_snapshot.generation + 1u;
  memset(&g_snapshot, 0, sizeof(g_snapshot));
  g_snapshot.generation = generation ? generation : 1u;
  g_snapshot.active = 1;
  g_snapshot.state = BK7258_BLE_SCAN_STARTING;
  g_stop_pending = false;
  spin_unlock_irqrestore(&g_lock, flags);
  ret = scan_launch(true);
  if (ret < 0)
    {
      int release = bk7258_radio_mode_release(BK7258_RADIO_MODE_BLE_SCAN);
      flags = spin_lock_irqsave(&g_lock);
      g_snapshot.state = release == 0 ? BK7258_BLE_SCAN_IDLE : BK7258_BLE_SCAN_FAULTED;
      g_snapshot.active = release == 0 ? 0 : 1;
      g_snapshot.last_error = ret;
      g_worker = false;
      spin_unlock_irqrestore(&g_lock, flags);
    }
  return ret;
}

int bk7258_ble_scan_poll(struct bk7258_ble_scan_snapshot_s *snapshot)
{
  irqstate_t flags;
  if (snapshot == NULL) return -EINVAL;
  flags = spin_lock_irqsave(&g_lock);
  memcpy(snapshot, &g_snapshot, sizeof(*snapshot));
  spin_unlock_irqrestore(&g_lock, flags);
  return 0;
}

int bk7258_ble_scan_stop(void)
{
  irqstate_t flags;
  int ret;
  flags = spin_lock_irqsave(&g_lock);
  if (!g_snapshot.active)
    { spin_unlock_irqrestore(&g_lock, flags); return 0; }
  if (g_worker)
    {
      g_stop_pending = true;
      spin_unlock_irqrestore(&g_lock, flags);
      return 0;
    }
  g_worker = true;
  g_snapshot.state = BK7258_BLE_SCAN_STOPPING;
  spin_unlock_irqrestore(&g_lock, flags);
  ret = scan_launch(false);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&g_lock);
      g_snapshot.state = BK7258_BLE_SCAN_FAULTED;
      g_snapshot.last_error = ret;
      g_worker = false;
      spin_unlock_irqrestore(&g_lock, flags);
    }
  return ret;
}
#endif
