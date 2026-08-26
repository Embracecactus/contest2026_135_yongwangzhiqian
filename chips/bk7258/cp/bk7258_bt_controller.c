/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/
 * bk7258_bt_controller.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-side bootstrap for the official Beken Bluetooth mailbox IPC.  The
 * controller itself is started on demand by the SDK vendor-init message sent
 * by the AP NuttX HCI wrapper.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET)

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_bt_ipc.h>
#ifdef CONFIG_BK7258_RPTUN_MBOX
#  include <arch/chip/bk7258_rptun.h>
#endif
#include <arch/chip/bk7258_sdk_abi.h>

#include <common/bk_err.h>
#include <components/system.h>

#ifdef CONFIG_BK7258_WIFI_VNET
#  include <components/event.h>
#  include <components/netif.h>
#  include <modules/wifi.h>

/* The public v3.1.1.9 wifi_types.h macro takes the addresses of these
 * archive-owned objects but the generated declarations were not exported in
 * the official armino_as_lib header bundle.  Only their addresses cross this
 * wrapper boundary, so their private structure definitions remain in SDK.
 */

extern void bk7258_os_wifi_malloc_zero_begin(void);
extern void bk7258_os_wifi_malloc_zero_end(void);
#endif

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* CP and AP SDK variants intentionally expose different bt_ipc_init return
 * types.  This declaration matches the CP archive selected for this image.
 */

#ifdef CONFIG_BK7258_BT_IPC
extern void bk7258_os_bt_ipc_init_begin(void);
extern void bk7258_os_bt_ipc_init_end(void);
#endif

/* Restore every build-selected physical diagnostic owner after SDK leaves
 * that may touch pinmux.  SWD and UART are independent when their pins do
 * not conflict.
 */

#include <arch/chip/bk7258_console.h>

#ifdef CONFIG_BK7258_SWD_DEBUG
#  include <arch/chip/bk7258_debug.h>
#endif

static void bk7258_debug_transport_recover(void)
{
#ifdef CONFIG_BK7258_SWD_DEBUG
  (void)bk7258_swd_initialize();
#endif
#ifdef BK7258_HAVE_UART_CONSOLE
  bk7258_uart_recover_console();
#endif
}

/* Match the relevant ordering from the official CP startup sequence.  Driver
 * initialization makes flash calibration data readable.  The SDK
 * components_early_init installs the PHY/RF adapter tables, and the normal
 * Wi-Fi path subsequently initializes calibration.  The board wrapper
 * invokes none of those SDK top-level routines, so call their exported leaf
 * initializers explicitly before the Controller can open RF.
 */

/* WIFI_DEFAULT_INIT_CONFIG() normally publishes the SDK-owned callback table
 * through bk_wifi_init().  Bluetooth-only NuttX must not start the SDK Wi-Fi
 * stack, but the shared PHY backend still dereferences g_wifi_funcs for
 * critical-section leaves.
 */

/* The generated SDK bundle omits partitions_gen.h, so including the private
 * flash_partition.h is not possible.  Keep the small binary ABI used here
 * in the board-private SDK ABI header and guard the numeric partition IDs
 * against the official v3.1.1.9 table at runtime before every read or write.
 */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SYS_CPU_POWER_SLEEP_WAKEUP    0x44010040u
#define BK7258_SYS_PWD_OFDM                  (1u << 13)

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BK7258_BT_IPC
static mutex_t g_bk7258_bt_controller_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_bt_controller_ipc_ready;
static bool g_bk7258_bt_controller_ready;
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
static mutex_t g_bk7258_wifi_controller_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_wifi_controller_ready;
#endif
static bool g_bk7258_bt_phy_adapter_ready;
static bool g_bk7258_bt_wifi_adapter_ready;
static bool g_bk7258_bt_calibration_ready;
static mutex_t g_bk7258_bt_mac_lock = NXMUTEX_INITIALIZER;
static const struct bk7258_bt_mac_storage_ops_s *g_bk7258_bt_mac_storage;
static bool g_bk7258_bt_mac_ready;
static uint8_t g_bk7258_bt_base_mac[BK_MAC_ADDR_LEN];
#ifdef CONFIG_BK7258_BT_IPC
static volatile uint32_t g_bk7258_bt_vendor_init_calls;
static volatile uint32_t g_bk7258_bt_vendor_deinit_calls;
static volatile int g_bk7258_bt_vendor_init_result;
static volatile int g_bk7258_bt_vendor_deinit_result;
volatile struct bk7258_bt_lifecycle_diag_s g_bk7258_bt_cp_lifecycle =
{
  .magic   = BK7258_BT_LIFECYCLE_MAGIC,
  .version = BK7258_BT_LIFECYCLE_VERSION,
  .size    = sizeof(struct bk7258_bt_lifecycle_diag_s),
  .state   = BK7258_BT_LIFECYCLE_CLOSED,
};
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

#if defined(CONFIG_BK7258_BT_IPC) && !defined(CONFIG_BK7258_WIFI_VNET)
static void bk7258_bt_set_ofdm_pwd(uint32_t value)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)(uintptr_t)BK7258_SYS_CPU_POWER_SLEEP_WAKEUP;
  uint32_t current = *reg;

  if (value != 0)
    {
      current |= BK7258_SYS_PWD_OFDM;
    }
  else
    {
      current &= ~BK7258_SYS_PWD_OFDM;
    }

  *reg = current;
}

static uint32_t bk7258_bt_get_ofdm_pwd(void)
{
  volatile uint32_t *reg =
    (volatile uint32_t *)(uintptr_t)BK7258_SYS_CPU_POWER_SLEEP_WAKEUP;

  return (*reg & BK7258_SYS_PWD_OFDM) != 0;
}

static const struct bk7258_bt_wifi_phy_funcs_s g_bk7258_bt_wifi_phy_funcs =
{
  .cal_set_wifi_pll = rwnx_cal_set_rfconfig_WIFIPLL,
  .delay_us = bk_delay_us,
  .set_ofdm_pwd = bk7258_bt_set_ofdm_pwd,
  .get_ofdm_pwd = bk7258_bt_get_ofdm_pwd,
  .disable_int = rtos_disable_int,
  .enable_int = rtos_enable_int,
  .enter_low_analog = sys_hal_enter_low_analog,
  .exit_low_analog = sys_hal_exit_low_analog,
};
#endif

static_assert(BK_MAC_ADDR_LEN == BK7258_SDK_MAC_ADDRESS_SIZE,
              "Beken MAC address ABI changed");

/****************************************************************************
 * SDK Bluetooth Lifecycle Wrappers
 ****************************************************************************/

#ifdef CONFIG_BK7258_BT_IPC
int __wrap_bk_bluetooth_init(void)
{
  int ret;

  __atomic_fetch_add(&g_bk7258_bt_vendor_init_calls, 1u,
                     __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_bk7258_bt_cp_lifecycle.init_requests, 1u,
                     __ATOMIC_RELAXED);
  ret = __real_bk_bluetooth_init();
  __atomic_store_n(&g_bk7258_bt_vendor_init_result, ret, __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_bt_cp_lifecycle.last_error, ret,
                   __ATOMIC_RELAXED);
  if (ret == 0)
    {
      __atomic_fetch_add(&g_bk7258_bt_cp_lifecycle.init_successes, 1u,
                         __ATOMIC_RELAXED);
      __atomic_store_n(&g_bk7258_bt_controller_ready, true,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&g_bk7258_bt_cp_lifecycle.state,
                       BK7258_BT_LIFECYCLE_OPEN, __ATOMIC_RELEASE);
#ifdef CONFIG_BK7258_RPTUN_MBOX
      __atomic_fetch_or(&bk7258_rptun_control()->flags,
                        BK7258_RPTUN_FLAG_CP_BT_ACTIVE,
                        __ATOMIC_RELEASE);
#endif
    }

  bk7258_debug_transport_recover();

  if (ret != 0)
    {
      syslog(LOG_ERR, "bk7258: SDK Bluetooth init failed: %d\n", ret);
    }

  return ret;
}

int __wrap_bk_bluetooth_deinit(void)
{
  int ret;

  __atomic_fetch_add(&g_bk7258_bt_vendor_deinit_calls, 1u,
                     __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_bk7258_bt_cp_lifecycle.deinit_requests, 1u,
                     __ATOMIC_RELAXED);
  ret = __real_bk_bluetooth_deinit();
  __atomic_store_n(&g_bk7258_bt_vendor_deinit_result, ret,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_bt_cp_lifecycle.last_error, ret,
                   __ATOMIC_RELAXED);
  if (ret == 0)
    {
      __atomic_fetch_add(&g_bk7258_bt_cp_lifecycle.deinit_successes, 1u,
                         __ATOMIC_RELAXED);
      __atomic_store_n(&g_bk7258_bt_controller_ready, false,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&g_bk7258_bt_cp_lifecycle.state,
                       BK7258_BT_LIFECYCLE_CLOSED, __ATOMIC_RELEASE);
#ifdef CONFIG_BK7258_RPTUN_MBOX
      __atomic_fetch_and(&bk7258_rptun_control()->flags,
                         ~BK7258_RPTUN_FLAG_CP_BT_ACTIVE,
                         __ATOMIC_RELEASE);
#endif
    }

  bk7258_debug_transport_recover();

  if (ret != 0)
    {
      syslog(LOG_ERR, "bk7258: SDK Bluetooth deinit failed: %d\n", ret);
    }

  return ret;
}
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_bt_mac_is_valid(const uint8_t *mac)
{
  bool all_zero = true;
  bool all_ff = true;
  unsigned int i;

  for (i = 0; i < BK_MAC_ADDR_LEN; i++)
    {
      all_zero = all_zero && mac[i] == 0;
      all_ff = all_ff && mac[i] == UINT8_MAX;
    }

  return !all_zero && !all_ff && (mac[0] & 1u) == 0;
}

static bool bk7258_bt_mac_is_official_valid(const uint8_t *mac)
{
  return bk7258_bt_mac_is_valid(mac) &&
         mac[0] == BK7258_SDK_MAC_OUI0 &&
         mac[1] == BK7258_SDK_MAC_OUI1 &&
         mac[2] == BK7258_SDK_MAC_OUI2;
}

static uint8_t bk7258_bt_crc8(const uint8_t *buffer, uint32_t length)
{
  uint8_t crc = 0;
  uint8_t bit;

  while (length-- > 0)
    {
      crc ^= *buffer++;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc & 0x80u) != 0 ?
                (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }

  return crc;
}

static bool bk7258_bt_partition_read(enum bk7258_bt_mac_store_e store,
                                     uint32_t offset, uint8_t *buffer,
                                     uint32_t length)
{
  if (g_bk7258_bt_mac_storage == NULL)
    {
      return false;
    }

  return g_bk7258_bt_mac_storage->read(store, offset, buffer, length) == OK;
}

static bool bk7258_bt_partition_write(enum bk7258_bt_mac_store_e store,
                                      uint32_t offset,
                                      const uint8_t *buffer,
                                      uint32_t length)
{
  if (g_bk7258_bt_mac_storage == NULL)
    {
      return false;
    }

  return g_bk7258_bt_mac_storage->write(store, offset, buffer, length) == OK;
}

static bool bk7258_bt_sysnet_write(const uint8_t *mac)
{
  return bk7258_bt_partition_write(BK7258_BT_MAC_STORE_NETWORK, 0, mac,
                                   BK_MAC_ADDR_LEN);
}

static bool bk7258_bt_mac_record_is_erased(
  const struct bk7258_bt_mac_record_s *record)
{
  const uint8_t *data = (const uint8_t *)record;
  unsigned int i;

  for (i = 0; i < sizeof(*record); i++)
    {
      if (data[i] != UINT8_MAX)
        {
          return false;
        }
    }

  return true;
}

static bool bk7258_bt_mac_record_is_valid(
  const struct bk7258_bt_mac_record_s *record)
{
  return record->magic == BK7258_SDK_MAC_RECORD_MAGIC &&
         bk7258_bt_crc8((const uint8_t *)record, 3) == record->header_crc &&
         bk7258_bt_crc8(record->mac, BK_MAC_ADDR_LEN) == record->data_crc &&
         bk7258_bt_mac_is_valid(record->mac);
}

static bool bk7258_bt_mac_read_backup(uint8_t *mac)
{
  union
  {
    uint8_t bytes[BK7258_SDK_MAC_RECORD_AREA_SIZE];
    struct bk7258_bt_mac_record_s records[BK7258_SDK_MAC_RECORD_COUNT];
  } area;

  const struct bk7258_bt_mac_record_s *records =
    area.records;
  int latest = BK7258_SDK_MAC_RECORD_COUNT - 1;
  int i;

  if (!bk7258_bt_partition_read(BK7258_BT_MAC_STORE_BACKUP,
                                BK7258_SDK_MAC_RECORD_AREA_OFFSET,
                                area.bytes, sizeof(area.bytes)))
    {
      return false;
    }

  for (i = 0; i < BK7258_SDK_MAC_RECORD_COUNT; i++)
    {
      if (bk7258_bt_mac_record_is_erased(&records[i]))
        {
          latest = i - 1;
          break;
        }
    }

  for (i = latest; i >= 0; i--)
    {
      if (bk7258_bt_mac_record_is_valid(&records[i]))
        {
          memcpy(mac, records[i].mac, BK_MAC_ADDR_LEN);
          return true;
        }
    }

  return false;
}

static bool bk7258_bt_mac_write_backup(const uint8_t *mac)
{
  union
  {
    uint8_t bytes[BK7258_SDK_MAC_RECORD_AREA_SIZE];
    struct bk7258_bt_mac_record_s records[BK7258_SDK_MAC_RECORD_COUNT];
  } area;

  struct bk7258_bt_mac_record_s record;
  int free_index = -1;
  int latest = -1;
  int i;

  if (!bk7258_bt_partition_read(BK7258_BT_MAC_STORE_BACKUP,
                                BK7258_SDK_MAC_RECORD_AREA_OFFSET,
                                area.bytes, sizeof(area.bytes)))
    {
      return false;
    }

  for (i = 0; i < BK7258_SDK_MAC_RECORD_COUNT; i++)
    {
      if (bk7258_bt_mac_record_is_erased(&area.records[i]))
        {
          free_index = i;
          break;
        }

      if (bk7258_bt_mac_record_is_valid(&area.records[i]))
        {
          latest = i;
        }
    }

  if (latest >= 0 &&
      memcmp(area.records[latest].mac, mac, BK_MAC_ADDR_LEN) == 0)
    {
      return true;
    }

  if (free_index < 0)
    {
      return false;
    }

  record.magic = BK7258_SDK_MAC_RECORD_MAGIC;
  memcpy(record.mac, mac, BK_MAC_ADDR_LEN);
  record.data_crc = bk7258_bt_crc8(record.mac, BK_MAC_ADDR_LEN);
  record.header_crc = bk7258_bt_crc8((const uint8_t *)&record, 3);

  return bk7258_bt_partition_write(
           BK7258_BT_MAC_STORE_BACKUP,
           BK7258_SDK_MAC_RECORD_AREA_OFFSET +
             (uint32_t)free_index * sizeof(record),
           (const uint8_t *)&record, sizeof(record));
}

static void bk7258_bt_mac_initialize_locked(void)
{
  static const uint8_t fallback[BK_MAC_ADDR_LEN] =
    {
      0xc8, 0x47, 0x8c, 0x00, 0x00, 0x18
    };

  uint8_t net_mac[BK_MAC_ADDR_LEN];
  uint8_t backup_mac[BK_MAC_ADDR_LEN];
  bool net_valid;
  bool backup_valid;
  bool net_written = true;
  bool backup_written = true;

  if (g_bk7258_bt_mac_ready)
    {
      return;
    }

  /* Match CONFIG_NEW_MAC_POLICY + CONFIG_RANDOM_MAC_ADDR from the official
   * CP profile.  sys_net is authoritative, the final 512 bytes of sys_rf are
   * an append-only CRC-checked backup, and an empty board receives the Beken
   * OUI plus three TRNG bytes.  Writes deliberately use the same exported
   * SDK flash leaves as mac.c.  Only their orchestration lives in this board
   * wrapper because libbk_system.a owns an incompatible FreeRTOS runtime.
   */

  memset(net_mac, UINT8_MAX, sizeof(net_mac));
  memset(backup_mac, UINT8_MAX, sizeof(backup_mac));

  if (bk_flash_driver_init() != BK_OK)
    {
      memcpy(g_bk7258_bt_base_mac, fallback, sizeof(fallback));
      g_bk7258_bt_mac_ready = true;
      return;
    }

  net_valid = bk7258_bt_partition_read(
                BK7258_BT_MAC_STORE_NETWORK, 0, net_mac,
                sizeof(net_mac)) &&
              bk7258_bt_mac_is_official_valid(net_mac);
  backup_valid = bk7258_bt_mac_read_backup(backup_mac) &&
                 bk7258_bt_mac_is_official_valid(backup_mac);

  if (net_valid)
    {
      memcpy(g_bk7258_bt_base_mac, net_mac, sizeof(net_mac));
      if (!backup_valid ||
          memcmp(net_mac, backup_mac, sizeof(net_mac)) != 0)
        {
          backup_written = bk7258_bt_mac_write_backup(net_mac);
        }
    }
  else if (backup_valid)
    {
      memcpy(g_bk7258_bt_base_mac, backup_mac, sizeof(backup_mac));
      net_written = bk7258_bt_sysnet_write(backup_mac);
    }
  else if (bk_trng_driver_init() == BK_OK)
    {
      g_bk7258_bt_base_mac[0] = BK7258_SDK_MAC_OUI0;
      g_bk7258_bt_base_mac[1] = BK7258_SDK_MAC_OUI1;
      g_bk7258_bt_base_mac[2] = BK7258_SDK_MAC_OUI2;
      g_bk7258_bt_base_mac[3] = (uint8_t)bk_rand();
      g_bk7258_bt_base_mac[4] = (uint8_t)bk_rand();
      g_bk7258_bt_base_mac[5] = (uint8_t)bk_rand();
      net_written = bk7258_bt_sysnet_write(g_bk7258_bt_base_mac);
      backup_written =
        bk7258_bt_mac_write_backup(g_bk7258_bt_base_mac);
    }
  else
    {
      memcpy(g_bk7258_bt_base_mac, fallback, sizeof(fallback));
      net_written = false;
      backup_written = false;
    }

  if (!net_written || !backup_written)
    {
      syslog(LOG_WARNING,
             "bk7258: base MAC persistence incomplete: net=%u backup=%u\n",
             net_written, backup_written);
    }

  g_bk7258_bt_mac_ready = true;
}

/****************************************************************************
 * SDK System-service Wrappers
 ****************************************************************************/

/* libbk_system.a contains the FreeRTOS tick/printf runtime and must not be
 * linked into NuttX.  Provide the small MAC service required by the real
 * Beken controller, preserving the SDK's interface-address mapping.
 */

bk_err_t bk_get_mac(uint8_t *mac, mac_type_t type)
{
  uint8_t mac_mask = 1u; /* Official NX_VIRT_DEV_MAX is 2. */
  uint8_t mac_low;
  int ret;

  if (mac == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  ret = nxmutex_lock(&g_bk7258_bt_mac_lock);
  if (ret < 0)
    {
      return BK_FAIL;
    }

  bk7258_bt_mac_initialize_locked();
  memcpy(mac, g_bk7258_bt_base_mac, BK_MAC_ADDR_LEN);

  switch (type)
    {
      case MAC_TYPE_BASE:
      case MAC_TYPE_STA:
        break;

      case MAC_TYPE_AP:
        mac_low = mac[5];
        mac[5] &= ~mac_mask;
        mac_low = (mac_low & mac_mask) ^ mac_mask;
        mac[5] |= mac_low;
        break;

      case MAC_TYPE_BLUETOOTH:
        mac[5]++;
        break;

      case MAC_TYPE_ETH:
        mac[5] += 3;
        break;

      default:
        nxmutex_unlock(&g_bk7258_bt_mac_lock);
        return BK_ERR_INVALID_MAC_TYPE;
    }

  nxmutex_unlock(&g_bk7258_bt_mac_lock);
  return BK_OK;
}

bk_err_t bk_set_base_mac(const uint8_t *mac)
{
  bool backup_written;
  bool net_written;
  int ret;

  if (mac == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  if (!bk7258_bt_mac_is_valid(mac))
    {
      return BK_ERR_GROUP_MAC;
    }

  ret = nxmutex_lock(&g_bk7258_bt_mac_lock);
  if (ret < 0)
    {
      return BK_FAIL;
    }

  bk7258_bt_mac_initialize_locked();
  net_written = bk7258_bt_sysnet_write(mac);
  backup_written = bk7258_bt_mac_write_backup(mac);

  /* sys_net is authoritative on the next boot.  Publish the new in-memory
   * address only after that write succeeds; a backup-only partial write is
   * repaired from the still-authoritative sys_net record on the next read.
   */

  if (net_written)
    {
      memcpy(g_bk7258_bt_base_mac, mac, BK_MAC_ADDR_LEN);
    }

  nxmutex_unlock(&g_bk7258_bt_mac_lock);
  return net_written && backup_written ? BK_OK : BK_FAIL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_bt_mac_storage_register(
  const struct bk7258_bt_mac_storage_ops_s *ops)
{
  int ret;

  if (ops == NULL || ops->read == NULL || ops->write == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_bk7258_bt_mac_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_bt_mac_ready)
    {
      ret = -EBUSY;
    }
  else if (g_bk7258_bt_mac_storage != NULL &&
           g_bk7258_bt_mac_storage != ops)
    {
      ret = -EALREADY;
    }
  else
    {
      g_bk7258_bt_mac_storage = ops;
      ret = OK;
    }

  nxmutex_unlock(&g_bk7258_bt_mac_lock);
  return ret;
}

#ifdef CONFIG_BK7258_WIFI_VNET
int bk7258_wifi_controller_initialize(void)
{
  wifi_init_config_t config = WIFI_DEFAULT_INIT_CONFIG();
  bk_err_t sdkret;
  int ret;

  ret = nxmutex_lock(&g_bk7258_wifi_controller_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_wifi_controller_ready)
    {
      nxmutex_unlock(&g_bk7258_wifi_controller_lock);
      return OK;
    }

  /* Follow the official v3.1.1.9 CP startup leaves, but keep NuttX in
   * charge of startup, scheduling and interrupts.  bk_wifi_init() installs
   * the complete SDK callback table; the Bluetooth-only reduced callback
   * table below must never replace it in a Wi-Fi build.
   */

  if (bk_flash_driver_init() != BK_OK || bk_adc_driver_init() != BK_OK)
    {
      ret = -EIO;
      goto out;
    }

  if (!g_bk7258_bt_phy_adapter_ready)
    {
      bk_phy_adapter_init();
      bk_rf_adapter_init();
      g_bk7258_bt_phy_adapter_ready = true;
    }

  sdkret = bk_event_init();
  if (sdkret != BK_OK)
    {
      ret = -EIO;
      goto recover_console;
    }

  sdkret = bk_netif_init();
  if (sdkret != BK_OK)
    {
      ret = -EIO;
      goto recover_console;
    }

  bk7258_os_wifi_malloc_zero_begin();
  sdkret = bk_wifi_init(&config);
  bk7258_os_wifi_malloc_zero_end();
  if (sdkret != BK_OK)
    {
      ret = -EIO;
      goto recover_console;
    }

  g_bk7258_bt_wifi_adapter_ready = true;
  g_bk7258_bt_calibration_ready = true;
  __atomic_store_n(&g_bk7258_wifi_controller_ready, true,
                   __ATOMIC_RELEASE);
  ret = OK;

recover_console:
  bk7258_debug_transport_recover();
out:
  nxmutex_unlock(&g_bk7258_wifi_controller_lock);
  return ret;
}

bool bk7258_wifi_controller_active(void)
{
  return __atomic_load_n(&g_bk7258_wifi_controller_ready,
                         __ATOMIC_ACQUIRE);
}
#endif

#ifdef CONFIG_BK7258_BT_IPC
int bk7258_bt_controller_ipc_initialize(void)
{
  int32_t sdkret;
  int ret;

#ifdef CONFIG_BK7258_WIFI_VNET
  /* Wi-Fi owns the complete shared RF callback table and must be ready before
   * Bluetooth controller IPC starts.  This also makes the combined profile's
   * initialization order independent of the AP boot timing.
   */

  ret = bk7258_wifi_controller_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

  ret = nxmutex_lock(&g_bk7258_bt_controller_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_bt_controller_ipc_ready)
    {
      nxmutex_unlock(&g_bk7258_bt_controller_lock);
      return OK;
    }

  /* Match the relevant driver_init() leaves used by the official CP startup.
   * Calibration reads sys_rf through the SDK flash driver and owns SARADC
   * through the SDK ADC mutex/buffer.  Both drivers must therefore precede
   * bk_cal_if_init(); waiting until bk_get_mac() is too late because the
   * controller asks for its MAC only after RF/calibration startup.
   */

#ifndef CONFIG_BK7258_WIFI_VNET
  if (bk_flash_driver_init() != BK_OK)
    {
      nxmutex_unlock(&g_bk7258_bt_controller_lock);
      return -EIO;
    }

  if (bk_adc_driver_init() != BK_OK)
    {
      nxmutex_unlock(&g_bk7258_bt_controller_lock);
      return -EIO;
    }

  if (!g_bk7258_bt_phy_adapter_ready)
    {
      bk_phy_adapter_init();
      bk_rf_adapter_init();
      g_bk7258_bt_phy_adapter_ready = true;
    }

  if (!g_bk7258_bt_wifi_adapter_ready)
    {
      g_wifi_funcs = (void *)&g_bk7258_bt_wifi_phy_funcs;
      __asm volatile ("dmb sy" ::: "memory");
      g_bk7258_bt_wifi_adapter_ready = true;
    }

  if (!g_bk7258_bt_calibration_ready)
    {
      /* Match the official CP driver_init() call site, which deliberately
       * ignores this function's return value.  In SDK v3.1.1.9 the normal
       * calibration_init() zero return is translated by bk_cal_if_init() to
       * -1 even though calibration has completed; treating it as bk_err_t
       * aborts before Bluetooth IPC is initialized.
       */

      (void)bk_cal_if_init();
      g_bk7258_bt_calibration_ready = true;
    }
#endif

  bk7258_os_bt_ipc_init_begin();
  sdkret = bt_ipc_init();
  bk7258_os_bt_ipc_init_end();

  /* The official CP startup performs the PHY/RF/calibration leaf sequence
   * before it hands the UART to the application.  In the NuttX wrapper the
   * console may already be live, and that same sequence can clear UART
   * registers without using bk_uart_deinit().  Reassert selected debug and
   * console ownership only after every SDK leaf above has completed.
   */

  bk7258_debug_transport_recover();
  if (sdkret == 0 || sdkret == 1)
    {
      g_bk7258_bt_controller_ipc_ready = true;
      ret = OK;
    }
  else
    {
      ret = -EIO;
    }

  nxmutex_unlock(&g_bk7258_bt_controller_lock);
  return ret;
}

int bk7258_bt_controller_initialize(void)
{
  int sdkret;
  int ret;

  ret = nxmutex_lock(&g_bk7258_bt_controller_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&g_bk7258_bt_controller_ready, __ATOMIC_ACQUIRE))
    {
      ret = OK;
      goto out;
    }

  if (!g_bk7258_bt_controller_ipc_ready)
    {
      ret = -EAGAIN;
      goto out;
    }

  /* This is the official v3.1.1.9 controller startup, delayed only until
   * the AP has published that its SDK BT mailbox endpoint is ready.  The AP
   * still issues the normal vendor-init request afterwards; the SDK handles
   * that second call idempotently and returns through its original ABI.
   */

  sdkret = __wrap_bk_bluetooth_init();
  if (sdkret != 0)
    {
      ret = -EIO;
      goto out;
    }

  __atomic_store_n(&g_bk7258_bt_controller_ready, true,
                   __ATOMIC_RELEASE);
  ret = OK;

out:
  nxmutex_unlock(&g_bk7258_bt_controller_lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_BT_IPC || CONFIG_BK7258_WIFI_VNET */
