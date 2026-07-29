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

#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>

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
#define BK7258_AP_HEARTBEAT_US      100000u

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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

static int bk7258_ap_validate_runtime(void)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  void *test;
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

  if (*(volatile uint32_t *)BK7258_LOCAL_CORE_ID_ADDR != 0 ||
      state->physical_core_id != 1)
    {
      return BK7258_AP_ERROR_BAD_CORE_ID;
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_main(int argc, char *argv[])
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  uint32_t event;
  int error;
  int ret;

  (void)argc;
  (void)argv;

  if (state->magic != BK7258_AP_BOOT_STATE_MAGIC ||
      state->version != BK7258_AP_BOOT_STATE_VERSION ||
      state->size != sizeof(struct bk7258_ap_boot_state_s))
    {
      state->state = BK7258_AP_STATE_FAILED;
      state->error = BK7258_AP_ERROR_BAD_BOOT_STATE;
      bk7258_ap_mbox_send(BK7258_AP_EVENT_FAILED);
      goto parked;
    }

  error = bk7258_ap_validate_runtime();
  if (error != BK7258_AP_ERROR_NONE)
    {
      state->state = BK7258_AP_STATE_FAILED;
      state->error = (uint32_t)error;
      bk7258_ap_mbox_send(BK7258_AP_EVENT_FAILED);
      goto parked;
    }

  ret = bk7258_cpu2_probe_start(BK7258_CPU2_PROBE_TIMEOUT_MS);
  if (ret < 0)
    {
      state->state = BK7258_AP_STATE_FAILED;
      state->error = BK7258_AP_ERROR_CPU2_PROBE;
      bk7258_ap_mbox_send(BK7258_AP_EVENT_FAILED);
      goto parked;
    }

  state->error      = BK7258_AP_ERROR_NONE;
  state->last_event = BK7258_AP_EVENT_READY;
  state->state      = BK7258_AP_STATE_READY;
  __asm volatile ("dmb sy" ::: "memory");
  bk7258_ap_mbox_send(BK7258_AP_EVENT_READY);

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
          ret = bk7258_cpu2_probe_stop(
            BK7258_CPU2_PROBE_STOP_TIMEOUT_MS);
          if (ret < 0)
            {
              state->error = BK7258_AP_ERROR_CPU2_PROBE;
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

      state->heartbeat++;
      __asm volatile ("dmb sy; sev" ::: "memory");
      nxsig_usleep(BK7258_AP_HEARTBEAT_US);
    }

parked:
  __asm volatile ("cpsid i; dsb sy; isb sy" ::: "memory");
  for (; ; )
    {
      __asm volatile ("wfe");
    }

  return 0;
}
