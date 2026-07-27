/****************************************************************************
 * arch/arm/include/bk7258/bk7258_amp.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/AP image layout and the Stage N7 shared boot-state protocol.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_AMP_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_AMP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Logical flash layout.  CPU-visible XIP addresses are FLASH_XIP_BASE plus
 * the logical offsets below.  CRC-packed physical offsets use the 34/32
 * expansion because all partition boundaries are 32-byte aligned.
 */

#define BK7258_FLASH_XIP_BASE            0x02000000u
#define BK7258_BOOT_FLASH_OFFSET         0x00000000u
#define BK7258_BOOT_FLASH_SIZE           0x00010000u
#define BK7258_CP_FLASH_OFFSET           0x00010000u
#define BK7258_CP_FLASH_SIZE             0x000f0000u
#define BK7258_DATA_FLASH_OFFSET         0x00100000u
#define BK7258_DATA_FLASH_SIZE           0x00100000u
#define BK7258_AP_FLASH_OFFSET           0x00200000u
#define BK7258_AP_FLASH_SIZE             0x00200000u

#define BK7258_CP_FLASH_ADDR             \
  (BK7258_FLASH_XIP_BASE + BK7258_CP_FLASH_OFFSET)
#define BK7258_AP_FLASH_ADDR             \
  (BK7258_FLASH_XIP_BASE + BK7258_AP_FLASH_OFFSET)

#define BK7258_CRC_PHYSICAL_OFFSET(n)    (((n) / 32u) * 34u)
#define BK7258_CP_PHYSICAL_OFFSET        \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_CP_FLASH_OFFSET)
#define BK7258_DATA_PHYSICAL_OFFSET      \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_DATA_FLASH_OFFSET)
#define BK7258_AP_PHYSICAL_OFFSET        \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_AP_FLASH_OFFSET)

/* The 640 KiB SRAM window is split between two independent NuttX kernels.
 * The top 4 KiB page is excluded from both linkers and remains shared.
 */

#define BK7258_SRAM_BASE                 0x28000000u
#define BK7258_SRAM_SIZE                 0x000a0000u
#define BK7258_CP_RAM_BASE               0x28000000u
#define BK7258_CP_RAM_SIZE               0x00050000u
#define BK7258_AP_RAM_BASE               0x28050000u
#define BK7258_AP_RAM_SIZE               0x0004f000u
#define BK7258_SHARED_RAM_BASE           0x2809f000u
#define BK7258_SHARED_RAM_SIZE           0x00001000u

#define BK7258_CP_INITIAL_MSP            \
  (BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE - 4u)
#define BK7258_AP_INITIAL_MSP            \
  (BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE - 4u)

/* The SDK uses a per-core DTCM word as its local core-ID cell.  AP logical
 * core 0 writes zero there and reports SoC physical CPU1 as local + 1.
 */

#define BK7258_LOCAL_CORE_ID_ADDR        0x20000000u
#define BK7258_AP_PHYSICAL_ID_OFFSET     1u

/* Raw system and mailbox registers used by the minimal Stage N7 wrapper. */

#define BK7258_SYS_CPU1_CONTROL          0x44010014u
#define BK7258_SYS_CPU1_RESET            (1u << 0)
#define BK7258_SYS_CPU1_POWER_DOWN       (1u << 1)
#define BK7258_SYS_CPU1_RXEVT_SEL        (1u << 5)
#define BK7258_SYS_CPU1_BOOT_MASK        0xffffff00u

#define BK7258_MBOX0_BASE                0x41000000u /* CPU0 -> CPU1 */
#define BK7258_MBOX1_BASE                0x41020000u /* CPU1 -> CPU0 */
#define BK7258_MBOX_CLKRST_OFFSET        0x08u
#define BK7258_MBOX_READY_OFFSET         0x10u
#define BK7258_MBOX_CLEAR_OFFSET         0x14u
#define BK7258_MBOX_SENDER_OFFSET        0x18u
#define BK7258_MBOX_RECEIVER_OFFSET      0x1cu
#define BK7258_MBOX_PARAM0_OFFSET        0x20u
#define BK7258_MBOX_PARAM1_OFFSET        0x24u
#define BK7258_MBOX_PARAM2_OFFSET        0x28u
#define BK7258_MBOX_PARAM3_OFFSET        0x2cu
#define BK7258_MBOX_BOX0_BIT             (1u << 0)

#define BK7258_AP_BOOT_STATE_MAGIC       0x53425041u /* "APBS" */
#define BK7258_AP_BOOT_STATE_VERSION     1u
#define BK7258_AP_DOORBELL_MAGIC         0x524f4f44u /* "DOOR" */
#define BK7258_AP_DEFAULT_TIMEOUT_MS     3000u
#define BK7258_AP_RESTART_DELAY_MS       6u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_ap_command_e
{
  BK7258_AP_COMMAND_NONE = 0,
  BK7258_AP_COMMAND_START,
  BK7258_AP_COMMAND_STOP
};

enum bk7258_ap_state_e
{
  BK7258_AP_STATE_OFF = 0,
  BK7258_AP_STATE_STARTING,
  BK7258_AP_STATE_READY,
  BK7258_AP_STATE_STOPPING,
  BK7258_AP_STATE_STOPPED,
  BK7258_AP_STATE_FAILED
};

enum bk7258_ap_event_e
{
  BK7258_AP_EVENT_NONE = 0,
  BK7258_AP_EVENT_READY,
  BK7258_AP_EVENT_STOP,
  BK7258_AP_EVENT_STOPPED,
  BK7258_AP_EVENT_FAILED
};

enum bk7258_ap_error_e
{
  BK7258_AP_ERROR_NONE = 0,
  BK7258_AP_ERROR_BAD_BOOT_STATE,
  BK7258_AP_ERROR_BAD_CORE_ID,
  BK7258_AP_ERROR_BAD_VTOR,
  BK7258_AP_ERROR_BAD_SYSTICK,
  BK7258_AP_ERROR_HEAP,
  BK7258_AP_ERROR_TIMEOUT
};

struct bk7258_ap_boot_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t command;
  uint32_t state;
  uint32_t error;
  uint32_t last_event;
  uint32_t local_core_id;
  uint32_t physical_core_id;
  uint32_t initial_vtor;
  uint32_t initial_msp;
  uint32_t runtime_vtor;
  uint32_t runtime_msp;
  uint32_t clock_hz;
  uint32_t systick_ctrl;
  uint32_t systick_reload;
  uint32_t systick_current;
  uint32_t heap_start;
  uint32_t heap_end;
  uint32_t heap_test;
  uint32_t cp_to_ap_doorbells;
  uint32_t ap_to_cp_doorbells;
  uint32_t heartbeat;
  uint32_t ram_start;
  uint32_t ram_end;
  uint32_t flash_start;
  uint32_t flash_end;
  uint32_t reserved[4];
};

static_assert(BK7258_CP_FLASH_OFFSET + BK7258_CP_FLASH_SIZE ==
              BK7258_DATA_FLASH_OFFSET,
              "CP flash must end at the LittleFS boundary");
static_assert(BK7258_DATA_FLASH_OFFSET + BK7258_DATA_FLASH_SIZE ==
              BK7258_AP_FLASH_OFFSET,
              "AP flash must start after LittleFS");
static_assert(BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE ==
              BK7258_SHARED_RAM_BASE,
              "AP RAM must end at the shared page");
static_assert(sizeof(struct bk7258_ap_boot_state_s) <=
              BK7258_SHARED_RAM_SIZE,
              "AP boot state exceeds the shared page");

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

static inline volatile struct bk7258_ap_boot_state_s *
bk7258_ap_boot_state(void)
{
  return (volatile struct bk7258_ap_boot_state_s *)BK7258_SHARED_RAM_BASE;
}

#ifdef CONFIG_BK7258_AP_CONTROL
int bk7258_ap_control_initialize(void);
int bk7258_ap_start(uint32_t timeout_ms);
int bk7258_ap_stop(uint32_t timeout_ms);
int bk7258_ap_restart(uint32_t timeout_ms);
void bk7258_ap_get_status(struct bk7258_ap_boot_state_s *status);
#endif

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_ap_main(int argc, char *argv[]);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_AMP_H */
