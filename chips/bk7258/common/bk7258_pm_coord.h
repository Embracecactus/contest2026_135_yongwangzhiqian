/****************************************************************************
 * board/bk7258/chip/common/bk7258_pm_coord.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private CP/AP interface for the v3.1.1.9 coordinated low-voltage path.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_COMMON_BK7258_PM_COORD_H
#define __BOARD_BK7258_CHIP_COMMON_BK7258_PM_COORD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_PM_COORD_DIAG_MAGIC       0x44504d42u /* "BMPD" */
#define BK7258_PM_COORD_DIAG_VERSION     2u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_pm_coord_reason_e
{
  BK7258_PM_COORD_REASON_NONE = 0,
  BK7258_PM_COORD_REASON_NOT_READY,
  BK7258_PM_COORD_REASON_ACTIVE_VOTE,
  BK7258_PM_COORD_REASON_RPTUN_BUSY,
  BK7258_PM_COORD_REASON_MBOX_BUSY,
  BK7258_PM_COORD_REASON_CP_IRQ_PENDING,
  BK7258_PM_COORD_REASON_AP_TIMEOUT,
  BK7258_PM_COORD_REASON_AP_VOTE_CHANGED,
  BK7258_PM_COORD_REASON_FLASH_PREPARE,
  BK7258_PM_COORD_REASON_WAKE_NOT_ARMED,
  BK7258_PM_COORD_REASON_ENTERED,
  BK7258_PM_COORD_REASON_SDK_ACTIVITY,
  BK7258_PM_COORD_REASON_SDK_REJECTED,
};

struct bk7258_pm_coord_diag_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t attempts;
  uint32_t entered;
  uint32_t aborted;
  uint32_t wakeups;
  uint32_t last_reason;
  uint32_t last_aon_r3;
  uint32_t last_ap_sleep_vote_low;
  uint32_t last_ap_sleep_vote_high;
  uint32_t last_ap_clock_vote_low;
  uint32_t last_ap_clock_vote_high;
  uint32_t last_sleep_us;
  uint32_t compensated_ticks;
  uint32_t last_cp_sdk_awake_low;
  uint32_t last_cp_sdk_awake_high;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef CONFIG_BK7258_AP_CORE
extern volatile struct bk7258_pm_coord_diag_s g_bk7258_pm_coord_diag;
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
bool bk7258_pm_ap_runtime_ready(void);
bool bk7258_pm_ap_release_peer(void);
void bk7258_pm_ap_idle(void);
#else
void bk7258_pm_coord_early_initialize(void);
int bk7258_pm_coord_initialize(void);
bool bk7258_pm_cp_can_standby(void);
bool bk7258_pm_cp_standby(void);
#endif

#endif /* __BOARD_BK7258_CHIP_COMMON_BK7258_PM_COORD_H */
