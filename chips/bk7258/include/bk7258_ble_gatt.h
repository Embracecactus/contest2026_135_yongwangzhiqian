/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_ble_gatt.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board-owned N13 BLE peripheral service ABI.  The Beken SDK remains the
 * controller owner and the unmodified NuttX Bluetooth stack remains the
 * host/ATT/GATT owner.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BLE_GATT_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BLE_GATT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BLE_GATT_SERVICE_UUID \
  "72580001-4e31-3347-4154-545f424c4500"
#define BK7258_BLE_GATT_CONTROL_UUID \
  "72580002-4e31-3347-4154-545f424c4500"
#define BK7258_BLE_GATT_STATUS_UUID \
  "72580003-4e31-3347-4154-545f424c4500"

#define BK7258_BLE_GATT_GAP_SERVICE_HANDLE       0x0001u
#define BK7258_BLE_GATT_NAME_CHRC_HANDLE         0x0003u
#define BK7258_BLE_GATT_NAME_VALUE_HANDLE        0x0004u
#define BK7258_BLE_GATT_APPEARANCE_CHRC_HANDLE   0x0005u
#define BK7258_BLE_GATT_APPEARANCE_VALUE_HANDLE  0x0006u
#define BK7258_BLE_GATT_SERVICE_HANDLE           0x0010u
#define BK7258_BLE_GATT_CONTROL_CHRC_HANDLE      0x0011u
#define BK7258_BLE_GATT_CONTROL_VALUE_HANDLE     0x0012u
#define BK7258_BLE_GATT_STATUS_CHRC_HANDLE       0x0013u
#define BK7258_BLE_GATT_STATUS_VALUE_HANDLE      0x0014u
#define BK7258_BLE_GATT_STATUS_CCC_HANDLE        0x0015u

#define BK7258_BLE_GATT_FRAME_MAGIC              0x31474c42u /* "BLG1" */
#define BK7258_BLE_GATT_FRAME_VERSION            1u
#define BK7258_BLE_GATT_FRAME_SIZE               20u
#define BK7258_BLE_GATT_FRAME_CRC_OFFSET         16u
#define BK7258_BLE_GATT_MAX_BURST                100u
#define BK7258_BLE_GATT_RESPONSE_BIT             0x80u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_ble_gatt_opcode_e
{
  BK7258_BLE_GATT_OPCODE_ECHO = 1,
  BK7258_BLE_GATT_OPCODE_BURST,
  BK7258_BLE_GATT_OPCODE_RESET_COUNTERS
};

enum bk7258_ble_gatt_state_e
{
  BK7258_BLE_GATT_STATE_DISABLED = 0,
  BK7258_BLE_GATT_STATE_INITIALIZING,
  BK7258_BLE_GATT_STATE_ADVERTISING,
  BK7258_BLE_GATT_STATE_CONNECTED,
  BK7258_BLE_GATT_STATE_FAULTED
};

struct bk7258_ble_gatt_frame_s
{
  uint8_t data[BK7258_BLE_GATT_FRAME_SIZE];
};

static_assert(sizeof(struct bk7258_ble_gatt_frame_s) ==
              BK7258_BLE_GATT_FRAME_SIZE,
              "BK7258 N13 GATT frame ABI changed");

struct bk7258_ble_gatt_stats_s
{
  uint32_t state;
  int32_t  last_error;
  uint32_t worker_cpu;
  uint32_t connected;
  uint32_t disconnected;
  uint32_t readvertised;
  uint32_t queue_full;
  uint32_t writes_accepted;
  uint32_t writes_bad_offset;
  uint32_t writes_bad_length;
  uint32_t writes_bad_magic;
  uint32_t writes_bad_version;
  uint32_t writes_bad_opcode;
  uint32_t writes_bad_count;
  uint32_t writes_bad_crc;
  uint32_t ccc_changes;
  uint32_t notify_attempted;
  uint32_t active_handle;
  uint32_t subscribed;
};

static_assert(sizeof(struct bk7258_ble_gatt_stats_s) == 76u,
              "BK7258 N13 GATT stats ABI changed");

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_BLE_GATT) && defined(CONFIG_BK7258_AP_CORE)
int bk7258_ble_gatt_initialize(void);
void bk7258_ble_gatt_hci_event(const uint8_t *buffer, uint16_t length);
int bk7258_ble_gatt_get_stats(struct bk7258_ble_gatt_stats_s *stats);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BLE_GATT_H */
