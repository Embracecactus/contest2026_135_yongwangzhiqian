/****************************************************************************
 * chips/bk7258/include/bk7258_wifi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include <arch/chip/bk7258_wifi_csi.h>
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WIFI_SSID_MAX_LEN       32u
#define BK7258_WIFI_PASSWORD_MAX_LEN   64u
#define BK7258_WIFI_CONNECT_MIN_MS     5000u
#define BK7258_WIFI_CONNECT_MAX_MS     60000u
#define BK7258_WIFI_CONNECT_DEFAULT_MS 30000u
#define BK7258_WIFI_ECHO_MIN_MS        1000u
#define BK7258_WIFI_ECHO_DEFAULT_MS    10000u
#define BK7258_WIFI_ECHO_COUNT_DEFAULT 4u
#define BK7258_WIFI_ECHO_COUNT_MAX     32u
#define BK7258_WIFI_ECHO_SIZE_DEFAULT  64u
#define BK7258_WIFI_ECHO_SIZE_MAX      256u
#define BK7258_WIFI_MONITOR_CHANNEL_MIN 1u
#define BK7258_WIFI_MONITOR_CHANNEL_MAX 14u
#define BK7258_WIFI_MONITOR_MIN_MS      1000u
#define BK7258_WIFI_MONITOR_DEFAULT_MS  20000u
#define BK7258_WIFI_SCAN_MIN_MS          1000u
#define BK7258_WIFI_SCAN_DEFAULT_MS      15000u
#define BK7258_WIFI_SCAN_MAX_RESULTS     4u
/* AP-local scan snapshots are never serialized on the CP control wire. */
#define BK7258_WIFI_AP_SCAN_MAX_RESULTS  32u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_wifi_operation_e
{
  BK7258_WIFI_OPERATION_CONNECT = 1,
  BK7258_WIFI_OPERATION_STATUS,
  BK7258_WIFI_OPERATION_PING,
  BK7258_WIFI_OPERATION_TCP_ECHO,
  BK7258_WIFI_OPERATION_UDP_ECHO,
  BK7258_WIFI_OPERATION_MONITOR_START,
  BK7258_WIFI_OPERATION_MONITOR_STOP,
  BK7258_WIFI_OPERATION_MONITOR_STATUS,
  BK7258_WIFI_OPERATION_MONITOR_CHANNEL,
  BK7258_WIFI_OPERATION_SCAN,
  BK7258_WIFI_OPERATION_CSI_START,
  BK7258_WIFI_OPERATION_CSI_STOP,
  BK7258_WIFI_OPERATION_CSI_STATUS
};

enum bk7258_wifi_link_state_e
{
  BK7258_WIFI_LINK_IDLE = 0,
  BK7258_WIFI_LINK_CONNECTING,
  BK7258_WIFI_LINK_DISCONNECTED,
  BK7258_WIFI_LINK_CONNECTED,
  BK7258_WIFI_LINK_CONNECT_FAILED
};

/* Numeric scan values match the vendor wifi_security_t ABI.  They are kept
 * here so AP applications do not need to include vendor SDK headers. */
enum bk7258_wifi_security_e
{
  BK7258_WIFI_SECURITY_NONE = 0,
  BK7258_WIFI_SECURITY_WEP,
  BK7258_WIFI_SECURITY_WPA_TKIP,
  BK7258_WIFI_SECURITY_WPA_AES,
  BK7258_WIFI_SECURITY_WPA_MIXED,
  BK7258_WIFI_SECURITY_WPA2_TKIP,
  BK7258_WIFI_SECURITY_WPA2_AES,
  BK7258_WIFI_SECURITY_WPA2_MIXED,
  BK7258_WIFI_SECURITY_WPA3_SAE,
  BK7258_WIFI_SECURITY_WPA3_WPA2_MIXED,
  BK7258_WIFI_SECURITY_EAP,
  BK7258_WIFI_SECURITY_OWE,
  BK7258_WIFI_SECURITY_AUTO,
  BK7258_WIFI_SECURITY_WAPI_PSK,
  BK7258_WIFI_SECURITY_WAPI_CERT,
  BK7258_WIFI_SECURITY_WAPI_UNKNOWN
};

struct bk7258_wifi_echo_s
{
  uint32_t address;
  uint32_t port;
  uint32_t count;
  uint32_t size;
};

struct bk7258_wifi_result_s
{
  int32_t status;
  uint32_t link_state;
  int32_t rssi;
  uint32_t ipaddr;
  uint32_t netmask;
  uint32_t router;
  uint32_t echo_count;
  uint32_t echo_bytes;
};

struct bk7258_wifi_monitor_result_s
{
  int32_t status;
  uint32_t active;
  uint32_t session;
  uint32_t channel;
  uint32_t frame_count;
  uint32_t byte_count;
  int32_t last_rssi;
  int32_t min_rssi;
  int32_t max_rssi;
  uint32_t last_tsf_lo;
  uint32_t last_tsf_hi;
};

struct bk7258_wifi_scan_ap_s
{
  char ssid[BK7258_WIFI_SSID_MAX_LEN + 1u];
  uint8_t bssid[6];
  int32_t rssi;
  uint8_t channel;
  uint8_t security;
  uint8_t reserved[2];
};

struct bk7258_wifi_scan_result_s
{
  int32_t status;
  uint32_t found;
  uint32_t returned;
  uint32_t truncated;
  struct bk7258_wifi_scan_ap_s aps[BK7258_WIFI_SCAN_MAX_RESULTS];
};

struct bk7258_wifi_scan_snapshot_s
{
  int32_t status;
  uint32_t found;
  uint32_t returned;
  uint32_t truncated;
  struct bk7258_wifi_scan_ap_s aps[BK7258_WIFI_AP_SCAN_MAX_RESULTS];
};

/* Local AP diagnostics only: never serialized into the CP wire message. */
#define BK7258_WIFI_CHANNEL_STATS_MAX 13u
struct bk7258_wifi_channel_stats_s
{
  int32_t status;
  uint32_t returned;
  uint32_t dwell_ms;
  struct bk7258_wifi_monitor_result_s channels[BK7258_WIFI_CHANNEL_STATS_MAX];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_WIFI_VNET
#  ifdef CONFIG_BK7258_AP_CORE
int bk7258_wifi_initialize(void);
int bk7258_wifi_read_link(struct bk7258_wifi_result_s *result);
int bk7258_wifi_refresh_carrier(void);
int bk7258_wifi_retire_link(void);
bool bk7258_wifi_native_link_ready(void);
bool bk7258_wifi_native_lease_matches(
  const struct bk7258_wifi_result_s *result);
int bk7258_wifi_set_native_lease(
  const struct bk7258_wifi_result_s *result);
int bk7258_wifi_clear_native_lease(void);
/* Nonblocking AP product access to the existing logical-CPU0 control worker.
 * Credentials are copied on submit and wiped by the worker. poll consumes
 * one completion; -EAGAIN means pending, otherwise result.status is the
 * connection outcome. Cancellation is cooperative and must be polled/joined
 * before reusing the network. Tickets never wrap or match an old completion.
 */
int bk7258_wifi_connect_async(const char *ssid, const char *password,
                              uint32_t timeout_ms, uint32_t *ticket);
int bk7258_wifi_connect_poll(uint32_t ticket,
                             struct bk7258_wifi_result_s *result);
int bk7258_wifi_connect_cancel(uint32_t ticket);
int bk7258_wifi_ping_async(uint32_t timeout_ms, uint32_t *ticket);
int bk7258_wifi_ping_poll(uint32_t ticket, struct bk7258_wifi_result_s *result);
int bk7258_wifi_ping_cancel(uint32_t ticket);
/* Scan completion is consumed with scan_poll, including after leaving a UI
 * page. It shares the worker with connect/CP requests and returns -EBUSY
 * while reserved. Scanning stops at timeout; navigation need not block. */
int bk7258_wifi_scan_async(uint32_t timeout_ms, uint32_t *ticket);
int bk7258_wifi_scan_poll(uint32_t ticket,
                         struct bk7258_wifi_scan_result_s *result);
/* AP-local extended scan result. It shares the scan ticket and is consumed
 * exactly once, just like bk7258_wifi_scan_poll(). */
int bk7258_wifi_scan_snapshot_poll(
  uint32_t ticket, struct bk7258_wifi_scan_snapshot_s *result);
/* Returns a stable display name for a documented scan security value, or
 * NULL for a value unknown to this chip interface. */
const char *bk7258_wifi_security_name(uint8_t security);
int bk7258_wifi_channels_async(uint32_t dwell_ms, uint32_t *ticket);
/* Explicitly disconnect STA before local channel sampling. */
int bk7258_wifi_channels_switch_async(uint32_t dwell_ms, uint32_t *ticket);
int bk7258_wifi_channels_poll(uint32_t ticket,
                             struct bk7258_wifi_channel_stats_s *result);
/* Stop is cooperative: poll the matching result until completed before restart. */
int bk7258_wifi_diagnostic_cancel(uint32_t ticket);
/* A trial retains exclusive control after connect completion. Consume the
 * start completion with connect_poll, then finish with commit=true only after
 * durable product publication; otherwise restore the previous worker-owned
 * credentials. Finish itself is asynchronous and returns a new poll ticket.
 * A failed restore retains the lease; retry restore or restart, never reuse
 * the network under another owner. Unknown pre-existing credentials are not
 * overwritten. */
int bk7258_wifi_trial_start(const char *ssid, const char *password,
                            uint32_t timeout_ms, uint32_t *lease);
int bk7258_wifi_trial_finish(uint32_t lease, bool commit, uint32_t *ticket);
#  else
int bk7258_wifi_controller_initialize(void);
bool bk7258_wifi_controller_active(void);
#  endif
int bk7258_wifi_control_initialize(void);
#  ifndef CONFIG_BK7258_AP_CORE
int bk7258_wifi_control_request(enum bk7258_wifi_operation_e operation,
                                const char *ssid, const char *password,
                                const struct bk7258_wifi_echo_s *echo,
                                uint32_t timeout_ms,
                                struct bk7258_wifi_result_s *result);
int bk7258_wifi_monitor_request(enum bk7258_wifi_operation_e operation,
                                uint32_t channel, uint32_t timeout_ms,
                                struct bk7258_wifi_monitor_result_s *result);
int bk7258_wifi_scan_request(uint32_t timeout_ms,
                             struct bk7258_wifi_scan_result_s *result);
#    ifdef CONFIG_BK7258_WIFI_CSI
int bk7258_wifi_csi_request(enum bk7258_wifi_operation_e operation,
                            const struct bk7258_wifi_csi_config_s *config,
                            struct bk7258_wifi_csi_result_s *result);
#    endif
#  endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H */
