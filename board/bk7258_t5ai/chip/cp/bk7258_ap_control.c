/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_ap_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CPU0 control wrapper for the physical CPU1 AP NuttX image.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>

#if defined(CONFIG_BK7258_BT_IPC) && defined(CONFIG_BK7258_RPTUN_MBOX)
#  include <arch/chip/bk7258_bt_ipc.h>
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
#  include <arch/chip/bk7258_wifi.h>
#endif

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

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* These routines are provided by the pinned Beken CP SDK archives. */

#define BK7258_SDK_PM_POWER_MODULE_CPU1 17u
#define BK7258_SDK_PM_POWER_ON          0u

extern int bk_pm_module_vote_power_ctrl(unsigned int module,
                                        unsigned int power_state);
extern void sys_drv_set_cpu1_pwr_dw(uint32_t is_pwr_down);
extern void sys_drv_set_cpu1_rxevt_sel(uint32_t value);
extern void sys_drv_set_cpu1_boot_address_offset(uint32_t address_offset);
extern void sys_drv_set_cpu1_reset(uint32_t reset_value);
extern void sys_drv_set_cpu2_pwr_dw(uint32_t is_pwr_down);
extern void sys_drv_set_cpu2_rxevt_sel(uint32_t value);
extern void sys_drv_set_cpu2_boot_address_offset(uint32_t address_offset);
extern void sys_drv_set_cpu2_reset(uint32_t reset_value);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Singleton SoC CPU1 control state.  The mutex serializes SYS register and
 * shared-state transitions issued by concurrent NSH callers.
 */

static mutex_t g_bk7258_ap_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_ap_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_cpu1_sdk_stop(void)
{
  /* Match SDK reset_cpu1_core(0, 0), which is also the hardware part of
   * stop_cpu1_core().  Keep the SDK's power-up-before-reset ordering: the
   * pwr_dw bit is not a reliable substitute for asserting the CPU reset.
   */

  sys_drv_set_cpu1_pwr_dw(0);
  sys_drv_set_cpu1_rxevt_sel(1);
  sys_drv_set_cpu1_boot_address_offset(0);
  sys_drv_set_cpu1_reset(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void bk7258_cpu2_sdk_stop(void)
{
  /* Match SDK reset_cpu2_core(0, 0), which is also stop_cpu2_core().  Use
   * the SDK system-driver wrappers so their register critical sections and
   * bitfield ownership remain authoritative.
   */

  sys_drv_set_cpu2_pwr_dw(0);
  sys_drv_set_cpu2_rxevt_sel(1);
  sys_drv_set_cpu2_boot_address_offset(0);
  sys_drv_set_cpu2_reset(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

#ifdef CONFIG_BK7258_RPTUN_MBOX
static int bk7258_ap_mbox_initialize(void)
{
  return bk7258_rptun_mbox_initialize();
}

static void bk7258_ap_mbox_send(uint32_t event)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  if (bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_LIFECYCLE,
                             state->generation, event) >= 0)
    {
      state->cp_to_ap_doorbells++;
    }

  __asm volatile ("dmb sy" ::: "memory");
}

static uint32_t bk7258_ap_mbox_receive(void)
{
  return bk7258_rptun_mbox_take_lifecycle();
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

static int bk7258_ap_mbox_initialize(void)
{
  volatile uint32_t *to_ap = bk7258_ap_mbox(BK7258_MBOX0_BASE);
  volatile uint32_t *to_cp = bk7258_ap_mbox(BK7258_MBOX1_BASE);

  to_ap[BK7258_MBOX_CLKRST_OFFSET / 4] = 1u;
  to_cp[BK7258_MBOX_CLKRST_OFFSET / 4] = 1u;
  to_ap[BK7258_MBOX_SENDER_OFFSET / 4] = 1u << 0;
  to_ap[BK7258_MBOX_RECEIVER_OFFSET / 4] = 1u << 1;
  to_cp[BK7258_MBOX_SENDER_OFFSET / 4] = 1u << 1;
  to_cp[BK7258_MBOX_RECEIVER_OFFSET / 4] = 1u << 0;

  bk7258_ap_mbox_ack(to_ap);
  bk7258_ap_mbox_ack(to_cp);
  return OK;
}

static void bk7258_ap_mbox_send(uint32_t event)
{
  volatile uint32_t *mbox = bk7258_ap_mbox(BK7258_MBOX0_BASE);
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  if ((mbox[BK7258_MBOX_READY_OFFSET / 4] &
       BK7258_MBOX_BOX0_BIT) != 0)
    {
      bk7258_ap_mbox_ack(mbox);
    }

  mbox[BK7258_MBOX_PARAM0_OFFSET / 4] = BK7258_AP_DOORBELL_MAGIC;
  mbox[BK7258_MBOX_PARAM1_OFFSET / 4] = event;
  mbox[BK7258_MBOX_PARAM2_OFFSET / 4] = state->generation;
  mbox[BK7258_MBOX_PARAM3_OFFSET / 4] = state->command;
  __asm volatile ("dmb sy" ::: "memory");
  mbox[BK7258_MBOX_READY_OFFSET / 4] = BK7258_MBOX_BOX0_BIT;
  state->cp_to_ap_doorbells++;
  __asm volatile ("dsb sy; sev" ::: "memory");
}

static uint32_t bk7258_ap_mbox_receive(void)
{
  volatile uint32_t *mbox = bk7258_ap_mbox(BK7258_MBOX1_BASE);
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  uint32_t event = BK7258_AP_EVENT_NONE;

  if ((mbox[BK7258_MBOX_READY_OFFSET / 4] &
       BK7258_MBOX_BOX0_BIT) != 0)
    {
      if (mbox[BK7258_MBOX_PARAM0_OFFSET / 4] ==
          BK7258_AP_DOORBELL_MAGIC &&
          mbox[BK7258_MBOX_PARAM2_OFFSET / 4] == state->generation)
        {
          event = mbox[BK7258_MBOX_PARAM1_OFFSET / 4];
        }

      bk7258_ap_mbox_ack(mbox);
    }

  return event;
}
#endif

static void bk7258_ap_state_prepare(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
#ifdef CONFIG_BK7258_RPTUN_MBOX
  volatile struct bk7258_rptun_control_s *rptun = bk7258_rptun_control();
#endif
  uint32_t generation = 1;

  if (state->magic == BK7258_AP_BOOT_STATE_MAGIC &&
      state->version == BK7258_AP_BOOT_STATE_VERSION)
    {
      generation = state->generation + 1;
    }

  memset((void *)(uintptr_t)state, 0,
         sizeof(struct bk7258_ap_boot_state_s));
  bk7258_ap_fault_state()->magic = 0;
  bk7258_cpu2_probe_state()->magic = 0;
  bk7258_ap_ipi_state()->magic = 0;
  bk7258_ap_smp_state()->magic = 0;
  bk7258_ap_affinity_state()->magic = 0;
  bk7258_ap_sem_wake_state()->magic = 0;
  bk7258_ap_sem_wake_loop_state()->magic = 0;
  bk7258_ap_bp2p_state()->magic = 0;
  bk7258_ap_bdul_state()->magic = 0;
  bk7258_ap_bmig_state()->magic = 0;
  bk7258_ap_btim_state()->magic = 0;
  bk7258_ap_blcy_state()->magic = 0;
#ifdef CONFIG_BK7258_RPTUN_MBOX
  memset((void *)(uintptr_t)BK7258_RPTUN_SHMEM_BASE, 0,
         BK7258_RPTUN_SHMEM_SIZE);
  rptun->version = BK7258_RPTUN_CONTROL_VERSION;
  rptun->size = sizeof(*rptun);
  rptun->generation = generation;
  rptun->state = BK7258_RPTUN_STATE_OFFLINE;
  rptun->cp_epoch = generation;
  __asm volatile ("dmb sy" ::: "memory");
  rptun->magic = BK7258_RPTUN_CONTROL_MAGIC;
#endif
  state->magic       = BK7258_AP_BOOT_STATE_MAGIC;
  state->version     = BK7258_AP_BOOT_STATE_VERSION;
  state->size        = sizeof(struct bk7258_ap_boot_state_s);
  state->generation  = generation;
  state->command     = BK7258_AP_COMMAND_START;
  state->state       = BK7258_AP_STATE_STARTING;
  state->ram_start   = BK7258_AP_RAM_BASE;
  state->ram_end     = BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE;
  state->flash_start = BK7258_AP_FLASH_ADDR;
  state->flash_end   = BK7258_AP_FLASH_ADDR + BK7258_AP_FLASH_SIZE;
  __asm volatile ("dmb sy" ::: "memory");
}

static bool bk7258_ap_scheduler_online(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();

  __asm volatile ("dmb sy" ::: "memory");
  return cpu2->magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
         cpu2->version == BK7258_CPU2_PROBE_STATE_VERSION &&
         cpu2->size == sizeof(struct bk7258_cpu2_probe_state_s) &&
         cpu2->generation == state->generation &&
         cpu2->state == BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE &&
         cpu2->online_mask == 0x3u;
}

#ifdef CONFIG_BK7258_WIFI_VNET
static bool bk7258_ap_wifi_generation_started(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  __asm volatile ("dmb sy" ::: "memory");
  return bk7258_wifi_controller_active() &&
         state->magic == BK7258_AP_BOOT_STATE_MAGIC &&
         state->version == BK7258_AP_BOOT_STATE_VERSION &&
         state->size == sizeof(*state) && state->generation != 0;
}
#endif

#if defined(CONFIG_BK7258_BT_IPC) && defined(CONFIG_BK7258_RPTUN_MBOX)
static int bk7258_ap_bt_controller_service(uint32_t wanted)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t flags;

  if (wanted != BK7258_AP_STATE_READY)
    {
      return OK;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (control->magic != BK7258_RPTUN_CONTROL_MAGIC ||
      control->version != BK7258_RPTUN_CONTROL_VERSION ||
      control->size != sizeof(*control) ||
      control->generation != state->generation)
    {
      return -EPROTO;
    }

  flags = __atomic_load_n(&control->flags, __ATOMIC_ACQUIRE);
  if ((flags & BK7258_RPTUN_FLAG_AP_BT_IPC_READY) == 0 ||
      (flags & BK7258_RPTUN_FLAG_CP_BT_READY) != 0)
    {
      return OK;
    }

  /* CP bt_ipc was initialized before AP release.  Once AP publishes its
   * endpoint, acknowledge that both mailbox workers are ready and let AP
   * issue the official vendor-init command.  The CP SDK bt_ipc worker must
   * remain the sole caller that starts the Controller; pre-starting it here
   * creates early HCI/free traffic outside the SDK's ownership sequence.
   */

  __atomic_fetch_or(&control->flags, BK7258_RPTUN_FLAG_CP_BT_READY,
                    __ATOMIC_RELEASE);
  return OK;
}
#endif

static int bk7258_ap_wait(uint32_t wanted, uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  clock_t start;
  clock_t timeout_ticks;
  uint32_t event;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_DEFAULT_TIMEOUT_MS;
    }

  timeout_ticks = MSEC2TICK(timeout_ms);
  start = clock_systime_ticks();

  for (;;)
    {
      event = bk7258_ap_mbox_receive();
      if (event != BK7258_AP_EVENT_NONE)
        {
          state->last_event = event;
        }

#if defined(CONFIG_BK7258_BT_IPC) && defined(CONFIG_BK7258_RPTUN_MBOX)
      {
        int ret = bk7258_ap_bt_controller_service(wanted);

        if (ret < 0)
          {
            return ret;
          }
      }
#endif

      __asm volatile ("dmb sy" ::: "memory");
      if (state->state == wanted)
        {
          return OK;
        }

      if (state->state == BK7258_AP_STATE_FAILED)
        {
          return -EIO;
        }

      if ((clock_t)(clock_systime_ticks() - start) >= timeout_ticks)
        {
          break;
        }

      /* Yield to the CP idle thread while waiting.  A pure busy-poll at the
       * NSH task priority prevents the board task watchdog from being fed and
       * can reset CP before the 15-second AP timeout expires.  This mirrors
       * the SDK's scheduled millisecond-delay model while the absolute tick
       * comparison above remains the authoritative timeout.
       */

      nxsig_usleep(1000);
    }

  return -ETIMEDOUT;
}

#ifdef CONFIG_BK7258_RPTUN
static int bk7258_rptun_wait_quiesce_ready(uint32_t generation,
                                          uint32_t timeout_ms)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  clock_t start;
  clock_t timeout_ticks;
  uint32_t flags;
  uint32_t state;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_DEFAULT_TIMEOUT_MS;
    }

  timeout_ticks = MSEC2TICK(timeout_ms);
  start = clock_systime_ticks();

  for (;;)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (control->magic != BK7258_RPTUN_CONTROL_MAGIC ||
          control->version != BK7258_RPTUN_CONTROL_VERSION ||
          control->size != sizeof(*control) ||
          control->generation != generation)
        {
          return -EPROTO;
        }

      state = control->state;
      flags = control->flags;
      if (state == BK7258_RPTUN_STATE_CONNECTED ||
          state == BK7258_RPTUN_STATE_OFFLINE ||
          (state == BK7258_RPTUN_STATE_FAULTED &&
           (flags & BK7258_RPTUN_FLAG_CONNECTED_ONCE) != 0))
        {
          return OK;
        }

      if (state == BK7258_RPTUN_STATE_FAULTED)
        {
          return -EIO;
        }

      if (state != BK7258_RPTUN_STATE_PREPARING &&
          state != BK7258_RPTUN_STATE_TABLE_READY &&
          state != BK7258_RPTUN_STATE_CONNECTING)
        {
          return -EBUSY;
        }

      if ((clock_t)(clock_systime_ticks() - start) >= timeout_ticks)
        {
          /* Do not return -ETIMEDOUT: bk7258_ap_restart() deliberately
           * continues after an AP stop timeout, while this condition means
           * the CP RPTUN vdev is still unsafe to destroy or replace.
           */

          return -EBUSY;
        }

      nxsig_usleep(1000);
    }
}
#endif

static int bk7258_ap_start_locked(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  int ret;

#ifdef CONFIG_BK7258_PSRAM
  /* AP is a direct-memory consumer, never a PSRAM hardware owner.  Refuse
   * manual as well as automatic release unless the CP driver and heap gate
   * completed successfully for this SoC boot.
   */

  if (!bk7258_psram_ready())
    {
      return -ENODEV;
    }
#endif

  if (state->magic == BK7258_AP_BOOT_STATE_MAGIC &&
      (state->state == BK7258_AP_STATE_READY ||
       state->state == BK7258_AP_STATE_STARTING))
    {
      return -EBUSY;
    }

  /* Stop the AP SMP secondary before its primary.  Resetting physical CPU1
   * first would leave scheduler-online physical CPU2 running without the
   * primary CPU, a state NuttX SMP does not support.
   */

  bk7258_cpu2_sdk_stop();
  bk7258_cpu1_sdk_stop();
  up_mdelay(BK7258_AP_RESTART_DELAY_MS);

  ret = bk7258_ap_mbox_initialize();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_RPTUN
  /* Stop the persistent CP-side RPTUN worker before state_prepare() clears
   * the shared table/carveout.  This is a no-op on the initial boot.
   */

  ret = bk7258_rptun_quiesce();
  if (ret < 0)
    {
      return ret;
    }
#endif

  bk7258_ap_state_prepare();

#ifdef CONFIG_BK7258_RPTUN
  ret = bk7258_rptun_initialize(state->generation);
  if (ret < 0)
    {
      return ret;
    }
#endif

  /* Release order matches the BK7258 CP SDK.  The reset+power-down pulse
   * above is an intentional cold-start normalization step: unlike the SDK's
   * one-shot boot path, this controller must also survive downloader reset
   * residue and repeated AP restart attempts with private caches populated.
   * The official PM vote is not equivalent to pwr_dw=0: it also clears the
   * CPU1 halt bit, restores its clock, and waits for that clock to settle.
   */

  ret = bk_pm_module_vote_power_ctrl(BK7258_SDK_PM_POWER_MODULE_CPU1,
                                     BK7258_SDK_PM_POWER_ON);
  if (ret != 0)
    {
      return -EIO;
    }

  sys_drv_set_cpu1_pwr_dw(0);
  sys_drv_set_cpu1_rxevt_sel(1);
  /* A MCUboot-signed AP slot starts with its 0x200-byte image header.  CPU1
   * has no MCUboot parser of its own: BL2 validated the pair before it
   * launched CP, and CP must release CPU1 at the AP vector table itself.
   */

  sys_drv_set_cpu1_boot_address_offset(BK7258_AP_VECTOR_ADDR >> 8);
  __asm volatile ("dsb sy; isb sy" ::: "memory");

  sys_drv_set_cpu1_reset(1);
  __asm volatile ("dsb sy; sev" ::: "memory");

  ret = bk7258_ap_wait(BK7258_AP_STATE_READY, timeout_ms);
  if (ret < 0)
    {
      bk7258_cpu2_sdk_stop();
      bk7258_cpu1_sdk_stop();
      up_mdelay(BK7258_AP_RESTART_DELAY_MS);

      if (state->state != BK7258_AP_STATE_FAILED)
        {
          state->error = BK7258_AP_ERROR_TIMEOUT;
        }

      state->command = BK7258_AP_COMMAND_NONE;
      state->state = BK7258_AP_STATE_FAILED;
      state->last_event = BK7258_AP_EVENT_FAILED;
      __asm volatile ("dmb sy" ::: "memory");
    }

  return ret;
}

static int bk7258_ap_stop_locked(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  bool scheduler_online = bk7258_ap_scheduler_online();
  int ret = OK;

#ifdef CONFIG_BK7258_RPTUN
  /* NuttX starts RPTUN in a worker, so AP READY can precede completion of
   * rpmsg_virtio_probe().  Wait for the Name Service connection proof before
   * asking NuttX to remove the vdev.  Otherwise a rapid cycle can tear down a
   * partially probed vdev and fault inside rpmsg_virtio_remove().
   */

  if (state->magic == BK7258_AP_BOOT_STATE_MAGIC &&
      state->version == BK7258_AP_BOOT_STATE_VERSION &&
      state->size == sizeof(*state) &&
      state->state != BK7258_AP_STATE_OFF &&
      state->state != BK7258_AP_STATE_STOPPED)
    {
      ret = bk7258_rptun_wait_quiesce_ready(state->generation, timeout_ms);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bk7258_rptun_quiesce();
  if (ret < 0)
    {
      return ret;
    }
#endif

  /* NuttX SMP has no supported logical-CPU hot-unplug sequence here.  When
   * CPU2 is scheduler-online, skip the in-band STOP command and reset both AP
   * physical cores after the transport is quiesced.  Non-SMP/probe builds
   * retain the original graceful handshake.
   */

  if (!scheduler_online &&
      (state->state == BK7258_AP_STATE_READY ||
       state->state == BK7258_AP_STATE_STARTING))
    {
      state->command = BK7258_AP_COMMAND_STOP;
      __asm volatile ("dmb sy" ::: "memory");
      bk7258_ap_mbox_send(BK7258_AP_EVENT_STOP);
      ret = bk7258_ap_wait(BK7258_AP_STATE_STOPPED, timeout_ms);
    }

  /* A timeout still ends in a deterministic forced stop of both AP cores.
   * Stop the SMP secondary first so it cannot execute after its primary is
   * held in reset.
   */

  bk7258_cpu2_sdk_stop();
  bk7258_cpu1_sdk_stop();
  up_mdelay(BK7258_AP_RESTART_DELAY_MS);

  if (ret < 0 && state->state != BK7258_AP_STATE_FAILED)
    {
      state->error = BK7258_AP_ERROR_TIMEOUT;
    }

  state->command = BK7258_AP_COMMAND_NONE;
  state->state = BK7258_AP_STATE_STOPPED;
  state->last_event = BK7258_AP_EVENT_STOPPED;
  __asm volatile ("dmb sy" ::: "memory");
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_control_initialize(void)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_ap_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_bk7258_ap_initialized)
    {
      ret = bk7258_ap_mbox_initialize();
      if (ret >= 0)
        {
          g_bk7258_ap_initialized = true;
        }
    }

  nxmutex_unlock(&g_bk7258_ap_lock);
  return ret;
}

int bk7258_ap_start(uint32_t timeout_ms)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_ap_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_bk7258_ap_initialized)
    {
      ret = bk7258_ap_mbox_initialize();
      if (ret < 0)
        {
          nxmutex_unlock(&g_bk7258_ap_lock);
          return ret;
        }

      g_bk7258_ap_initialized = true;
    }

#ifdef CONFIG_BK7258_WIFI_VNET
  if (bk7258_ap_wifi_generation_started())
    {
      nxmutex_unlock(&g_bk7258_ap_lock);
      return -EBUSY;
    }
#endif

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  bk7258_ap_supervisor_lifecycle_begin();
#endif
  ret = bk7258_ap_start_locked(timeout_ms);
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  bk7258_ap_supervisor_lifecycle_end();
#endif
  nxmutex_unlock(&g_bk7258_ap_lock);
  return ret;
}

int bk7258_ap_stop(uint32_t timeout_ms)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_ap_lock);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_WIFI_VNET
  /* The official v3.1.1.9 controller has no supported Wi-Fi deinit path.
   * Resetting AP alone can therefore strand CP-owned vnet pointers and open
   * mailbox channels.  Whole-chip reset is the bounded recovery operation.
   */

  if (bk7258_wifi_controller_active())
    {
      nxmutex_unlock(&g_bk7258_ap_lock);
      return -EBUSY;
    }
#endif

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  bk7258_ap_supervisor_lifecycle_begin();
#endif
  ret = bk7258_ap_stop_locked(timeout_ms);
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  bk7258_ap_supervisor_lifecycle_end();
#endif
  nxmutex_unlock(&g_bk7258_ap_lock);
  return ret;
}

int bk7258_ap_restart(uint32_t timeout_ms)
{
  int ret;

  ret = nxmutex_lock(&g_bk7258_ap_lock);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_WIFI_VNET
  if (bk7258_wifi_controller_active())
    {
      nxmutex_unlock(&g_bk7258_ap_lock);
      return -EBUSY;
    }
#endif

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  bk7258_ap_supervisor_lifecycle_begin();
#endif
  ret = bk7258_ap_stop_locked(timeout_ms);
  if (ret == OK || ret == -ETIMEDOUT)
    {
      up_mdelay(BK7258_AP_RESTART_DELAY_MS);
      ret = bk7258_ap_start_locked(timeout_ms);
    }
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  bk7258_ap_supervisor_lifecycle_end();
#endif

  nxmutex_unlock(&g_bk7258_ap_lock);
  return ret;
}

int bk7258_ap_ipi_test(uint32_t count, uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_ap_ipi_state_s *ipi = bk7258_ap_ipi_state();
  uint32_t elapsed;
  uint32_t event;
  int ret;

  if (count == 0)
    {
      count = BK7258_AP_IPI_DEFAULT_COUNT;
    }

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_IPI_DEFAULT_TIMEOUT_MS;
    }

  if (count > BK7258_AP_IPI_MAX_COUNT)
    {
      return -ERANGE;
    }

  ret = nxmutex_lock(&g_bk7258_ap_lock);
  if (ret < 0)
    {
      return ret;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (bk7258_ap_scheduler_online())
    {
      ret = -ENOTSUP;
      goto out;
    }

  if (state->state != BK7258_AP_STATE_READY ||
      ipi->magic != BK7258_AP_IPI_STATE_MAGIC ||
      ipi->version != BK7258_AP_IPI_STATE_VERSION ||
      ipi->size != sizeof(struct bk7258_ap_ipi_state_s) ||
      ipi->generation != state->generation)
    {
      ret = -EAGAIN;
      goto out;
    }

  if (ipi->state == BK7258_AP_IPI_STATE_RUNNING ||
      ipi->state == BK7258_AP_IPI_STATE_REQUESTED)
    {
      ret = -EBUSY;
      goto out;
    }

  ipi->requested_count = count;
  ipi->completed_count = 0;
  ipi->timeout_ms = timeout_ms;
  ipi->error = BK7258_AP_IPI_ERROR_NONE;
  ipi->state = BK7258_AP_IPI_STATE_REQUESTED;
  state->command = BK7258_AP_COMMAND_IPI_TEST;
  __asm volatile ("dmb sy" ::: "memory");
  bk7258_ap_mbox_send(BK7258_AP_EVENT_IPI_TEST);

  ret = -ETIMEDOUT;
  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      event = bk7258_ap_mbox_receive();
      if (event != BK7258_AP_EVENT_NONE)
        {
          state->last_event = event;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (ipi->state == BK7258_AP_IPI_STATE_PASSED)
        {
          ret = OK;
          break;
        }

      if (ipi->state == BK7258_AP_IPI_STATE_FAILED ||
          state->state == BK7258_AP_STATE_FAILED)
        {
          ret = -EIO;
          break;
        }

      nxsig_usleep(1000);
    }

out:
  nxmutex_unlock(&g_bk7258_ap_lock);
  return ret;
}

void bk7258_ap_get_status(struct bk7258_ap_boot_state_s *status)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  if (status != NULL)
    {
      __asm volatile ("dmb sy" ::: "memory");
      memcpy(status, (const void *)(uintptr_t)state, sizeof(*status));
      __asm volatile ("dmb sy" ::: "memory");
    }
}
