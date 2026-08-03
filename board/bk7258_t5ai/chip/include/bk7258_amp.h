/****************************************************************************
 * arch/arm/include/bk7258/bk7258_amp.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/AP image layout and the N7/N8 shared boot-state protocol.
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

/* ADR-004 official-style contiguous A/B layout.  CPU-visible executable
 * addresses are FLASH_XIP_BASE plus logical offsets.  Executable images use
 * the 34/32 CRC expansion; bk_flash_* data accesses use raw physical offsets.
 * Never apply the executable-image conversion to LittleFS.
 */

#define BK7258_FLASH_XIP_BASE            0x02000000u
#define BK7258_BOOT_FLASH_OFFSET         0x00000000u
#define BK7258_BOOT_FLASH_SIZE           0x00010000u
#define BK7258_CP_FLASH_OFFSET           0x00010000u
#define BK7258_CP_FLASH_SIZE             0x00140000u
#define BK7258_AP_FLASH_OFFSET           0x00150000u
#define BK7258_AP_FLASH_SIZE             0x00110000u

#define BK7258_CP_FLASH_ADDR             \
  (BK7258_FLASH_XIP_BASE + BK7258_CP_FLASH_OFFSET)
#define BK7258_AP_FLASH_ADDR             \
  (BK7258_FLASH_XIP_BASE + BK7258_AP_FLASH_OFFSET)

#define BK7258_CRC_PHYSICAL_OFFSET(n)    (((n) / 32u) * 34u)
#define BK7258_CP_PHYSICAL_OFFSET        \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_CP_FLASH_OFFSET)
#define BK7258_AP_PHYSICAL_OFFSET        \
  BK7258_CRC_PHYSICAL_OFFSET(BK7258_AP_FLASH_OFFSET)

/* Exact official v3.1.1.9 raw physical A/B envelopes. */

#define BK7258_BOOT_RAW_PHYSICAL_START   0x00000000u
#define BK7258_BOOT_RAW_PHYSICAL_SIZE    0x00011000u
#define BK7258_CP_RAW_PHYSICAL_START     0x00011000u
#define BK7258_CP_RAW_PHYSICAL_SIZE      0x00154000u
#define BK7258_AP_RAW_PHYSICAL_START     0x00165000u
#define BK7258_AP_RAW_PHYSICAL_SIZE      0x00121000u
#define BK7258_AB_SECONDARY_START        0x00286000u
#define BK7258_AB_SECONDARY_SIZE         0x00275000u
#define BK7258_AB_METADATA_START         0x004fb000u
#define BK7258_AB_METADATA_SIZE          0x00001000u
#define BK7258_USR_CONFIG_START          0x004fc000u
#define BK7258_USR_CONFIG_SIZE           0x0000e000u

/* The official bk_flash_* API consumes raw physical offsets.  ADR-004 moves
 * the one-MiB LittleFS window away from both executable pairs.  The owner
 * approved clearing the old data during the one-time migration.
 */

#define BK7258_DATA_RAW_PHYSICAL_OFFSET  0x00600000u
#define BK7258_DATA_RAW_PHYSICAL_SIZE    0x00100000u
#define BK7258_CALIBRATION_TAIL_START    0x007fa000u
#define BK7258_FLASH_RAW_SIZE            0x00800000u

/* The 640 KiB SRAM window is split between two independent NuttX kernels.
 * Keep the first 64 KiB as the AP SMP spinlock region used by the official
 * BK7258 SDK.  CP starts at 0x28010000 and still ends at 0x28050000, so the
 * AP/RPTUN/shared-page addresses remain unchanged.  The top 4 KiB page is
 * excluded from both linkers and remains shared.
 * N9 layout-only/RPTUN profiles additionally reserve the preceding 32 KiB
 * from the AP window.  The baseline N7/N8 profiles retain their exact linker
 * layout when CONFIG_BK7258_RPTUN_LAYOUT is disabled.
 */

#define BK7258_SRAM_BASE                 0x28000000u
#define BK7258_SRAM_SIZE                 0x000a0000u
#define BK7258_AP_SPINLOCK_BASE          0x28000000u
#define BK7258_AP_SPINLOCK_SIZE          0x00010000u
#define BK7258_CP_RAM_BASE               0x28010000u
#define BK7258_CP_RAM_SIZE               0x00040000u
#define BK7258_AP_RAM_BASE               0x28050000u
#define BK7258_RPTUN_SHMEM_BASE          0x28097000u
#define BK7258_RPTUN_SHMEM_SIZE          0x00008000u
#define BK7258_SHARED_RAM_BASE           0x2809f000u
#define BK7258_SHARED_RAM_SIZE           0x00001000u

#ifdef CONFIG_BK7258_RPTUN_LAYOUT
#  define BK7258_AP_RAM_SIZE             0x00047000u
#  define BK7258_AP_RAM_END              BK7258_RPTUN_SHMEM_BASE
#else
#  define BK7258_AP_RAM_SIZE             0x0004f000u
#  define BK7258_AP_RAM_END              BK7258_SHARED_RAM_BASE
#endif

#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
#  define BK7258_CPU2_BOOT_STACK_SIZE    CONFIG_ARCH_INTERRUPTSTACK
#else
#  define BK7258_CPU2_BOOT_STACK_SIZE    0x00000400u
#endif

#define BK7258_CPU2_BOOT_STACK_TOP       BK7258_AP_RAM_END
#define BK7258_CPU2_BOOT_STACK_BASE      \
  (BK7258_CPU2_BOOT_STACK_TOP - BK7258_CPU2_BOOT_STACK_SIZE)

/* Preserve the N8-A names for the AP-UP probe implementation. */

#define BK7258_CPU2_PROBE_STACK_SIZE     BK7258_CPU2_BOOT_STACK_SIZE
#define BK7258_CPU2_PROBE_STACK_TOP      BK7258_CPU2_BOOT_STACK_TOP
#define BK7258_CPU2_PROBE_STACK_BASE     BK7258_CPU2_BOOT_STACK_BASE

#define BK7258_CP_HEAP_END               \
  (BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE - 4u)
#define BK7258_AP_HEAP_END               \
  (BK7258_CPU2_BOOT_STACK_BASE - 4u)

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

#define BK7258_SYS_CPU2_CONTROL          0x44010018u
#define BK7258_SYS_CPU2_RESET            (1u << 0)
#define BK7258_SYS_CPU2_POWER_DOWN       (1u << 1)
#define BK7258_SYS_CPU2_HALT             (1u << 3)
#define BK7258_SYS_CPU2_RXEVT_SEL        (1u << 5)
#define BK7258_SYS_CPU2_BOOT_MASK        0xffffff00u

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
#ifdef CONFIG_BK7258_PSRAM_TEST
#  define BK7258_AP_DEFAULT_TIMEOUT_MS   60000u
#else
#  define BK7258_AP_DEFAULT_TIMEOUT_MS   15000u
#endif
#define BK7258_AP_RESTART_DELAY_MS       6u

/* Keep the 0x80-byte boot-state ABI stable.  A fault-only extension lives
 * immediately after it in the otherwise unused shared page.
 */

#define BK7258_AP_FAULT_STATE_OFFSET     0x00000080u
#define BK7258_AP_FAULT_STATE_MAGIC      0x544c4641u /* "AFLT" */
#define BK7258_AP_FAULT_STATE_VERSION    1u

/* CPU0 owns a separate record so an AP-side peripheral or mailbox operation
 * that faults CP cannot overwrite the AP exception evidence.
 */

#define BK7258_CP_FAULT_STATE_OFFSET     0x00000100u
#define BK7258_CP_FAULT_STATE_MAGIC      0x544c4643u /* "CFLT" */
#define BK7258_CP_FAULT_STATE_VERSION    1u

/* Physical CPU2 shared state.  N8-A uses it for the freestanding probe;
 * N8-B1 preserves the ABI while publishing the NuttX secondary-bootstrap
 * contract and the still-offline scheduler mask.
 */

#define BK7258_CPU2_PROBE_STATE_OFFSET   0x00000180u
#define BK7258_CPU2_PROBE_STATE_MAGIC    0x32555043u /* "CPU2" */
#define BK7258_CPU2_PROBE_STATE_VERSION  1u
#define BK7258_CPU2_PROBE_TIMEOUT_MS     1000u
#define BK7258_CPU2_PROBE_STOP_TIMEOUT_MS 100u

/* N8-B2 keeps bidirectional IPI diagnostics in a separate 0x80-byte record
 * so the board-verified N8-A/N8-B1 CPU2 ABI remains unchanged.
 */

#define BK7258_AP_IPI_STATE_OFFSET       0x00000200u
#define BK7258_AP_IPI_STATE_MAGIC        0x49504942u /* "BIPI" */
#define BK7258_AP_IPI_STATE_VERSION      1u
#define BK7258_AP_IPI_DEFAULT_COUNT      100u
#define BK7258_AP_IPI_MAX_COUNT          4095u
#define BK7258_AP_IPI_DEFAULT_TIMEOUT_MS 3000u

/* N8-C1 keeps scheduler-online diagnostics separate from the board-verified
 * N8-B2 IPI ABI.
 */

#define BK7258_AP_SMP_STATE_OFFSET       0x00000280u
#define BK7258_AP_SMP_STATE_MAGIC        0x504d5342u /* "BSMP" */
#define BK7258_AP_SMP_STATE_VERSION      1u
#define BK7258_AP_SMP_DEFAULT_TIMEOUT_MS 3000u

/* N8-C2 keeps the explicit CPU1-affinity task gate separate from the
 * board-verified N8-C1 scheduler-online ABI.
 */

#define BK7258_AP_AFFINITY_STATE_OFFSET  0x00000300u
#define BK7258_AP_AFFINITY_STATE_MAGIC   0x46464142u /* "BAFF" */
#define BK7258_AP_AFFINITY_STATE_VERSION 1u
#define BK7258_AP_AFFINITY_TIMEOUT_MS    3000u

/* N8-C3 keeps the CPU1 semaphore-block/wake proof separate from the
 * board-verified N8-C2 affinity ABI.
 */

#define BK7258_AP_SEM_WAKE_STATE_OFFSET  0x00000380u
#define BK7258_AP_SEM_WAKE_STATE_MAGIC   0x4d455342u /* "BSEM" */
#define BK7258_AP_SEM_WAKE_STATE_VERSION 1u
#define BK7258_AP_SEM_WAKE_TIMEOUT_MS    3000u

/* N8-C4 preserves the N8-C3 first-wake proof and records the fixed
 * eight-cycle semaphore wake loop in the next shared-state slot.
 */

#define BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET  0x00000400u
#define BK7258_AP_SEM_WAKE_LOOP_STATE_MAGIC   0x4c575342u /* "BSWL" */
#define BK7258_AP_SEM_WAKE_LOOP_STATE_VERSION 1u
#define BK7258_AP_SEM_WAKE_LOOP_TIMEOUT_MS    3000u
#define BK7258_AP_SEM_WAKE_LOOP_CYCLES        8u

/* N8-C5 through N8-D1 share one generic exact-0x80 advanced-stage ABI struct
 * used at five separate shared-page offsets:
 *
 *   0x480  BP2P  bidirectional pingpong
 *   0x500  BDUL  dual CPU1 tasks
 *   0x580  BMIG  controlled migration
 *   0x600  BTIM  timed wake
 *   0x680  BLCY  scheduler quiesce/resume foundation
 *
 * A useful 32-word layout is defined below.
 */

#define BK7258_AP_ADV_CYCLES                 8u
#define BK7258_AP_ADV_TIMEOUT_MS             3000u
#define BK7258_AP_ADV_TIMED_INTERVAL_US      20000u

#define BK7258_AP_BP2P_STATE_OFFSET          0x00000480u
#define BK7258_AP_BP2P_STATE_MAGIC           0x50325042u /* "BP2P" */
#define BK7258_AP_BP2P_STATE_VERSION         1u

#define BK7258_AP_BDUL_STATE_OFFSET          0x00000500u
#define BK7258_AP_BDUL_STATE_MAGIC           0x4c554442u /* "BDUL" */
#define BK7258_AP_BDUL_STATE_VERSION         1u

#define BK7258_AP_BMIG_STATE_OFFSET          0x00000580u
#define BK7258_AP_BMIG_STATE_MAGIC           0x47494d42u /* "BMIG" */
#define BK7258_AP_BMIG_STATE_VERSION         1u

#define BK7258_AP_BTIM_STATE_OFFSET          0x00000600u
#define BK7258_AP_BTIM_STATE_MAGIC           0x4d495442u /* "BTIM" */
#define BK7258_AP_BTIM_STATE_VERSION         1u

#define BK7258_AP_BLCY_STATE_OFFSET          0x00000680u
#define BK7258_AP_BLCY_STATE_MAGIC           0x59434c42u /* "BLCY" */
#define BK7258_AP_BLCY_STATE_VERSION         1u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_ap_command_e
{
  BK7258_AP_COMMAND_NONE = 0,
  BK7258_AP_COMMAND_START,
  BK7258_AP_COMMAND_STOP,
  BK7258_AP_COMMAND_IPI_TEST
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
  BK7258_AP_EVENT_FAILED,
  BK7258_AP_EVENT_IPI_TEST,
  BK7258_AP_EVENT_IPI_TEST_PASSED,
  BK7258_AP_EVENT_IPI_TEST_FAILED
};

enum bk7258_ap_error_e
{
  BK7258_AP_ERROR_NONE = 0,
  BK7258_AP_ERROR_BAD_BOOT_STATE,
  BK7258_AP_ERROR_BAD_CORE_ID,
  BK7258_AP_ERROR_BAD_VTOR,
  BK7258_AP_ERROR_BAD_SYSTICK,
  BK7258_AP_ERROR_HEAP,
  BK7258_AP_ERROR_TIMEOUT,
  BK7258_AP_ERROR_NMI,
  BK7258_AP_ERROR_HARDFAULT,
  BK7258_AP_ERROR_CPU2_PROBE,
  BK7258_AP_ERROR_CPU2_SMP_BOOTSTRAP,
  BK7258_AP_ERROR_CPU2_IPI,
  BK7258_AP_ERROR_CPU2_SMP_SCHEDULER,
  BK7258_AP_ERROR_CPU2_AFFINITY,
  BK7258_AP_ERROR_CPU2_SEM_WAKE,
  BK7258_AP_ERROR_CPU2_SEM_WAKE_LOOP,
  BK7258_AP_ERROR_CPU2_BP2P,
  BK7258_AP_ERROR_CPU2_BDUL,
  BK7258_AP_ERROR_CPU2_BMIG,
  BK7258_AP_ERROR_CPU2_BTIM,
  BK7258_AP_ERROR_CPU2_BLCY,
  BK7258_AP_ERROR_SUPERVISOR,
  BK7258_AP_ERROR_RPMSGFS,
  BK7258_AP_ERROR_BLUETOOTH,
  BK7258_AP_ERROR_PSRAM
};

/* N10 supervisor state is CP-private.  Only the existing heartbeat/epoch
 * words cross the CP/AP boundary, so this status structure consumes neither
 * the fixed 0x40-byte RPTUN control ABI nor the vendor-reserved PWR_MNG/SWAP
 * tail at shared offset 0x700.
 */

#define BK7258_AP_SUPERVISOR_STATUS_VERSION 1u

#define BK7258_AP_SUPERVISOR_FLAG_ARMED       (1u << 0)
#define BK7258_AP_SUPERVISOR_FLAG_PRIMARY     (1u << 1)
#define BK7258_AP_SUPERVISOR_FLAG_SECONDARY   (1u << 2)
#define BK7258_AP_SUPERVISOR_FLAG_RPMSG_READY (1u << 3)
#define BK7258_AP_SUPERVISOR_FLAG_RPMSG_OK    (1u << 4)
#define BK7258_AP_SUPERVISOR_FLAG_FAULT_SAVED (1u << 5)
#define BK7258_AP_SUPERVISOR_FLAG_AUTO_RECOVER (1u << 6)
#define BK7258_AP_SUPERVISOR_FLAG_INJECTED     (1u << 7)

enum bk7258_ap_supervisor_state_e
{
  BK7258_AP_SUPERVISOR_OFFLINE = 0,
  BK7258_AP_SUPERVISOR_ARMING,
  BK7258_AP_SUPERVISOR_HEALTHY,
  BK7258_AP_SUPERVISOR_SUSPECT,
  BK7258_AP_SUPERVISOR_FAULTED,
  BK7258_AP_SUPERVISOR_RECOVERING,
  BK7258_AP_SUPERVISOR_LOCKOUT
};

enum bk7258_ap_supervisor_reason_e
{
  BK7258_AP_SUPERVISOR_REASON_NONE = 0,
  BK7258_AP_SUPERVISOR_REASON_BAD_SHARED_STATE,
  BK7258_AP_SUPERVISOR_REASON_AP_REPORTED_FAILURE,
  BK7258_AP_SUPERVISOR_REASON_AP_EXCEPTION,
  BK7258_AP_SUPERVISOR_REASON_PRIMARY_TIMEOUT,
  BK7258_AP_SUPERVISOR_REASON_SECONDARY_TIMEOUT,
  BK7258_AP_SUPERVISOR_REASON_RPTUN_DISCONNECTED,
  BK7258_AP_SUPERVISOR_REASON_RPMSG_TIMEOUT,
  BK7258_AP_SUPERVISOR_REASON_RECOVERY_FAILED
};

enum bk7258_ap_supervisor_injection_e
{
  BK7258_AP_SUPERVISOR_INJECT_NONE = 0,
  BK7258_AP_SUPERVISOR_INJECT_PRIMARY,
  BK7258_AP_SUPERVISOR_INJECT_SECONDARY,
  BK7258_AP_SUPERVISOR_INJECT_RPMSG
};

struct bk7258_ap_supervisor_status_s
{
  uint32_t version;
  uint32_t size;
  uint32_t state;
  uint32_t reason;
  uint32_t generation;
  uint32_t flags;
  uint32_t primary_heartbeat;
  uint32_t secondary_heartbeat;
  uint32_t transport_sequence;
  uint32_t primary_age_ms;
  uint32_t secondary_age_ms;
  uint32_t transport_age_ms;
  uint32_t fault_count;
  uint32_t recovery_count;
  uint32_t consecutive_failures;
  uint32_t injection;
  int32_t  last_error;
  uint32_t fault_generation;
  uint32_t fault_exception;
  uint32_t fault_hfsr;
  uint32_t fault_cfsr;
  uint32_t fault_pc;
  uint32_t fault_lr;
};

enum bk7258_cpu2_probe_command_e
{
  BK7258_CPU2_PROBE_COMMAND_NONE = 0,
  BK7258_CPU2_PROBE_COMMAND_START,
  BK7258_CPU2_PROBE_COMMAND_STOP
};

enum bk7258_cpu2_probe_state_e
{
  BK7258_CPU2_PROBE_STATE_OFF = 0,
  BK7258_CPU2_PROBE_STATE_STARTING,
  BK7258_CPU2_PROBE_STATE_READY,
  BK7258_CPU2_PROBE_STATE_STOPPING,
  BK7258_CPU2_PROBE_STATE_STOPPED,
  BK7258_CPU2_PROBE_STATE_FAILED,
  BK7258_CPU2_PROBE_STATE_BOOTSTRAP,
  BK7258_CPU2_PROBE_STATE_SECONDARY_READY,
  BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE
};

enum bk7258_cpu2_probe_error_e
{
  BK7258_CPU2_PROBE_ERROR_NONE = 0,
  BK7258_CPU2_PROBE_ERROR_BAD_BOOT_STATE,
  BK7258_CPU2_PROBE_ERROR_BAD_CORE_ID,
  BK7258_CPU2_PROBE_ERROR_BAD_VTOR,
  BK7258_CPU2_PROBE_ERROR_BAD_MSP,
  BK7258_CPU2_PROBE_ERROR_TIMEOUT,
  BK7258_CPU2_PROBE_ERROR_NMI,
  BK7258_CPU2_PROBE_ERROR_HARDFAULT,
  BK7258_CPU2_PROBE_ERROR_BAD_IDLE_STACK,
  BK7258_CPU2_PROBE_ERROR_IPI_UNAVAILABLE,
  BK7258_CPU2_PROBE_ERROR_IPI_INIT
};

enum bk7258_ap_ipi_state_e
{
  BK7258_AP_IPI_STATE_OFF = 0,
  BK7258_AP_IPI_STATE_INITIALIZING,
  BK7258_AP_IPI_STATE_READY,
  BK7258_AP_IPI_STATE_REQUESTED,
  BK7258_AP_IPI_STATE_RUNNING,
  BK7258_AP_IPI_STATE_PASSED,
  BK7258_AP_IPI_STATE_FAILED,
  BK7258_AP_IPI_STATE_STOPPED
};

enum bk7258_ap_ipi_error_e
{
  BK7258_AP_IPI_ERROR_NONE = 0,
  BK7258_AP_IPI_ERROR_BAD_STATE,
  BK7258_AP_IPI_ERROR_SDK_IRQ,
  BK7258_AP_IPI_ERROR_SDK_MAILBOX,
  BK7258_AP_IPI_ERROR_BAD_ENDPOINT,
  BK7258_AP_IPI_ERROR_BAD_COMMAND,
  BK7258_AP_IPI_ERROR_SEND,
  BK7258_AP_IPI_ERROR_TIMEOUT,
  BK7258_AP_IPI_ERROR_COUNT_MISMATCH,
  BK7258_AP_IPI_ERROR_DUPLICATE,
  BK7258_AP_IPI_ERROR_LOST
};

enum bk7258_ap_smp_state_e
{
  BK7258_AP_SMP_STATE_OFF = 0,
  BK7258_AP_SMP_STATE_INITIALIZING,
  BK7258_AP_SMP_STATE_ONLINE,
  BK7258_AP_SMP_STATE_TESTING,
  BK7258_AP_SMP_STATE_PASSED,
  BK7258_AP_SMP_STATE_FAILED
};

enum bk7258_ap_smp_error_e
{
  BK7258_AP_SMP_ERROR_NONE = 0,
  BK7258_AP_SMP_ERROR_BAD_STATE,
  BK7258_AP_SMP_ERROR_BAD_CPU,
  BK7258_AP_SMP_ERROR_SEND,
  BK7258_AP_SMP_ERROR_CALL,
  BK7258_AP_SMP_ERROR_TIMEOUT,
  BK7258_AP_SMP_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_affinity_state_e
{
  BK7258_AP_AFFINITY_STATE_OFF = 0,
  BK7258_AP_AFFINITY_STATE_INITIALIZING,
  BK7258_AP_AFFINITY_STATE_DISPATCHING,
  BK7258_AP_AFFINITY_STATE_RUNNING,
  BK7258_AP_AFFINITY_STATE_PASSED,
  BK7258_AP_AFFINITY_STATE_FAILED
};

enum bk7258_ap_affinity_error_e
{
  BK7258_AP_AFFINITY_ERROR_NONE = 0,
  BK7258_AP_AFFINITY_ERROR_BAD_STATE,
  BK7258_AP_AFFINITY_ERROR_ATTR,
  BK7258_AP_AFFINITY_ERROR_CREATE,
  BK7258_AP_AFFINITY_ERROR_TIMEOUT,
  BK7258_AP_AFFINITY_ERROR_BAD_CPU,
  BK7258_AP_AFFINITY_ERROR_BAD_MASK,
  BK7258_AP_AFFINITY_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_sem_wake_state_e
{
  BK7258_AP_SEM_WAKE_STATE_OFF = 0,
  BK7258_AP_SEM_WAKE_STATE_INITIALIZING,
  BK7258_AP_SEM_WAKE_STATE_WAITING,
  BK7258_AP_SEM_WAKE_STATE_BLOCKED,
  BK7258_AP_SEM_WAKE_STATE_POSTED,
  BK7258_AP_SEM_WAKE_STATE_WOKEN,
  BK7258_AP_SEM_WAKE_STATE_PASSED,
  BK7258_AP_SEM_WAKE_STATE_FAILED
};

enum bk7258_ap_sem_wake_error_e
{
  BK7258_AP_SEM_WAKE_ERROR_NONE = 0,
  BK7258_AP_SEM_WAKE_ERROR_BAD_STATE,
  BK7258_AP_SEM_WAKE_ERROR_SEM_INIT,
  BK7258_AP_SEM_WAKE_ERROR_WAIT_TIMEOUT,
  BK7258_AP_SEM_WAKE_ERROR_SEM_WAIT,
  BK7258_AP_SEM_WAKE_ERROR_SEM_POST,
  BK7258_AP_SEM_WAKE_ERROR_BAD_CPU,
  BK7258_AP_SEM_WAKE_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_sem_wake_loop_state_e
{
  BK7258_AP_SEM_WAKE_LOOP_STATE_OFF = 0,
  BK7258_AP_SEM_WAKE_LOOP_STATE_INITIALIZING,
  BK7258_AP_SEM_WAKE_LOOP_STATE_WAITING,
  BK7258_AP_SEM_WAKE_LOOP_STATE_BLOCKED,
  BK7258_AP_SEM_WAKE_LOOP_STATE_POSTED,
  BK7258_AP_SEM_WAKE_LOOP_STATE_WOKEN,
  BK7258_AP_SEM_WAKE_LOOP_STATE_CONTINUE,
  BK7258_AP_SEM_WAKE_LOOP_STATE_PASSED,
  BK7258_AP_SEM_WAKE_LOOP_STATE_FAILED
};

enum bk7258_ap_sem_wake_loop_error_e
{
  BK7258_AP_SEM_WAKE_LOOP_ERROR_NONE = 0,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_STATE,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_WAIT_TIMEOUT,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_WAKE_TIMEOUT,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_SEM_WAIT,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_SEM_POST,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_BAD_CPU,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE,
  BK7258_AP_SEM_WAKE_LOOP_ERROR_COUNT_MISMATCH
};

/* N8-C5/C6/C7/C8 generic advanced-stage state and error enums. */

enum bk7258_ap_bp2p_state_e
{
  BK7258_AP_BP2P_STATE_OFF = 0,
  BK7258_AP_BP2P_STATE_INITIALIZING,
  BK7258_AP_BP2P_STATE_RUNNING,
  BK7258_AP_BP2P_STATE_PASSED,
  BK7258_AP_BP2P_STATE_FAILED
};

enum bk7258_ap_bp2p_error_e
{
  BK7258_AP_BP2P_ERROR_NONE = 0,
  BK7258_AP_BP2P_ERROR_BAD_STATE,
  BK7258_AP_BP2P_ERROR_SEM_INIT,
  BK7258_AP_BP2P_ERROR_CREATE,
  BK7258_AP_BP2P_ERROR_WAIT_TIMEOUT,
  BK7258_AP_BP2P_ERROR_SEM_POST,
  BK7258_AP_BP2P_ERROR_BAD_CPU,
  BK7258_AP_BP2P_ERROR_SEQUENCE,
  BK7258_AP_BP2P_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_bdul_state_e
{
  BK7258_AP_BDUL_STATE_OFF = 0,
  BK7258_AP_BDUL_STATE_INITIALIZING,
  BK7258_AP_BDUL_STATE_RUNNING,
  BK7258_AP_BDUL_STATE_PASSED,
  BK7258_AP_BDUL_STATE_FAILED
};

enum bk7258_ap_bdul_error_e
{
  BK7258_AP_BDUL_ERROR_NONE = 0,
  BK7258_AP_BDUL_ERROR_BAD_STATE,
  BK7258_AP_BDUL_ERROR_SEM_INIT,
  BK7258_AP_BDUL_ERROR_CREATE,
  BK7258_AP_BDUL_ERROR_WAIT_TIMEOUT,
  BK7258_AP_BDUL_ERROR_SEM_POST,
  BK7258_AP_BDUL_ERROR_BAD_CPU,
  BK7258_AP_BDUL_ERROR_SEQUENCE,
  BK7258_AP_BDUL_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_bmig_state_e
{
  BK7258_AP_BMIG_STATE_OFF = 0,
  BK7258_AP_BMIG_STATE_INITIALIZING,
  BK7258_AP_BMIG_STATE_RUNNING,
  BK7258_AP_BMIG_STATE_PASSED,
  BK7258_AP_BMIG_STATE_FAILED
};

enum bk7258_ap_bmig_error_e
{
  BK7258_AP_BMIG_ERROR_NONE = 0,
  BK7258_AP_BMIG_ERROR_BAD_STATE,
  BK7258_AP_BMIG_ERROR_CREATE,
  BK7258_AP_BMIG_ERROR_SETAFFINITY,
  BK7258_AP_BMIG_ERROR_GETAFFINITY,
  BK7258_AP_BMIG_ERROR_BAD_CPU,
  BK7258_AP_BMIG_ERROR_TIMEOUT,
  BK7258_AP_BMIG_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_btim_state_e
{
  BK7258_AP_BTIM_STATE_OFF = 0,
  BK7258_AP_BTIM_STATE_INITIALIZING,
  BK7258_AP_BTIM_STATE_RUNNING,
  BK7258_AP_BTIM_STATE_PASSED,
  BK7258_AP_BTIM_STATE_FAILED
};

enum bk7258_ap_btim_error_e
{
  BK7258_AP_BTIM_ERROR_NONE = 0,
  BK7258_AP_BTIM_ERROR_BAD_STATE,
  BK7258_AP_BTIM_ERROR_CREATE,
  BK7258_AP_BTIM_ERROR_SLEEP,
  BK7258_AP_BTIM_ERROR_BAD_CPU,
  BK7258_AP_BTIM_ERROR_TIMEOUT,
  BK7258_AP_BTIM_ERROR_COUNT_MISMATCH
};

enum bk7258_ap_blcy_state_e
{
  BK7258_AP_BLCY_STATE_OFF = 0,
  BK7258_AP_BLCY_STATE_INITIALIZING,
  BK7258_AP_BLCY_STATE_RUNNING,
  BK7258_AP_BLCY_STATE_PASSED,
  BK7258_AP_BLCY_STATE_FAILED
};

enum bk7258_ap_blcy_error_e
{
  BK7258_AP_BLCY_ERROR_NONE = 0,
  BK7258_AP_BLCY_ERROR_BAD_STATE,
  BK7258_AP_BLCY_ERROR_CALL,
  BK7258_AP_BLCY_ERROR_QUIESCE_TIMEOUT,
  BK7258_AP_BLCY_ERROR_RESUME_TIMEOUT,
  BK7258_AP_BLCY_ERROR_BAD_CPU,
  BK7258_AP_BLCY_ERROR_STOP_GATE,
  BK7258_AP_BLCY_ERROR_COUNT_MISMATCH
};

/* N8-C5/C6/C7/C8/D1 shared generic 32-word stage ABI.  Used at five offsets:
 * 0x480 BP2P, 0x500 BDUL, 0x580 BMIG, 0x600 BTIM, 0x680 BLCY.
 */

struct bk7258_ap_advanced_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t error;
  uint32_t requested;
  uint32_t completed;
  uint32_t task_id[2];
  uint32_t task_cpu[2];
  uint32_t task_started[2];
  uint32_t task_completed[2];
  uint32_t sequence[2];
  int32_t  value[2];
  uint32_t smp_tx0_before;
  uint32_t smp_tx0_after;
  uint32_t smp_rx1_before;
  uint32_t smp_rx1_after;
  uint32_t smp_tx1_before;
  uint32_t smp_tx1_after;
  uint32_t smp_rx0_before;
  uint32_t smp_rx0_after;
  uint32_t calls_before;
  uint32_t calls_after;
  uint32_t aux[2];
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

struct bk7258_ap_fault_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t exception;
  uint32_t error;
  uint32_t exc_return;
  uint32_t stack_pointer;
  uint32_t hfsr;
  uint32_t cfsr;
  uint32_t mmfar;
  uint32_t bfar;
  uint32_t stacked_r0;
  uint32_t stacked_r1;
  uint32_t stacked_r2;
  uint32_t stacked_r3;
  uint32_t stacked_r12;
  uint32_t stacked_lr;
  uint32_t stacked_pc;
  uint32_t stacked_xpsr;
};

struct bk7258_cp_fault_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t exception;
  uint32_t reserved;
  uint32_t exc_return;
  uint32_t stack_pointer;
  uint32_t hfsr;
  uint32_t cfsr;
  uint32_t mmfar;
  uint32_t bfar;
  uint32_t stacked_r0;
  uint32_t stacked_r1;
  uint32_t stacked_r2;
  uint32_t stacked_r3;
  uint32_t stacked_r12;
  uint32_t stacked_lr;
  uint32_t stacked_pc;
  uint32_t stacked_xpsr;
};

struct bk7258_cpu2_probe_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t command;
  uint32_t state;
  uint32_t error;
  uint32_t local_core_id;
  uint32_t physical_core_id;
  uint32_t vector;
  uint32_t initial_msp;
  uint32_t runtime_vtor;
  uint32_t runtime_msp;
  uint32_t heartbeat;
  uint32_t control;
  uint32_t fault_exception;
  uint32_t fault_hfsr;
  uint32_t fault_cfsr;
  uint32_t fault_lr;
  uint32_t fault_pc;
  uint32_t fault_xpsr;
  uint32_t control_before;
  uint32_t control_after;
  uint32_t idle_stack_base;
  uint32_t idle_stack_top;
  uint32_t secondary_entry;
  uint32_t secondary_ready;
  uint32_t online_mask;
  uint32_t smp_call_requests;
  uint32_t boot_count;
  uint32_t reserved[2];
};

struct bk7258_ap_ipi_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t error;
  uint32_t requested_count;
  uint32_t completed_count;
  uint32_t timeout_ms;
  uint32_t test_runs;
  uint32_t tx_count[2];
  uint32_t rx_count[2];
  uint32_t last_tx_sequence[2];
  uint32_t last_rx_sequence[2];
  uint32_t duplicate_count[2];
  uint32_t lost_count[2];
  uint32_t send_failures[2];
  uint32_t irq_count[2];
  uint32_t wake_count[2];
  uint32_t spurious_count;
  uint32_t stale_count;
  uint32_t last_command[2];
};

struct bk7258_ap_smp_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t error;
  uint32_t online_mask;
  uint32_t boot_count;
  uint32_t tx_count[2];
  uint32_t rx_count[2];
  uint32_t coalesced_count[2];
  uint32_t send_failures[2];
  uint32_t call_handler_count[2];
  uint32_t delivered_handler_count[2];
  uint32_t callback_count[2];
  uint32_t last_command[2];
  uint32_t test_runs;
  uint32_t requested_count;
  uint32_t completed_count;
  uint32_t last_callback_cpu;
  uint32_t systick_irq_count[2];
  uint32_t sleep_enter_count;
  uint32_t sleep_return_count;
};

struct bk7258_ap_affinity_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t error;
  uint32_t requested_mask;
  uint32_t observed_mask;
  uint32_t task_id;
  uint32_t task_cpu;
  uint32_t task_started;
  uint32_t task_completed;
  uint32_t test_runs;
  uint32_t timeout_ms;
  uint32_t smp_tx_before;
  uint32_t smp_tx_after;
  uint32_t smp_rx_before;
  uint32_t smp_rx_after;
  uint32_t smp_fail_before;
  uint32_t smp_fail_after;
  uint32_t ipi_irq_before;
  uint32_t ipi_irq_after;
  uint32_t ipi_wake_before;
  uint32_t ipi_wake_after;
  uint32_t cpu2_calls_before;
  uint32_t cpu2_calls_after;
  uint32_t pid_released;
  uint32_t reserved[5];
};

struct bk7258_ap_sem_wake_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t error;
  uint32_t task_id;
  uint32_t test_runs;
  uint32_t timeout_ms;
  uint32_t wait_entered;
  uint32_t waiter_observed;
  int32_t waiter_sem_value;
  uint32_t post_count;
  uint32_t post_cpu;
  int32_t post_result;
  uint32_t wait_returned;
  int32_t wait_result;
  uint32_t wake_cpu;
  uint32_t smp_tx_before;
  uint32_t smp_tx_after;
  uint32_t smp_rx_before;
  uint32_t smp_rx_after;
  uint32_t smp_fail_before;
  uint32_t smp_fail_after;
  uint32_t ipi_irq_before;
  uint32_t ipi_irq_after;
  uint32_t ipi_wake_before;
  uint32_t ipi_wake_after;
  uint32_t cpu2_calls_before;
  uint32_t cpu2_calls_after;
  uint32_t reserved[2];
};

struct bk7258_ap_sem_wake_loop_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t error;
  uint32_t requested_cycles;
  uint32_t completed_cycles;
  uint32_t wait_entered;
  uint32_t waiter_observed;
  uint32_t post_count;
  uint32_t wait_returned;
  int32_t waiter_sem_value;
  uint32_t post_cpu;
  int32_t post_result;
  int32_t wait_result;
  uint32_t wake_cpu;
  uint32_t wait_sequence;
  uint32_t post_sequence;
  uint32_t wake_sequence;
  uint32_t smp_tx_before;
  uint32_t smp_tx_after;
  uint32_t smp_rx_before;
  uint32_t smp_rx_after;
  uint32_t smp_fail_before;
  uint32_t smp_fail_after;
  uint32_t ipi_irq_before;
  uint32_t ipi_irq_after;
  uint32_t ipi_wake_before;
  uint32_t ipi_wake_after;
  uint32_t cpu2_calls_before;
  uint32_t cpu2_calls_after;
};

static_assert(BK7258_CP_FLASH_OFFSET + BK7258_CP_FLASH_SIZE ==
              BK7258_AP_FLASH_OFFSET,
              "primary CP/AP logical windows must be contiguous");
static_assert(BK7258_CP_PHYSICAL_OFFSET ==
              BK7258_CP_RAW_PHYSICAL_START,
              "CP logical/raw address conversion drift");
static_assert(BK7258_CRC_PHYSICAL_OFFSET(BK7258_CP_FLASH_SIZE) ==
              BK7258_CP_RAW_PHYSICAL_SIZE,
              "CP logical/raw size conversion drift");
static_assert(BK7258_AP_PHYSICAL_OFFSET ==
              BK7258_AP_RAW_PHYSICAL_START,
              "AP logical/raw address conversion drift");
static_assert(BK7258_CRC_PHYSICAL_OFFSET(BK7258_AP_FLASH_SIZE) ==
              BK7258_AP_RAW_PHYSICAL_SIZE,
              "AP logical/raw size conversion drift");
static_assert(BK7258_AP_RAW_PHYSICAL_START +
              BK7258_AP_RAW_PHYSICAL_SIZE == BK7258_AB_SECONDARY_START,
              "primary pair must end at s_app");
static_assert(BK7258_CP_RAW_PHYSICAL_SIZE +
              BK7258_AP_RAW_PHYSICAL_SIZE == BK7258_AB_SECONDARY_SIZE,
              "primary and secondary pair sizes must match");
static_assert(BK7258_USR_CONFIG_START + BK7258_USR_CONFIG_SIZE <=
              BK7258_DATA_RAW_PHYSICAL_OFFSET,
              "vendor user config overlaps LittleFS");
static_assert(BK7258_DATA_RAW_PHYSICAL_OFFSET +
              BK7258_DATA_RAW_PHYSICAL_SIZE <=
              BK7258_CALIBRATION_TAIL_START,
              "LittleFS overlaps calibration tail");
static_assert(BK7258_CALIBRATION_TAIL_START < BK7258_FLASH_RAW_SIZE,
              "calibration tail must be inside Flash");
static_assert(BK7258_AP_SPINLOCK_BASE == BK7258_SRAM_BASE,
              "AP spinlock region must start at the official SRAM base");
static_assert(BK7258_AP_SPINLOCK_BASE + BK7258_AP_SPINLOCK_SIZE ==
              BK7258_CP_RAM_BASE,
              "AP spinlock region must end at CP RAM");
static_assert(BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE ==
              BK7258_AP_RAM_BASE,
              "CP RAM must end at AP RAM");
static_assert(BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE ==
              BK7258_AP_RAM_END,
              "AP RAM must end at its configured boundary");
static_assert(sizeof(struct bk7258_ap_boot_state_s) ==
              BK7258_AP_FAULT_STATE_OFFSET,
              "AP boot-state ABI must remain 0x80 bytes");
static_assert(BK7258_AP_FAULT_STATE_OFFSET +
              sizeof(struct bk7258_ap_fault_state_s) <=
              BK7258_CP_FAULT_STATE_OFFSET,
              "AP and CP fault states overlap");
static_assert(BK7258_CP_FAULT_STATE_OFFSET +
              sizeof(struct bk7258_cp_fault_state_s) <=
              BK7258_CPU2_PROBE_STATE_OFFSET,
              "CP fault state overlaps the CPU2 probe state");
static_assert(sizeof(struct bk7258_cpu2_probe_state_s) == 0x80,
              "CPU2 probe-state ABI must remain 0x80 bytes");
static_assert(BK7258_CPU2_PROBE_STATE_OFFSET +
              sizeof(struct bk7258_cpu2_probe_state_s) <=
              BK7258_AP_IPI_STATE_OFFSET,
              "CPU2 probe state overlaps the AP IPI state");
static_assert(sizeof(struct bk7258_ap_ipi_state_s) == 0x80,
              "AP IPI state ABI must remain 0x80 bytes");
static_assert(BK7258_AP_IPI_STATE_OFFSET +
              sizeof(struct bk7258_ap_ipi_state_s) <=
              BK7258_AP_SMP_STATE_OFFSET,
              "AP IPI state overlaps the AP SMP state");
static_assert(sizeof(struct bk7258_ap_smp_state_s) == 0x80,
              "AP SMP state ABI must remain 0x80 bytes");
static_assert(BK7258_AP_SMP_STATE_OFFSET +
              sizeof(struct bk7258_ap_smp_state_s) <=
              BK7258_AP_AFFINITY_STATE_OFFSET,
              "AP SMP state overlaps the AP affinity state");
static_assert(sizeof(struct bk7258_ap_affinity_state_s) == 0x80,
              "AP affinity state ABI must remain 0x80 bytes");
static_assert(BK7258_AP_AFFINITY_STATE_OFFSET +
              sizeof(struct bk7258_ap_affinity_state_s) ==
              BK7258_AP_SEM_WAKE_STATE_OFFSET,
              "AP affinity state must end at the AP semaphore-wake state");
static_assert(BK7258_AP_SEM_WAKE_STATE_OFFSET == 0x00000380u,
              "AP semaphore-wake state must start at shared offset 0x380");
static_assert(sizeof(struct bk7258_ap_sem_wake_state_s) == 0x80,
              "AP semaphore-wake state ABI must remain 0x80 bytes");
static_assert(BK7258_AP_SEM_WAKE_STATE_OFFSET +
              sizeof(struct bk7258_ap_sem_wake_state_s) ==
              BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET,
              "AP semaphore-wake state must end at the loop state");
static_assert(BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET == 0x00000400u,
              "AP semaphore-wake loop state must start at offset 0x400");
static_assert(sizeof(struct bk7258_ap_sem_wake_loop_state_s) == 0x80,
              "AP semaphore-wake loop state ABI must remain 0x80 bytes");
static_assert(BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET +
              sizeof(struct bk7258_ap_sem_wake_loop_state_s) == 0x00000480u,
              "AP semaphore-wake loop state must end at shared offset 0x480");
static_assert(BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET +
              sizeof(struct bk7258_ap_sem_wake_loop_state_s) <=
              BK7258_SHARED_RAM_SIZE,
              "AP semaphore-wake loop state exceeds the shared page");

/* N8-C5..D1 advanced-stage ABI size and non-overlap checks. */

static_assert(sizeof(struct bk7258_ap_advanced_state_s) == 0x80,
              "AP advanced-stage ABI must be exactly 0x80 bytes (32 words)");
static_assert(BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET +
              sizeof(struct bk7258_ap_sem_wake_loop_state_s) ==
              BK7258_AP_BP2P_STATE_OFFSET,
              "AP semaphore-wake loop state must end at the BP2P state");
static_assert(BK7258_AP_BP2P_STATE_OFFSET == 0x00000480u,
              "AP BP2P state must start at shared offset 0x480");
static_assert(BK7258_AP_BP2P_STATE_OFFSET +
              sizeof(struct bk7258_ap_advanced_state_s) ==
              BK7258_AP_BDUL_STATE_OFFSET,
              "BP2P and BDUL states must be contiguous");
static_assert(BK7258_AP_BDUL_STATE_OFFSET +
              sizeof(struct bk7258_ap_advanced_state_s) ==
              BK7258_AP_BMIG_STATE_OFFSET,
              "BDUL and BMIG states must be contiguous");
static_assert(BK7258_AP_BMIG_STATE_OFFSET +
              sizeof(struct bk7258_ap_advanced_state_s) ==
              BK7258_AP_BTIM_STATE_OFFSET,
              "BMIG and BTIM states must be contiguous");
static_assert(BK7258_AP_BTIM_STATE_OFFSET +
              sizeof(struct bk7258_ap_advanced_state_s) ==
              BK7258_AP_BLCY_STATE_OFFSET,
              "BTIM and BLCY states must be contiguous");
static_assert(BK7258_AP_BLCY_STATE_OFFSET +
              sizeof(struct bk7258_ap_advanced_state_s) == 0x00000700u,
              "AP BLCY state must end at shared offset 0x700");
static_assert(BK7258_AP_BLCY_STATE_OFFSET +
              sizeof(struct bk7258_ap_advanced_state_s) <=
              BK7258_SHARED_RAM_SIZE,
              "AP advanced-stage states exceed the shared page");
static_assert(BK7258_CPU2_PROBE_STACK_BASE >= BK7258_AP_RAM_BASE,
              "CPU2 probe stack must remain in AP-owned RAM");
static_assert(BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE ==
              BK7258_AP_RAM_END,
              "AP RAM size/end constants disagree");
#ifdef CONFIG_BK7258_RPTUN_LAYOUT
static_assert(BK7258_CPU2_PROBE_STACK_TOP == BK7258_RPTUN_SHMEM_BASE,
              "N9 CPU2 stack must end at the RPTUN carveout boundary");
static_assert(BK7258_RPTUN_SHMEM_BASE + BK7258_RPTUN_SHMEM_SIZE ==
              BK7258_SHARED_RAM_BASE,
              "RPTUN carveout must end at the telemetry-page boundary");
#else
static_assert(BK7258_CPU2_PROBE_STACK_TOP == BK7258_SHARED_RAM_BASE,
              "CPU2 probe stack must end at the shared-page boundary");
#endif

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

static inline volatile struct bk7258_ap_fault_state_s *
bk7258_ap_fault_state(void)
{
  return (volatile struct bk7258_ap_fault_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_FAULT_STATE_OFFSET);
}

static inline volatile struct bk7258_cp_fault_state_s *
bk7258_cp_fault_state(void)
{
  return (volatile struct bk7258_cp_fault_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_CP_FAULT_STATE_OFFSET);
}

static inline volatile struct bk7258_cpu2_probe_state_s *
bk7258_cpu2_probe_state(void)
{
  return (volatile struct bk7258_cpu2_probe_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_CPU2_PROBE_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_ipi_state_s *
bk7258_ap_ipi_state(void)
{
  return (volatile struct bk7258_ap_ipi_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_IPI_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_smp_state_s *
bk7258_ap_smp_state(void)
{
  return (volatile struct bk7258_ap_smp_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_SMP_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_affinity_state_s *
bk7258_ap_affinity_state(void)
{
  return (volatile struct bk7258_ap_affinity_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_AFFINITY_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_sem_wake_state_s *
bk7258_ap_sem_wake_state(void)
{
  return (volatile struct bk7258_ap_sem_wake_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_SEM_WAKE_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_sem_wake_loop_state_s *
bk7258_ap_sem_wake_loop_state(void)
{
  return (volatile struct bk7258_ap_sem_wake_loop_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_SEM_WAKE_LOOP_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_advanced_state_s *
bk7258_ap_bp2p_state(void)
{
  return (volatile struct bk7258_ap_advanced_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_BP2P_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_advanced_state_s *
bk7258_ap_bdul_state(void)
{
  return (volatile struct bk7258_ap_advanced_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_BDUL_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_advanced_state_s *
bk7258_ap_bmig_state(void)
{
  return (volatile struct bk7258_ap_advanced_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_BMIG_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_advanced_state_s *
bk7258_ap_btim_state(void)
{
  return (volatile struct bk7258_ap_advanced_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_BTIM_STATE_OFFSET);
}

static inline volatile struct bk7258_ap_advanced_state_s *
bk7258_ap_blcy_state(void)
{
  return (volatile struct bk7258_ap_advanced_state_s *)
    (BK7258_SHARED_RAM_BASE + BK7258_AP_BLCY_STATE_OFFSET);
}

#ifdef CONFIG_BK7258_AP_CONTROL
int bk7258_ap_control_initialize(void);
int bk7258_ap_start(uint32_t timeout_ms);
int bk7258_ap_stop(uint32_t timeout_ms);
int bk7258_ap_restart(uint32_t timeout_ms);
int bk7258_ap_ipi_test(uint32_t count, uint32_t timeout_ms);
void bk7258_ap_get_status(struct bk7258_ap_boot_state_s *status);
#  ifdef CONFIG_BK7258_AP_SUPERVISOR
int bk7258_ap_supervisor_initialize(void);
int bk7258_ap_supervisor_get_status(
  struct bk7258_ap_supervisor_status_s *status);
int bk7258_ap_supervisor_recover(uint32_t timeout_ms);
void bk7258_ap_supervisor_lifecycle_begin(void);
void bk7258_ap_supervisor_lifecycle_end(void);
#    ifdef CONFIG_BK7258_AP_SUPERVISOR_FAULT_INJECTION
int bk7258_ap_supervisor_inject(uint32_t injection);
#    endif
#  endif
#endif

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_ap_main(int argc, char *argv[]);
#  ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
int bk7258_ap_smp_secondary_stop(uint32_t timeout_ms);
#    ifdef CONFIG_BK7258_AP_IPI
int bk7258_ap_ipi_primary_initialize(void);
int bk7258_ap_ipi_secondary_initialize(void);
int bk7258_ap_ipi_selftest(uint32_t count, uint32_t timeout_ms);
int bk7258_ap_ipi_wake_secondary(void);
void bk7258_ap_ipi_mark_stopped(void);
#      ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
int bk7258_ap_ipi_send_smp(int target_cpu);
void bk7258_ap_ipi_mark_scheduler_online(void);
int bk7258_ap_smp_scheduler_selftest(uint32_t timeout_ms);
#        ifdef CONFIG_BK7258_AP_SMP_CPU1_AFFINITY
int bk7258_ap_smp_affinity_selftest(uint32_t timeout_ms);
#        endif
#        ifdef CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG
int bk7258_ap_smp_bp2p_selftest(uint32_t timeout_ms);
#        endif
#        ifdef CONFIG_BK7258_AP_SMP_CPU1_DUALTASK
int bk7258_ap_smp_bdul_selftest(uint32_t timeout_ms);
#        endif
#        ifdef CONFIG_BK7258_AP_SMP_CONTROLLED_MIGRATION
int bk7258_ap_smp_bmig_selftest(uint32_t timeout_ms);
#        endif
#        ifdef CONFIG_BK7258_AP_SMP_CPU1_TIMED_WAKE
int bk7258_ap_smp_btim_selftest(uint32_t timeout_ms);
#        endif
#        ifdef CONFIG_BK7258_AP_SMP_LIFECYCLE_QUIESCE
int bk7258_ap_smp_blcy_selftest(uint32_t timeout_ms);
#        endif
#      endif
#    endif
#  else
int bk7258_cpu2_probe_start(uint32_t timeout_ms);
int bk7258_cpu2_probe_stop(uint32_t timeout_ms);
#  endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_AMP_H */
