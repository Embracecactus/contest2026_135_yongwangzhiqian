/****************************************************************************
 * chips/bk7258/include/bk7258_bt_ipc.h
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
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BT_TEST_RESULT_MAGIC    0x54544242u /* "BBTT" */
#define BK7258_BT_TEST_RESULT_VERSION  10u
#define BK7258_BT_TEST_RESULT_SIZE     464u
#define BK7258_BT_TEST_SCAN_MIN_MS     100u
#define BK7258_BT_TEST_SCAN_MAX_MS     30000u
#define BK7258_BT_TEST_TIMEOUT_MIN_MS  1000u
#define BK7258_BT_TEST_TIMEOUT_MAX_MS  60000u

#define BK7258_BT_LIFECYCLE_MAGIC      0x434c5442u /* "BTLC" */
#define BK7258_BT_LIFECYCLE_VERSION    1u

#define BK7258_BT_ATT_TRACE_DEPTH        16u
#define BK7258_BT_ATT_TRACE_TX           (1u << 31)
#define BK7258_BT_ATT_TRACE_LENGTH_SHIFT 16u
#define BK7258_BT_ATT_TRACE_LENGTH_MASK  0x7fffu
#define BK7258_BT_ATT_TRACE_CID_MASK     0xffffu

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_bt_test_operation_e
{
  BK7258_BT_TEST_OPERATION_INFO = 1,
  BK7258_BT_TEST_OPERATION_SCAN,
  BK7258_BT_TEST_OPERATION_STATS
};

enum bk7258_bt_lifecycle_state_e
{
  BK7258_BT_LIFECYCLE_CLOSED = 0,
  BK7258_BT_LIFECYCLE_OPEN,
  BK7258_BT_LIFECYCLE_UNKNOWN
};

/* AP and CP keep one local copy of this record.  It is deliberately not a
 * shared-memory protocol: ELF symbols let SWD inspect each owner directly,
 * while runtime ownership is carried by the versioned RPTUN control flags.
 */

struct bk7258_bt_lifecycle_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  uint32_t init_requests;
  uint32_t init_successes;
  uint32_t deinit_requests;
  uint32_t deinit_successes;
  uint32_t unknown_transitions;
  int32_t  last_error;
  uint32_t validation_requested;
  uint32_t validation_completed;
  int32_t  validation_status;
  uint32_t status_mismatches;
  uint32_t reconciled_timeouts;
  uint32_t reserved;
};

static_assert(sizeof(struct bk7258_bt_lifecycle_diag_s) == 64u,
              "BK7258 Bluetooth lifecycle diagnostic ABI changed");

struct bk7258_bt_att_trace_s
{
  uint32_t meta;
  uint32_t data0;
  uint32_t data1;
};

static_assert(sizeof(struct bk7258_bt_att_trace_s) == 12u,
              "BK7258 Bluetooth ATT trace entry ABI changed");

struct bk7258_bt_hci_stats_s
{
  uint32_t command_tx;
  uint32_t acl_tx;
  uint32_t event_rx;
  uint32_t acl_rx;
  uint32_t invalid_rx;
  uint32_t receive_errors;
  uint32_t last_command;
  uint32_t last_acl_tx;
  uint32_t last_event_header;
  uint32_t last_event_payload;
  uint32_t last_acl_rx;
  uint32_t last_acl_rx_payload0;
  uint32_t last_acl_rx_payload1;
  uint32_t host_conn_rx;
  uint32_t host_l2cap_rx;
  uint32_t host_l2cap_tx;
  uint32_t host_conn_tx;
  uint32_t host_bt_send_acl;
  uint32_t host_bt_send_errors;
  uint32_t last_l2cap_rx;
  uint32_t last_l2cap_tx;
  uint32_t host_gatt_connected;
  uint32_t host_gatt_disconnected;
  uint32_t host_pdu_alloc;
  uint32_t host_pdu_fail;
  uint32_t host_mtu_clamped;
  uint32_t last_mtu_clamp;
  uint32_t hci_cmd_complete;
  uint32_t hci_cmd_status;
  uint32_t last_cmd_complete;
  uint32_t last_cmd_status;
  uint32_t host_num_completed_dropped;
  uint32_t hci_le_connected;
  uint32_t hci_disconnected;
  uint32_t last_disconnection;
  uint32_t host_att_trace_sequence;
  struct bk7258_bt_att_trace_s host_att_trace[BK7258_BT_ATT_TRACE_DEPTH];
};

static_assert(sizeof(struct bk7258_bt_hci_stats_s) == 336u,
              "BK7258 Bluetooth HCI stats ABI changed");

/* The RPMsg frame has exactly 12 spare bytes after the N12 result.  N13 keeps
 * a compact, saturating lifecycle view here so bkbttest can validate repeated
 * advertising without requiring J-Link.  Full diagnostic counters remain in
 * struct bk7258_ble_gatt_stats_s on AP.
 */

struct bk7258_bt_gatt_lifecycle_s
{
  uint8_t  state;
  uint8_t  worker_cpu;
  int16_t  last_error;
  uint16_t connected;
  uint16_t disconnected;
  uint16_t readvertised;
  uint16_t queue_full;
};

static_assert(sizeof(struct bk7258_bt_gatt_lifecycle_s) == 12u,
              "BK7258 Bluetooth GATT lifecycle ABI changed");

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
  struct bk7258_bt_hci_stats_s hci;
  struct bk7258_bt_gatt_lifecycle_s gatt;
};

static_assert(sizeof(struct bk7258_bt_test_result_s) ==
              BK7258_BT_TEST_RESULT_SIZE,
              "BK7258 Bluetooth test result ABI changed");

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
extern volatile struct bk7258_bt_lifecycle_diag_s
  g_bk7258_bt_ap_lifecycle;
int bk7258_bt_hci_initialize(void);
int bk7258_bt_hci_get_stats(struct bk7258_bt_hci_stats_s *stats);
#else
extern volatile struct bk7258_bt_lifecycle_diag_s
  g_bk7258_bt_cp_lifecycle;
int bk7258_bt_controller_ipc_initialize(void);
int bk7258_bt_controller_initialize(void);
#endif

#if defined(CONFIG_BK7258_BT_IPC_TEST) && \
    !defined(CONFIG_BK7258_AP_CORE)
int bk7258_bt_test_run(enum bk7258_bt_test_operation_e operation,
                       uint32_t scan_duration_ms, uint32_t timeout_ms,
                       struct bk7258_bt_test_result_s *result);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BT_IPC_H */
