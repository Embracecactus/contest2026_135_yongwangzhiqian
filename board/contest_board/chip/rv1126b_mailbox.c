/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_mailbox.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * RV1126B Mailbox V2 driver for AMP notification (HPMCU <-> Linux A-core)
 *
 * Hardware: Rockchip mailbox V2.0, single-channel CMD+DATA register pairs.
 * Each instance has A2B and B2A channels with hiword write-enable INTEN.
 *
 * AMP assignment:
 *   RX (Linux -> HPMCU): MBOX7 A2B, IRQ 116 (HPMCU_MBOX3_BB)
 *   TX vqid0            : MBOX4 B2A
 *   TX vqid1            : MBOX7 B2A
 *   TX ALL (UINT32_MAX) : both MBOX4 B2A and MBOX7 B2A
 *
 * Busy contract (V2.0): before writing CMD+DATA, check target instance
 * B2A_STATUS bit0.  If set -> doorbell already pending, return OK (merge).
 * If clear -> write CMD=0x03, DATA="RMSG" (0x524d5347) as two 32-bit MMIO.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdint.h>

#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include <arch/barriers.h>

#include "riscv_internal.h"

#include "hardware/rv1126b_memorymap.h"
#include "hardware/rv1126b_mailbox.h"
#include "rv1126b_mailbox.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* INTMUX source and IRQ for HPMCU_MBOX3 BB interrupt (MBOX7 A2B RX) */

#define MBOX3_INTMUX_SOURCE   RV1126B_MBOX3_BB_INTMUX_SOURCE  /* 116 */
#define MBOX3_IRQ             RV1126B_INTMUX_SOURCE_TO_IRQ(MBOX3_INTMUX_SOURCE)

/* Instance base addresses (using constants from rv1126b_memorymap.h) */

#define MBOX_RX_BASE          RV1126B_MBOX7_BASE  /* MBOX7 A2B: Linux -> HPMCU */
#define MBOX_TX0_BASE         RV1126B_MBOX4_BASE  /* MBOX4 B2A: HPMCU -> Linux vqid0 */
#define MBOX_TX1_BASE         RV1126B_MBOX7_BASE  /* MBOX7 B2A: HPMCU -> Linux vqid1 */

/* Mailbox handshake magic and command */

#define MBOX_MAGIC             0x524d5347u         /* "RMSG" */
#define MBOX_CMD               0x03u               /* Handshake command */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static rv1126b_mbox_callback_t g_mbox_callback;
static void                   *g_mbox_arg;
static spinlock_t              g_mbox_lock = SP_UNLOCKED;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_mbox_consume_locked
 *
 * Description:
 *   Consume one pending A2B event from the RX instance (MBOX7) while
 *   g_mbox_lock is held.  Reads CMD+DATA before clearing only status bit0,
 *   executes UP_DSB after W1C, and snapshots callback/arg under the lock.
 *
 *   Payload validation, warning, and callback invocation are deliberately
 *   deferred until after the caller releases g_mbox_lock and restores the
 *   saved IRQ state.
 *
 * Returned Value:
 *   One if an event was consumed; zero if no bit0 event was pending.
 *
 ****************************************************************************/

static int rv1126b_mbox_consume_locked(
  rv1126b_mbox_callback_t *callback, void **cb_arg,
  uint32_t *cmd, uint32_t *data)
{
  uint32_t status;

  status = getreg32(MBOX_RX_BASE + RV1126B_MBOX_A2B_STATUS_OFFSET);

  if (!(status & RV1126B_MBOX_INT_TX_DONE))
    {
      return 0;
    }

  /* Read CMD and DATA payload BEFORE clearing status */

  *cmd  = getreg32(MBOX_RX_BASE + RV1126B_MBOX_A2B_CMD_OFFSET);
  *data = getreg32(MBOX_RX_BASE + RV1126B_MBOX_A2B_DATA_OFFSET);

  /* W1C: clear only bit0, then publish the consume before dispatch */

  putreg32(RV1126B_MBOX_INT_TX_DONE,
           MBOX_RX_BASE + RV1126B_MBOX_A2B_STATUS_OFFSET);

  UP_DSB();

  /* Snapshot callback state consistently with init/deinit */

  *callback = g_mbox_callback;
  *cb_arg   = g_mbox_arg;

  return 1;
}

/****************************************************************************
 * Name: rv1126b_mbox_dispatch
 *
 * Description:
 *   Validate and dispatch a consumed event after g_mbox_lock is unlocked
 *   and the caller's IRQ state is restored.
 *
 ****************************************************************************/

static void rv1126b_mbox_dispatch(int consumed,
                                  rv1126b_mbox_callback_t callback,
                                  void *cb_arg, uint32_t cmd, uint32_t data)
{
  if (!consumed)
    {
      return;
    }

  if (cmd != MBOX_CMD || data != MBOX_MAGIC)
    {
      syslog(LOG_WARNING,
             "rv1126b_mailbox: spurious A2B event cmd=0x%08" PRIx32
             " data=0x%08" PRIx32 "\n", cmd, data);
      return;
    }

  if (callback != NULL)
    {
      callback(cb_arg);
    }
}

/****************************************************************************
 * Name: rv1126b_mbox_isr
 *
 * Description:
 *   ISR for HPMCU_MBOX3 BB interrupt (INTMUX source 116).
 *   Consumes and snapshots under g_mbox_lock, then validates and invokes
 *   the platform callback only after the lock is released and irqstate is
 *   restored.
 *
 ****************************************************************************/

static int rv1126b_mbox_isr(int irq, void *context, void *arg)
{
  rv1126b_mbox_callback_t callback = NULL;
  void *cb_arg = NULL;
  uint32_t cmd = 0;
  uint32_t data = 0;
  irqstate_t flags;
  int consumed;

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  flags = spin_lock_irqsave(&g_mbox_lock);
  consumed = rv1126b_mbox_consume_locked(&callback, &cb_arg, &cmd, &data);
  spin_unlock_irqrestore(&g_mbox_lock, flags);

  rv1126b_mbox_dispatch(consumed, callback, cb_arg, cmd, data);

  return OK;
}

/****************************************************************************
 * Name: rv1126b_mbox_tx_one
 *
 * Description:
 *   Send a doorbell to Linux via the specified TX instance's B2A channel.
 *   Checks B2A_STATUS bit0 for busy (V2.0 busy merge: if set, doorbell
 *   already pending -> return OK).  If idle, writes CMD=0x03 followed by
 *   DATA="RMSG" as two 32-bit MMIO writes (no 64-bit atomic needed).
 *
 *   Caller holds g_mbox_lock.
 *
 ****************************************************************************/

static int rv1126b_mbox_tx_one(uintptr_t tx_base)
{
  uint32_t status;

  /* Check B2A_STATUS bit0 — if already busy, doorbell is merged */

  status = getreg32(tx_base + RV1126B_MBOX_B2A_STATUS_OFFSET);

  if (status & RV1126B_MBOX_INT_TX_DONE)
    {
      /* Doorbell already pending on this channel — merge, return OK */

      return OK;
    }

  /* Write CMD first, then DATA (DATA write triggers the hardware) */

  putreg32(MBOX_CMD,  tx_base + RV1126B_MBOX_B2A_CMD_OFFSET);
  putreg32(MBOX_MAGIC, tx_base + RV1126B_MBOX_B2A_DATA_OFFSET);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_mailbox_init
 ****************************************************************************/

int rv1126b_mailbox_init(rv1126b_mbox_callback_t callback, void *arg)
{
  int ret;
  irqstate_t flags;

  if (callback == NULL)
    {
      return -EINVAL;
    }

  /* Store callback and argument under lock */

  flags = spin_lock_irqsave(&g_mbox_lock);
  g_mbox_callback = callback;
  g_mbox_arg      = arg;
  spin_unlock_irqrestore(&g_mbox_lock, flags);

  /* Configure TX instances B2A TRIG_MODE (CMD+DATA trigger).
   * Both MBOX4 and MBOX7 B2A use the same TRIG_MODE setting.
   */

  putreg32(RV1126B_MBOX_B2A_TRIGMODE_SET,
           MBOX_TX0_BASE + RV1126B_MBOX_B2A_INTEN_OFFSET);

  putreg32(RV1126B_MBOX_B2A_TRIGMODE_SET,
           MBOX_TX1_BASE + RV1126B_MBOX_B2A_INTEN_OFFSET);

  /* Attach ISR for MBOX3 BB (MBOX7 A2B RX).  Keep IRQ DISABLED.
   * Do NOT blindly clear A2B pending — first handshake may already
   * be latched from Linux and will be consumed by enable_and_drain().
   */

  ret = irq_attach(MBOX3_IRQ, rv1126b_mbox_isr, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "rv1126b_mailbox: irq_attach(%u) failed: %d\n",
             MBOX3_IRQ, ret);
      return ret;
    }

  syslog(LOG_INFO,
         "rv1126b_mailbox: RX MBOX%d A2B IRQ=%u, "
         "TX MBOX%d B2A(vqid0) MBOX%d B2A(vqid1)\n",
         RV1126B_MBOX_RX_INST, MBOX3_IRQ,
         RV1126B_MBOX_TX0_INST, RV1126B_MBOX_TX1_INST);

  return OK;
}

/****************************************************************************
 * Name: rv1126b_mailbox_enable_and_drain
 ****************************************************************************/

void rv1126b_mailbox_enable_and_drain(void)
{
  rv1126b_mbox_callback_t callback = NULL;
  void *cb_arg = NULL;
  uint32_t cmd = 0;
  uint32_t data = 0;
  irqstate_t flags;
  int consumed;

  flags = spin_lock_irqsave(&g_mbox_lock);

  /* Enable RX interrupt: set MBOX7 A2B_INTEN bit0 (TX_DONE).
   * Uses hiword write-enable: (1 << 16) | (1 << 0) = 0x00010001.
   */

  putreg32(RV1126B_MBOX_A2B_INTEN_ENABLE,
           MBOX_RX_BASE + RV1126B_MBOX_A2B_INTEN_OFFSET);

  /* Enable the INTMUX IRQ at the interrupt controller level */

  up_enable_irq(MBOX3_IRQ);

  /* Atomically consume and snapshot any already-latched handshake.
   * A handshake arriving after this consume remains latched and is handled
   * by the enabled ISR after irqstate is restored.
   */

  consumed = rv1126b_mbox_consume_locked(&callback, &cb_arg, &cmd, &data);

  spin_unlock_irqrestore(&g_mbox_lock, flags);

  /* Never invoke the platform callback while holding g_mbox_lock. */

  rv1126b_mbox_dispatch(consumed, callback, cb_arg, cmd, data);
}

/****************************************************************************
 * Name: rv1126b_mailbox_disable
 ****************************************************************************/

void rv1126b_mailbox_disable(void)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_mbox_lock);

  /* Disable the INTMUX IRQ at the interrupt controller level */

  up_disable_irq(MBOX3_IRQ);

  /* Disable RX interrupt: clear MBOX7 A2B_INTEN bit0.
   * Uses hiword write-enable: (1 << 16) | 0 = 0x00010000.
   */

  putreg32(RV1126B_MBOX_A2B_INTEN_DISABLE,
           MBOX_RX_BASE + RV1126B_MBOX_A2B_INTEN_OFFSET);

  /* Clear any pending A2B status (W1C) */

  putreg32(RV1126B_MBOX_INT_TX_DONE,
           MBOX_RX_BASE + RV1126B_MBOX_A2B_STATUS_OFFSET);

  spin_unlock_irqrestore(&g_mbox_lock, flags);
}

/****************************************************************************
 * Name: rv1126b_mailbox_deinit
 ****************************************************************************/

void rv1126b_mailbox_deinit(void)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_mbox_lock);

  /* First disable IRQ and clear INTEN to stop new interrupts */

  up_disable_irq(MBOX3_IRQ);

  putreg32(RV1126B_MBOX_A2B_INTEN_DISABLE,
           MBOX_RX_BASE + RV1126B_MBOX_A2B_INTEN_OFFSET);

  /* Detach the ISR */

  irq_detach(MBOX3_IRQ);

  /* Clear TRIG_MODE on both TX instances */

  putreg32(RV1126B_MBOX_B2A_TRIGMODE_CLR,
           MBOX_TX0_BASE + RV1126B_MBOX_B2A_INTEN_OFFSET);

  putreg32(RV1126B_MBOX_B2A_TRIGMODE_CLR,
           MBOX_TX1_BASE + RV1126B_MBOX_B2A_INTEN_OFFSET);

  /* Clear pending A2B status on RX instance */

  putreg32(RV1126B_MBOX_INT_TX_DONE,
           MBOX_RX_BASE + RV1126B_MBOX_A2B_STATUS_OFFSET);

  /* Clear callback and arg */

  g_mbox_callback = NULL;
  g_mbox_arg      = NULL;

  spin_unlock_irqrestore(&g_mbox_lock, flags);
}

/****************************************************************************
 * Name: rv1126b_mailbox_notify
 ****************************************************************************/

int rv1126b_mailbox_notify(uint32_t vqid)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_mbox_lock);

  switch (vqid)
    {
    case 0:
      ret = rv1126b_mbox_tx_one(MBOX_TX0_BASE);
      break;

    case 1:
      ret = rv1126b_mbox_tx_one(MBOX_TX1_BASE);
      break;

    case UINT32_MAX:  /* RPTUN_NOTIFY_ALL */
      ret = rv1126b_mbox_tx_one(MBOX_TX0_BASE);
      if (ret >= 0)
        {
          ret = rv1126b_mbox_tx_one(MBOX_TX1_BASE);
        }
      break;

    default:
      syslog(LOG_ERR, "rv1126b_mailbox: invalid vqid %" PRIu32 "\n", vqid);
      ret = -EINVAL;
      break;
    }

  spin_unlock_irqrestore(&g_mbox_lock, flags);

  return ret;
}
