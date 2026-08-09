/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_wifi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H

#include <stdbool.h>
#include <stdint.h>

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

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_wifi_operation_e
{
  BK7258_WIFI_OPERATION_CONNECT = 1,
  BK7258_WIFI_OPERATION_STATUS,
  BK7258_WIFI_OPERATION_PING,
  BK7258_WIFI_OPERATION_TCP_ECHO,
  BK7258_WIFI_OPERATION_UDP_ECHO
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

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_WIFI_VNET
#  ifdef CONFIG_BK7258_AP_CORE
int bk7258_wifi_initialize(void);
int bk7258_wifi_read_link(struct bk7258_wifi_result_s *result);
int bk7258_wifi_refresh_carrier(void);
int bk7258_wifi_retire_link(void);
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
#  endif
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_H */
