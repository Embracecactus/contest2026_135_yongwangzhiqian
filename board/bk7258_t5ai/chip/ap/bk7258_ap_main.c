/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/bk7258_ap_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal AP NuttX init task for physical CPU1.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_peripherals.h>

#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif

#ifdef CONFIG_BK7258_RPTUN_MBOX
#  include <arch/chip/bk7258_rptun.h>
#  include "bk7258_rptun_mbox.h"
#endif
#ifdef CONFIG_BK7258_RPTUN
#  include "bk7258_rptun.h"
#endif
#ifdef CONFIG_BK7258_AP_SUPERVISOR
#  include "bk7258_ap_health.h"
#endif
#ifdef CONFIG_BK7258_BT_IPC
#  include <arch/chip/bk7258_bt_ipc.h>
#endif
#ifdef CONFIG_BK7258_BLE_GATT
#  include <arch/chip/bk7258_ble_gatt.h>
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
#  include <arch/chip/bk7258_wifi.h>
#endif

#include "arm_internal.h"
#include "bk7258_clockdiag.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SCB_VTOR             (*(volatile uint32_t *)0xe000ed08u)
#define BK7258_SYSTICK_CTRL         (*(volatile uint32_t *)0xe000e010u)
#define BK7258_SYSTICK_RELOAD       (*(volatile uint32_t *)0xe000e014u)
#define BK7258_SYSTICK_CURRENT      (*(volatile uint32_t *)0xe000e018u)
#define BK7258_SYSTICK_ENABLE       (1u << 0)
#define BK7258_SYSTICK_TICKINT      (1u << 1)
#define BK7258_SCB_CCR              (*(volatile uint32_t *)0xe000ed14u)
#define BK7258_SCB_CCR_DCACHE       (1u << 16)
#define BK7258_MPU_CTRL             (*(volatile uint32_t *)0xe000ed94u)
#define BK7258_MPU_RNR              (*(volatile uint32_t *)0xe000ed98u)
#define BK7258_MPU_RBAR             (*(volatile uint32_t *)0xe000ed9cu)
#define BK7258_MPU_RLAR             (*(volatile uint32_t *)0xe000eda0u)
#define BK7258_MPU_MAIR0            (*(volatile uint32_t *)0xe000edc0u)
#define BK7258_MPU_SRAM_REGION      15u
#define BK7258_MPU_SRAM_RBAR        0x2800001au
#define BK7258_MPU_SRAM_RLAR        0x3fffffe3u
#ifdef CONFIG_BK7258_PSRAM
#  define BK7258_MPU_PSRAM_REGION   6u
#  define BK7258_MPU_PSRAM_RBAR     0x60000002u
#  define BK7258_MPU_PSRAM_RLAR     0x63ffffe3u
#endif
#define BK7258_MPU_ATTR1_MASK       0x0000ff00u
#define BK7258_MPU_ATTR1_NOCACHE    0x00004400u
#define BK7258_MPU_CTRL_EXPECTED    0x7u
#ifdef CONFIG_BK7258_AP_SUPERVISOR
#  define BK7258_AP_HEARTBEAT_US \
    ((uint32_t)CONFIG_BK7258_AP_HEARTBEAT_PERIOD_MS * 1000u)
#else
#  define BK7258_AP_HEARTBEAT_US    100000u
#endif

#ifdef CONFIG_BK7258_RPTUN
#  define BK7258_AP_RPTUN_INIT_PRIORITY  226
static_assert(BK7258_AP_RPTUN_INIT_PRIORITY >
              CONFIG_BK7258_RPTUN_RX_PRIORITY,
              "AP RPTUN init coordinator must outrank RX worker");
static_assert(BK7258_AP_RPTUN_INIT_PRIORITY > CONFIG_RPTUN_PRIORITY,
              "AP RPTUN init coordinator must outrank RPTUN worker");
#endif

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
#  define BK7258_CPU2_EXPECTED_STATE \
    BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE
#  define BK7258_CPU2_EXPECTED_MASK  0x3u
#else
#  define BK7258_CPU2_EXPECTED_STATE \
    BK7258_CPU2_PROBE_STATE_SECONDARY_READY
#  define BK7258_CPU2_EXPECTED_MASK  0x1u
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_RPTUN_MBOX
static uint32_t bk7258_ap_mbox_receive(void)
{
  return bk7258_rptun_mbox_take_lifecycle();
}

static void bk7258_ap_mbox_send(uint32_t event)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  if (bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_LIFECYCLE,
                             state->generation, event) >= 0)
    {
      state->ap_to_cp_doorbells++;
    }

  __asm volatile ("dmb sy" ::: "memory");
}
#else
static inline volatile uint32_t *bk7258_ap_mbox(uint32_t base)
{
  return (volatile uint32_t *)(uintptr_t)base;
}

static void bk7258_ap_mbox_ack(volatile uint32_t *mbox)
{
  mbox[BK7258_MBOX_CLEAR_OFFSET / 4] = BK7258_MBOX_BOX0_BIT;
  __asm volatile ("dsb sy" ::: "memory");
  mbox[BK7258_MBOX_READY_OFFSET / 4] = 0;
  mbox[BK7258_MBOX_CLEAR_OFFSET / 4] = 0;
  __asm volatile ("dsb sy" ::: "memory");
}

static uint32_t bk7258_ap_mbox_receive(void)
{
  volatile uint32_t *mbox = bk7258_ap_mbox(BK7258_MBOX0_BASE);
  uint32_t event = BK7258_AP_EVENT_NONE;

  if ((mbox[BK7258_MBOX_READY_OFFSET / 4] &
       BK7258_MBOX_BOX0_BIT) != 0)
    {
      if (mbox[BK7258_MBOX_PARAM0_OFFSET / 4] ==
          BK7258_AP_DOORBELL_MAGIC)
        {
          event = mbox[BK7258_MBOX_PARAM1_OFFSET / 4];
        }

      bk7258_ap_mbox_ack(mbox);
    }

  return event;
}

static void bk7258_ap_mbox_send(uint32_t event)
{
  volatile uint32_t *mbox = bk7258_ap_mbox(BK7258_MBOX1_BASE);
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  if ((mbox[BK7258_MBOX_READY_OFFSET / 4] &
       BK7258_MBOX_BOX0_BIT) != 0)
    {
      bk7258_ap_mbox_ack(mbox);
    }

  mbox[BK7258_MBOX_SENDER_OFFSET / 4] = 1u << 1;
  mbox[BK7258_MBOX_RECEIVER_OFFSET / 4] = 1u << 0;
  mbox[BK7258_MBOX_PARAM0_OFFSET / 4] = BK7258_AP_DOORBELL_MAGIC;
  mbox[BK7258_MBOX_PARAM1_OFFSET / 4] = event;
  mbox[BK7258_MBOX_PARAM2_OFFSET / 4] = state->generation;
  mbox[BK7258_MBOX_PARAM3_OFFSET / 4] = state->state;
  __asm volatile ("dmb sy" ::: "memory");
  mbox[BK7258_MBOX_READY_OFFSET / 4] = BK7258_MBOX_BOX0_BIT;
  state->ap_to_cp_doorbells++;
  __asm volatile ("dsb sy; sev" ::: "memory");
}
#endif

static void bk7258_ap_publish_failure(uint32_t error)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  state->error = error;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_STATE_FAILED;
  __asm volatile ("dmb sy" ::: "memory");
  bk7258_ap_mbox_send(BK7258_AP_EVENT_FAILED);
}

static int bk7258_ap_validate_runtime(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  void *test;
  uint32_t mpu_rnr;
#ifdef CONFIG_BK7258_PSRAM
  uint32_t psram_rbar;
  uint32_t psram_rlar;
#endif
  uint32_t msp;

  __asm volatile ("mrs %0, msp" : "=r"(msp));

  state->runtime_vtor    = BK7258_SCB_VTOR;
  state->runtime_msp     = msp;
  state->clock_hz        = bk7258_clockdiag_current_cpu_hz();
  state->systick_ctrl    = BK7258_SYSTICK_CTRL;
  state->systick_reload  = BK7258_SYSTICK_RELOAD;
  state->systick_current = BK7258_SYSTICK_CURRENT;
  state->heap_start      = (uint32_t)g_idle_topstack;
  state->heap_end        = BK7258_AP_HEAP_END;
  state->ram_start       = BK7258_AP_RAM_BASE;
  state->ram_end         = BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE;
  state->flash_start     = BK7258_AP_FLASH_ADDR;
  state->flash_end       = BK7258_AP_FLASH_ADDR + BK7258_AP_FLASH_SIZE;

  /* Publish the cache/MPU handoff contract in the normal-boot reserved
   * words.  The fault handler intentionally reuses these words if a later
   * exception occurs, so a debugger can distinguish normal telemetry from
   * fault evidence through state->state/error.
   */

  mpu_rnr = BK7258_MPU_RNR;
  BK7258_MPU_RNR = BK7258_MPU_SRAM_REGION;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  state->reserved[0] = BK7258_SCB_CCR;
  state->reserved[1] = BK7258_MPU_CTRL;
  state->reserved[2] = BK7258_MPU_RBAR;
  state->reserved[3] = BK7258_MPU_RLAR;
#ifdef CONFIG_BK7258_PSRAM
  BK7258_MPU_RNR = BK7258_MPU_PSRAM_REGION;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  psram_rbar = BK7258_MPU_RBAR;
  psram_rlar = BK7258_MPU_RLAR;
#endif
  BK7258_MPU_RNR = mpu_rnr;
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  if (*(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR != 0 ||
      state->physical_core_id != 1)
    {
      return BK7258_AP_ERROR_BAD_CORE_ID;
    }

  if ((state->reserved[0] & BK7258_SCB_CCR_DCACHE) != 0 ||
      (state->reserved[1] & BK7258_MPU_CTRL_EXPECTED) !=
        BK7258_MPU_CTRL_EXPECTED ||
      state->reserved[2] != BK7258_MPU_SRAM_RBAR ||
      state->reserved[3] != BK7258_MPU_SRAM_RLAR ||
#ifdef CONFIG_BK7258_PSRAM
      psram_rbar != BK7258_MPU_PSRAM_RBAR ||
      psram_rlar != BK7258_MPU_PSRAM_RLAR ||
#endif
      (BK7258_MPU_MAIR0 & BK7258_MPU_ATTR1_MASK) !=
        BK7258_MPU_ATTR1_NOCACHE)
    {
      return BK7258_AP_ERROR_BAD_BOOT_STATE;
    }

  if (state->runtime_vtor < BK7258_AP_RAM_BASE ||
      state->runtime_vtor >= BK7258_SHARED_RAM_BASE ||
      (state->runtime_vtor & 0x1ffu) != 0)
    {
      return BK7258_AP_ERROR_BAD_VTOR;
    }

  if ((state->systick_ctrl &
       (BK7258_SYSTICK_ENABLE | BK7258_SYSTICK_TICKINT)) !=
      (BK7258_SYSTICK_ENABLE | BK7258_SYSTICK_TICKINT) ||
      state->systick_reload == 0)
    {
      return BK7258_AP_ERROR_BAD_SYSTICK;
    }

  test = kmm_malloc(64);
  if (test == NULL)
    {
      return BK7258_AP_ERROR_HEAP;
    }

  memset(test, 0xa5, 64);
  state->heap_test = (uint32_t)(uintptr_t)test;
  kmm_free(test);
  return BK7258_AP_ERROR_NONE;
}

#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
static int bk7258_ap_validate_secondary_bootstrap(void)
{
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
#ifdef CONFIG_BK7258_AP_IPI
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
#endif
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
#endif
  __asm volatile ("dmb sy" ::: "memory");

  if (cpu2->magic != BK7258_CPU2_PROBE_STATE_MAGIC ||
      cpu2->version != BK7258_CPU2_PROBE_STATE_VERSION ||
      cpu2->size != sizeof(struct bk7258_cpu2_probe_state_s) ||
      cpu2->generation != bk7258_ap_boot_state()->generation ||
      cpu2->state != BK7258_CPU2_EXPECTED_STATE ||
      cpu2->error != BK7258_CPU2_PROBE_ERROR_NONE ||
      cpu2->secondary_ready != 1 ||
      cpu2->online_mask != BK7258_CPU2_EXPECTED_MASK ||
      cpu2->local_core_id != 1 ||
      cpu2->physical_core_id != 2 ||
      cpu2->runtime_vtor != cpu2->vector ||
      cpu2->runtime_msp <= BK7258_CPU2_BOOT_STACK_BASE ||
      cpu2->runtime_msp > BK7258_CPU2_BOOT_STACK_TOP ||
      cpu2->idle_stack_base < BK7258_AP_RAM_BASE ||
      cpu2->idle_stack_base >= cpu2->idle_stack_top ||
      cpu2->idle_stack_top > BK7258_CPU2_BOOT_STACK_BASE)
    {
      return BK7258_AP_ERROR_CPU2_SMP_BOOTSTRAP;
    }

#ifdef CONFIG_BK7258_AP_IPI
  if (ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != bk7258_ap_boot_state()->generation ||
      ipi->state != BK7258_AP_IPI_STATE_READY ||
      ipi->error != BK7258_AP_IPI_ERROR_NONE)
    {
      return BK7258_AP_ERROR_CPU2_IPI;
    }
#endif

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  if (smp->magic != BK7258_AP_SMP_STATE_MAGIC ||
      smp->version != BK7258_AP_SMP_STATE_VERSION ||
      smp->size != sizeof(struct bk7258_ap_smp_state_s) ||
      smp->generation != bk7258_ap_boot_state()->generation ||
      smp->state != BK7258_AP_SMP_STATE_ONLINE ||
      smp->error != BK7258_AP_SMP_ERROR_NONE ||
      smp->online_mask != BK7258_CPU2_EXPECTED_MASK)
    {
      return BK7258_AP_ERROR_CPU2_SMP_SCHEDULER;
    }
#endif

  return BK7258_AP_ERROR_NONE;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_main(int argc, char *argv[])
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
#ifdef CONFIG_BK7258_RPTUN
  volatile struct bk7258_rptun_control_s *rptun =
    bk7258_rptun_control();
  struct sched_param saved_priority;
  struct sched_param startup_priority;
  bool priority_raised = false;
#endif
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  volatile struct bk7258_ap_smp_state_s *smp = bk7258_ap_smp_state();
#endif
#ifdef CONFIG_BK7258_BLE_GATT
  struct bk7258_ble_gatt_stats_s ble_gatt;
#endif
#ifdef CONFIG_BK7258_PSRAM_TEST
  struct bk7258_psram_test_result_s psram_test;
#endif
  uint32_t event;
  int error;
  int ret;

  (void)argc;
  (void)argv;

  if (state->magic != BK7258_AP_BOOT_STATE_MAGIC ||
      state->version != BK7258_AP_BOOT_STATE_VERSION ||
      state->size != sizeof(struct bk7258_ap_boot_state_s))
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

  error = bk7258_ap_validate_runtime();
  if (error != BK7258_AP_ERROR_NONE)
    {
      bk7258_ap_publish_failure((uint32_t)error);
      goto parked;
    }

#ifdef CONFIG_BK7258_PSRAM
  ret = bk7258_psram_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_PSRAM);
      goto parked;
    }

  state->reserved[BK7258_PSRAM_AP_RESERVED_HEAP] =
    BK7258_PSRAM_AP_HEAP_BASE;
  state->reserved[BK7258_PSRAM_AP_RESERVED_GATE] =
    BK7258_PSRAM_AP_HEAP_READY;
#ifdef CONFIG_BK7258_PSRAM_TEST
  memset(&psram_test, 0, sizeof(psram_test));
  state->reserved[BK7258_PSRAM_AP_RESERVED_RESULT] =
    (uint32_t)(uintptr_t)&psram_test;
  state->reserved[BK7258_PSRAM_AP_RESERVED_MAGIC] =
    BK7258_PSRAM_AP_RESULT_READY;
#endif
  __asm volatile ("dmb sy" ::: "memory");
#endif

#ifdef CONFIG_BK7258_RPTUN
  /* kthread_create() activates the new task before returning.  Both the
   * mailbox RX worker and stock RPTUN worker intentionally outrank normal
   * applications, but they must not outrank the coordinator which is still
   * constructing and pinning them.  Otherwise a cold-start context switch
   * can leave this init path inside kthread_create() indefinitely.  After
   * READY, N10 keeps this primary management/heartbeat loop at its reserved
   * supervisor priority; profiles without N10 restore the init priority.
   */

  ret = sched_getparam(0, &saved_priority);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

  startup_priority = saved_priority;
  startup_priority.sched_priority = BK7258_AP_RPTUN_INIT_PRIORITY;
  ret = sched_setparam(0, &startup_priority);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

  priority_raised = true;
#endif

#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
  error = bk7258_ap_validate_secondary_bootstrap();
  if (error != BK7258_AP_ERROR_NONE)
    {
      bk7258_ap_publish_failure((uint32_t)error);
      goto parked;
    }
#else
  ret = bk7258_cpu2_probe_start(BK7258_CPU2_PROBE_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_PROBE);
      goto parked;
    }
#endif

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  ret = bk7258_ap_smp_scheduler_selftest(
    BK7258_AP_SMP_DEFAULT_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_SMP_SCHEDULER);
      goto parked;
    }
#endif

#ifdef CONFIG_BK7258_AP_SMP_CPU1_AFFINITY
#  ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
  ret = bk7258_ap_smp_affinity_selftest(
    BK7258_AP_SEM_WAKE_LOOP_TIMEOUT_MS);
#  elif defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE)
  ret = bk7258_ap_smp_affinity_selftest(
    BK7258_AP_SEM_WAKE_TIMEOUT_MS);
#  else
  ret = bk7258_ap_smp_affinity_selftest(
    BK7258_AP_AFFINITY_TIMEOUT_MS);
#  endif
  if (ret < 0)
    {
#ifdef CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_SEM_WAKE_LOOP);
#elif defined(CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE)
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_SEM_WAKE);
#else
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_AFFINITY);
#endif
      goto parked;
    }
#endif

#ifdef CONFIG_BK7258_AP_SMP_BIDIR_PINGPONG
  ret = bk7258_ap_smp_bp2p_selftest(BK7258_AP_ADV_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_BP2P);
      goto parked;
    }
#elif defined(CONFIG_BK7258_AP_SMP_CPU1_DUALTASK)
  ret = bk7258_ap_smp_bdul_selftest(BK7258_AP_ADV_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_BDUL);
      goto parked;
    }
#elif defined(CONFIG_BK7258_AP_SMP_CONTROLLED_MIGRATION)
  ret = bk7258_ap_smp_bmig_selftest(BK7258_AP_ADV_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_BMIG);
      goto parked;
    }
#elif defined(CONFIG_BK7258_AP_SMP_CPU1_TIMED_WAKE)
  ret = bk7258_ap_smp_btim_selftest(BK7258_AP_ADV_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_BTIM);
      goto parked;
    }
#elif defined(CONFIG_BK7258_AP_SMP_LIFECYCLE_QUIESCE)
  ret = bk7258_ap_smp_blcy_selftest(BK7258_AP_ADV_TIMEOUT_MS);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_CPU2_BLCY);
      goto parked;
    }
#endif

#ifdef CONFIG_BK7258_PSRAM_TEST
  ret = bk7258_psram_heap_test(CONFIG_BK7258_PSRAM_TEST_ITERATIONS,
                               true, &psram_test);
  if (ret < 0 || psram_test.status < 0 ||
      psram_test.completed[0] != CONFIG_BK7258_PSRAM_TEST_ITERATIONS ||
      psram_test.completed[1] != CONFIG_BK7258_PSRAM_TEST_ITERATIONS ||
      psram_test.observed_cpu[0] != 0u ||
      psram_test.observed_cpu[1] != 1u)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_PSRAM);
      goto parked;
    }

  state->reserved[BK7258_PSRAM_AP_RESERVED_GATE] =
    BK7258_PSRAM_AP_TEST_PASSED;
  __asm volatile ("dmb sy" ::: "memory");
#endif

  /* Publish a second, independently scheduled liveness source only after all
   * N8 SMP gates have passed.  The task is permanent for this AP generation
   * and pinned to logical CPU1; a failed first increment is a startup failure,
   * not a degraded READY state.
   */

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  ret = bk7258_ap_health_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_SUPERVISOR);
      goto parked;
    }
#endif

  /* Keep AP-local N8 validation ahead of logical transport ownership so its
   * zero-length SMP IPI gates run without RPMsg traffic.  The SDK physical
   * MBOX0 driver was already initialized by the AP SMP bootstrap.  The
   * temporarily elevated coordinator priority above is what makes the later
   * synchronous worker creation deterministic; changing the SDK/NuttX source
   * or moving logical transport ahead of the N8 gates is unnecessary.
   */

#ifdef CONFIG_BK7258_RPTUN_MBOX
#ifdef CONFIG_BK7258_RPTUN
  __atomic_fetch_or((uint32_t *)(uintptr_t)&rptun->flags,
                    BK7258_RPTUN_FLAG_AP_MBOX_ENTER, __ATOMIC_RELEASE);
#endif
  ret = bk7258_rptun_mbox_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

#ifdef CONFIG_BK7258_RPTUN
  __atomic_fetch_or((uint32_t *)(uintptr_t)&rptun->flags,
                    BK7258_RPTUN_FLAG_AP_MBOX_READY, __ATOMIC_RELEASE);
#endif
#endif

#ifdef CONFIG_BK7258_RPTUN
  __atomic_fetch_or((uint32_t *)(uintptr_t)&rptun->flags,
                    BK7258_RPTUN_FLAG_AP_RPTUN_ENTER, __ATOMIC_RELEASE);
  ret = bk7258_rptun_initialize(state->generation);
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BAD_BOOT_STATE);
      goto parked;
    }

#ifdef CONFIG_BK7258_RPMSGFS
  ret = bk7258_rpmsgfs_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_RPMSGFS);
      goto parked;
    }
#endif

  __atomic_fetch_or((uint32_t *)(uintptr_t)&rptun->flags,
                    BK7258_RPTUN_FLAG_AP_RPTUN_READY, __ATOMIC_RELEASE);
#endif

#ifdef CONFIG_BK7258_WIFI_VNET
  ret = bk7258_wifi_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_WIFI);
      goto parked;
    }

  ret = bk7258_wifi_control_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_WIFI);
      goto parked;
    }
#endif

#ifdef CONFIG_BK7258_BT_IPC
  ret = bk7258_bt_hci_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BLUETOOTH);
      goto parked;
    }
#endif

#ifdef CONFIG_BK7258_BLE_GATT
  ret = bk7258_ble_gatt_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BLUETOOTH);
      goto parked;
    }

  ret = bk7258_ble_gatt_get_stats(&ble_gatt);
  if (ret < 0 ||
      ble_gatt.state != BK7258_BLE_GATT_STATE_ADVERTISING ||
      ble_gatt.worker_cpu != 0u)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_BLUETOOTH);
      goto parked;
    }
#endif

  /* Publish AP-owned NuttX character devices only after the transport and
   * shared radio services are ready.  Registration itself remains lazy:
   * drivers such as I2C do not touch hardware until their first open.
   */

  ret = bk7258_peripherals_initialize();
  if (ret < 0)
    {
      bk7258_ap_publish_failure(BK7258_AP_ERROR_PERIPHERALS);
      goto parked;
    }

  state->error      = BK7258_AP_ERROR_NONE;
  state->last_event = BK7258_AP_EVENT_READY;
  state->state      = BK7258_AP_STATE_READY;
#ifdef CONFIG_BK7258_RPTUN
  rptun->ap_epoch   = state->generation;
  __atomic_fetch_or((uint32_t *)(uintptr_t)&rptun->flags,
                    BK7258_RPTUN_FLAG_AP_READY, __ATOMIC_RELEASE);
#endif
  __asm volatile ("dmb sy" ::: "memory");
  bk7258_ap_mbox_send(BK7258_AP_EVENT_READY);

#ifdef CONFIG_BK7258_RPTUN
  if (priority_raised)
    {
#ifdef CONFIG_BK7258_AP_SUPERVISOR
      startup_priority = saved_priority;
      startup_priority.sched_priority =
        CONFIG_BK7258_AP_SUPERVISOR_PRIORITY;
      ret = sched_setparam(0, &startup_priority);
      if (ret < 0)
        {
          bk7258_ap_publish_failure(BK7258_AP_ERROR_SUPERVISOR);
          goto parked;
        }
#else
      (void)sched_setparam(0, &saved_priority);
#endif
    }
#endif

  for (; ; )
    {
      event = bk7258_ap_mbox_receive();
      if (event != BK7258_AP_EVENT_NONE)
        {
          state->last_event = event;
        }

      if (event == BK7258_AP_EVENT_STOP ||
          state->command == BK7258_AP_COMMAND_STOP)
        {
          state->state = BK7258_AP_STATE_STOPPING;
          __asm volatile ("dmb sy" ::: "memory");
#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
          ret = bk7258_ap_smp_secondary_stop(
            BK7258_CPU2_PROBE_STOP_TIMEOUT_MS);
#else
          ret = bk7258_cpu2_probe_stop(
            BK7258_CPU2_PROBE_STOP_TIMEOUT_MS);
#endif
          if (ret < 0)
            {
#ifdef CONFIG_BK7258_AP_SMP_BOOTSTRAP
              state->error = BK7258_AP_ERROR_CPU2_SMP_BOOTSTRAP;
#else
              state->error = BK7258_AP_ERROR_CPU2_PROBE;
#endif
              state->state = BK7258_AP_STATE_FAILED;
              state->last_event = BK7258_AP_EVENT_FAILED;
              bk7258_ap_mbox_send(BK7258_AP_EVENT_FAILED);
              break;
            }

          state->state = BK7258_AP_STATE_STOPPED;
          state->last_event = BK7258_AP_EVENT_STOPPED;
          bk7258_ap_mbox_send(BK7258_AP_EVENT_STOPPED);
          break;
        }

#ifdef CONFIG_BK7258_AP_IPI
      if (event == BK7258_AP_EVENT_IPI_TEST ||
          state->command == BK7258_AP_COMMAND_IPI_TEST)
        {
          volatile struct bk7258_ap_ipi_state_s *ipi =
            bk7258_ap_ipi_state();

          state->command = BK7258_AP_COMMAND_NONE;
          __asm volatile ("dmb sy" ::: "memory");
          ret = bk7258_ap_ipi_selftest(ipi->requested_count,
                                       ipi->timeout_ms);
          if (ret < 0)
            {
              state->error = BK7258_AP_ERROR_CPU2_IPI;
              state->last_event = BK7258_AP_EVENT_IPI_TEST_FAILED;
              bk7258_ap_mbox_send(BK7258_AP_EVENT_IPI_TEST_FAILED);
            }
          else
            {
              state->error = BK7258_AP_ERROR_NONE;
              state->last_event = BK7258_AP_EVENT_IPI_TEST_PASSED;
              bk7258_ap_mbox_send(BK7258_AP_EVENT_IPI_TEST_PASSED);
            }
        }
#endif

      state->heartbeat++;
#if defined(CONFIG_BK7258_AP_SUPERVISOR) && defined(CONFIG_BK7258_RPTUN)
      if (rptun->generation == state->generation)
        {
          rptun->ap_epoch = state->generation;
          __atomic_fetch_add(
            (uint32_t *)(uintptr_t)&rptun->ap_heartbeat, 1u,
            __ATOMIC_RELEASE);
        }
#endif
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
      smp->sleep_enter_count++;
#endif
      __asm volatile ("dmb sy; sev" ::: "memory");
      nxsig_usleep(BK7258_AP_HEARTBEAT_US);
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
      smp->sleep_return_count++;
      __asm volatile ("dmb sy" ::: "memory");
#endif
    }

parked:
  __asm volatile ("cpsid i; dsb sy; isb sy" ::: "memory");
  for (; ; )
    {
      __asm volatile ("wfe");
    }

  return 0;
}
