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
#include <nuttx/mutex.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>

/* TEMP cold-reset diagnostic: emit a short raw-UART tag so the last
 * completed startup checkpoint remains visible without /dev/console,
 * printf or syslog.  Delete after diagnosis.
 */

static void cold_ckpt(const char *tag)
{
  up_putc('\r');
  up_putc('\n');
  while (*tag)
    {
      up_putc(*tag++);
    }

  up_putc('\r');
  up_putc('\n');
}

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* These four routines are provided by the pinned Beken CP SDK archives. */

extern void sys_drv_set_cpu1_pwr_dw(uint32_t is_pwr_down);
extern void sys_drv_set_cpu1_rxevt_sel(uint32_t value);
extern void sys_drv_set_cpu1_boot_address_offset(uint32_t address_offset);
extern void sys_drv_set_cpu1_reset(uint32_t reset_value);

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

static inline volatile uint32_t *bk7258_ap_mbox(uint32_t base)
{
  return (volatile uint32_t *)(uintptr_t)base;
}

static void bk7258_cpu2_force_reset(void)
{
  volatile uint32_t *control =
    (volatile uint32_t *)(uintptr_t)BK7258_SYS_CPU2_CONTROL;

  *control &= ~BK7258_SYS_CPU2_RESET;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void bk7258_ap_mbox_ack(volatile uint32_t *mbox)
{
  mbox[BK7258_MBOX_CLEAR_OFFSET / 4] = BK7258_MBOX_BOX0_BIT;
  __asm volatile ("dsb sy" ::: "memory");
  mbox[BK7258_MBOX_READY_OFFSET / 4] = 0;
  mbox[BK7258_MBOX_CLEAR_OFFSET / 4] = 0;
  __asm volatile ("dsb sy" ::: "memory");
}

static void bk7258_ap_mbox_initialize(void)
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

static void bk7258_ap_state_prepare(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
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

static int bk7258_ap_wait(uint32_t wanted, uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  uint32_t elapsed;
  uint32_t event;
  bool first_iter = true;

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_DEFAULT_TIMEOUT_MS;
    }

  for (elapsed = 0; elapsed < timeout_ms; elapsed++)
    {
      event = bk7258_ap_mbox_receive();
      if (event != BK7258_AP_EVENT_NONE)
        {
          state->last_event = event;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (state->state == wanted)
        {
          return OK;
        }

      if (state->state == BK7258_AP_STATE_FAILED)
        {
          return -EIO;
        }

      if (first_iter)
        {
          cold_ckpt("W0"); /* Before first 1 ms sleep */
        }

      nxsig_usleep(1000);
      if (first_iter)
        {
          cold_ckpt("W1"); /* First 1 ms sleep returned */
          first_iter = false;
        }
    }

  return -ETIMEDOUT;
}

static int bk7258_ap_start_locked(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  int ret;

  cold_ckpt("A0"); /* Enter start_locked */

  if (state->magic == BK7258_AP_BOOT_STATE_MAGIC &&
      (state->state == BK7258_AP_STATE_READY ||
       state->state == BK7258_AP_STATE_STARTING))
    {
      return -EBUSY;
    }

  /* Hold CPU1 while replacing shared state and the boot address. */

  sys_drv_set_cpu1_reset(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  cold_ckpt("A1"); /* CPU1 reset hold done */

  bk7258_cpu2_force_reset();
  up_mdelay(BK7258_AP_RESTART_DELAY_MS);
  cold_ckpt("A2"); /* CPU2 force-reset + delay done */

  bk7258_ap_mbox_initialize();
  bk7258_ap_state_prepare();
  cold_ckpt("A3"); /* Mailbox/shared state prepared */

  /* Exact BK7258 CPU1 release order from the CP SDK. */

  sys_drv_set_cpu1_pwr_dw(0);
  sys_drv_set_cpu1_rxevt_sel(1);
  sys_drv_set_cpu1_boot_address_offset(BK7258_AP_FLASH_ADDR >> 8);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  cold_ckpt("A4"); /* CPU1 power/RXEVT/boot address done */

  sys_drv_set_cpu1_reset(1);
  __asm volatile ("dsb sy; sev" ::: "memory");
  cold_ckpt("A5"); /* CPU1 reset released */
  cold_ckpt("A6"); /* Entering AP READY wait */

  ret = bk7258_ap_wait(BK7258_AP_STATE_READY, timeout_ms);
  cold_ckpt("A7"); /* AP wait returned */
  if (ret < 0)
    {
      sys_drv_set_cpu1_reset(0);
      __asm volatile ("dsb sy; isb sy" ::: "memory");
      bk7258_cpu2_force_reset();
      up_mdelay(BK7258_AP_RESTART_DELAY_MS);
      cold_ckpt("F1"); /* Failure cleanup: AP cores held reset */

      sys_drv_set_cpu1_pwr_dw(1);
      cold_ckpt("F2"); /* Failure cleanup: CPU1 power-down done */

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
  int ret = OK;

  if (bk7258_ap_scheduler_online())
    {
      return -ENOTSUP;
    }

  if (state->state == BK7258_AP_STATE_READY ||
      state->state == BK7258_AP_STATE_STARTING)
    {
      state->command = BK7258_AP_COMMAND_STOP;
      __asm volatile ("dmb sy" ::: "memory");
      bk7258_ap_mbox_send(BK7258_AP_EVENT_STOP);
      ret = bk7258_ap_wait(BK7258_AP_STATE_STOPPED, timeout_ms);
    }

  /* A timeout still ends in a deterministic forced stop of both AP cores. */

  sys_drv_set_cpu1_reset(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  bk7258_cpu2_force_reset();
  up_mdelay(BK7258_AP_RESTART_DELAY_MS);
  sys_drv_set_cpu1_pwr_dw(1);

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
      bk7258_ap_mbox_initialize();
      g_bk7258_ap_initialized = true;
    }

  nxmutex_unlock(&g_bk7258_ap_lock);
  return OK;
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
      bk7258_ap_mbox_initialize();
      g_bk7258_ap_initialized = true;
    }

  ret = bk7258_ap_start_locked(timeout_ms);
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

  ret = bk7258_ap_stop_locked(timeout_ms);
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

  ret = bk7258_ap_stop_locked(timeout_ms);
  if (ret == OK || ret == -ETIMEDOUT)
    {
      up_mdelay(BK7258_AP_RESTART_DELAY_MS);
      ret = bk7258_ap_start_locked(timeout_ms);
    }

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
