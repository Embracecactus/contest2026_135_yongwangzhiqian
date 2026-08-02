/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/include/
 * bk7258_bt_ipc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned integration boundary between NuttX Bluetooth and the Beken
 * CP/AP Bluetooth mailbox IPC implementation.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BT_IPC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BT_IPC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BT_TEST_RESULT_MAGIC    0x54544242u /* "BBTT" */
#define BK7258_BT_TEST_RESULT_VERSION  2u
#define BK7258_BT_TEST_RESULT_SIZE     128u
#define BK7258_BT_TEST_SCAN_MIN_MS     100u
#define BK7258_BT_TEST_SCAN_MAX_MS     30000u
#define BK7258_BT_TEST_TIMEOUT_MIN_MS  1000u
#define BK7258_BT_TEST_TIMEOUT_MAX_MS  60000u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_bt_test_operation_e
{
  BK7258_BT_TEST_OPERATION_INFO = 1,
  BK7258_BT_TEST_OPERATION_SCAN
};

struct bk7258_bt_test_result_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t sequence;
  uint32_t operation;
  int32_t  status;
  uint32_t worker_cpu;
  uint32_t scan_duration_ms;
  uint32_t scan_results;
  uint32_t acl_mtu;
  uint32_t acl_buffers;
  uint8_t  bdaddr[6];
  uint8_t  address_valid;
  uint8_t  address_fallback;
  uint8_t  features[8];
  uint8_t  le_features[8];
  uint8_t  first_addr[6];
  uint8_t  first_addr_type;
  int8_t   first_rssi;
  uint8_t  first_adv_type;
  uint8_t  first_adv_len;
  uint8_t  first_adv_data[32];
  uint8_t  selected_index;
  uint8_t  n12v_payload_match;
  uint8_t  reserved[12];
};

static_assert(sizeof(struct bk7258_bt_test_result_s) ==
              BK7258_BT_TEST_RESULT_SIZE,
              "BK7258 Bluetooth test result ABI changed");

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_bt_hci_initialize(void);
#else
int bk7258_bt_controller_ipc_initialize(void);
#endif

#if defined(CONFIG_BK7258_BT_IPC_TEST) && \
    !defined(CONFIG_BK7258_AP_CORE)
int bk7258_bt_test_run(enum bk7258_bt_test_operation_e operation,
                       uint32_t scan_duration_ms, uint32_t timeout_ms,
                       struct bk7258_bt_test_result_s *result);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BT_IPC_H */
