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

#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
#  define BK7258_CPU2_BOOT_STACK_SIZE    CONFIG_ARCH_INTERRUPTSTACK
#else
#  define BK7258_CPU2_BOOT_STACK_SIZE    0x00000400u
#endif

#define BK7258_CPU2_BOOT_STACK_TOP       BK7258_SHARED_RAM_BASE
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
#define BK7258_AP_DEFAULT_TIMEOUT_MS     3000u
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
  BK7258_AP_ERROR_CPU2_SMP_SCHEDULER
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

static_assert(BK7258_CP_FLASH_OFFSET + BK7258_CP_FLASH_SIZE ==
              BK7258_DATA_FLASH_OFFSET,
              "CP flash must end at the LittleFS boundary");
static_assert(BK7258_DATA_FLASH_OFFSET + BK7258_DATA_FLASH_SIZE ==
              BK7258_AP_FLASH_OFFSET,
              "AP flash must start after LittleFS");
static_assert(BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE ==
              BK7258_SHARED_RAM_BASE,
              "AP RAM must end at the shared page");
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
              BK7258_SHARED_RAM_SIZE,
              "AP SMP state exceeds the shared page");
static_assert(BK7258_CPU2_PROBE_STACK_BASE >= BK7258_AP_RAM_BASE,
              "CPU2 probe stack must remain in AP-owned RAM");
static_assert(BK7258_CPU2_PROBE_STACK_TOP == BK7258_SHARED_RAM_BASE,
              "CPU2 probe stack must end at the shared-page boundary");

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

#ifdef CONFIG_BK7258_AP_CONTROL
int bk7258_ap_control_initialize(void);
int bk7258_ap_start(uint32_t timeout_ms);
int bk7258_ap_stop(uint32_t timeout_ms);
int bk7258_ap_restart(uint32_t timeout_ms);
int bk7258_ap_ipi_test(uint32_t count, uint32_t timeout_ms);
void bk7258_ap_get_status(struct bk7258_ap_boot_state_s *status);
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
