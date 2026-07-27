/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_ap_vectors.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Physical CPU1 executes this AP-local logical-core-0 vector table.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <arch/chip/bk7258_amp.h>

#include "arm_internal.h"

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

extern void exception_common(void);
extern void __start(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void bk7258_ap_fault_doorbell(uint32_t event)
{
  volatile uint32_t *mbox =
    (volatile uint32_t *)(uintptr_t)BK7258_MBOX1_BASE;
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();

  state->state      = BK7258_AP_STATE_FAILED;
  state->error      = BK7258_AP_ERROR_BAD_BOOT_STATE;
  state->last_event = event;

  mbox[BK7258_MBOX_SENDER_OFFSET / 4] = 1u << 1;
  mbox[BK7258_MBOX_RECEIVER_OFFSET / 4] = 1u << 0;
  mbox[BK7258_MBOX_PARAM0_OFFSET / 4] = BK7258_AP_DOORBELL_MAGIC;
  mbox[BK7258_MBOX_PARAM1_OFFSET / 4] = event;
  mbox[BK7258_MBOX_PARAM2_OFFSET / 4] = state->generation;
  mbox[BK7258_MBOX_PARAM3_OFFSET / 4] = state->state;
  __asm volatile ("dmb sy" ::: "memory");
  mbox[BK7258_MBOX_READY_OFFSET / 4] = BK7258_MBOX_BOX0_BIT;
  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

static void bk7258_ap_fault_handler(void)
{
  bk7258_ap_fault_doorbell(BK7258_AP_EVENT_FAILED);

  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

__attribute__((section(".vectors_core0"), used, aligned(4)))
const void *const __vector_core0_table[80] =
{
  [0]       = (void *)BK7258_AP_INITIAL_MSP,
  [1]       = (void *)__start,
  [2 ... 3] = &bk7258_ap_fault_handler,
  [4 ... 79] = &exception_common
};

/* NuttX ARM common code looks up _vectors.  Keep the official AP-local name
 * as the storage symbol and provide _vectors as an exact alias.
 */

extern const void *const _vectors[80]
  __attribute__((alias("__vector_core0_table")));
