/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_mailbox.h
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

#ifndef __BOARD_CHIP_RV1126B_MAILBOX_H
#define __BOARD_CHIP_RV1126B_MAILBOX_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Mailbox channel assignment for AMP (HPMCU <-> Linux A-core):
 *
 * RX (Linux -> HPMCU): MBOX7, A2B direction
 *   Linux writes MBOX7 A2B_CMD/DATA; HPMCU reads A2B_CMD/DATA.
 *   Interrupt: HPMCU_MBOX3_BB_IRQn = 116
 *
 * TX vqid0 (HPMCU -> Linux): MBOX4, B2A direction
 *   HPMCU writes MBOX4 B2A_CMD/DATA; Linux reads on its side.
 *
 * TX vqid1 (HPMCU -> Linux): MBOX7, B2A direction
 *   HPMCU writes MBOX7 B2A_CMD/DATA; Linux reads on its side.
 */

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*rv1126b_mbox_callback_t)(void *arg);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_mailbox_init
 *
 * Description:
 *   Configure mailbox hardware and attach the RX ISR.  The RX interrupt
 *   is kept DISABLED after init; call rv1126b_mailbox_enable_and_drain()
 *   to enable it.  Does NOT blindly clear any pending A2B status, so the
 *   first handshake from Linux (if already latched) is preserved.
 *
 * Input Parameters:
 *   callback - Function to call when a valid doorbell arrives from Linux
 *   arg      - Opaque argument passed to callback
 *
 * Returned Value:
 *   OK on success; negative errno on failure.
 *
 ****************************************************************************/

int rv1126b_mailbox_init(rv1126b_mbox_callback_t callback, void *arg);

/****************************************************************************
 * Name: rv1126b_mailbox_enable_and_drain
 *
 * Description:
 *   Enable RX interrupt (MBOX7 A2B bit0) and then atomically check for /
 *   consume any latched pending handshake.  The function internally
 *   serializes via irqsave/spinlock, enables RX and the INTMUX IRQ, then
 *   processes any already-latched handshake through the callback.  Uses the
 *   same drain logic as the ISR so the first valid handshake is processed
 *   exactly once.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void rv1126b_mailbox_enable_and_drain(void);

/****************************************************************************
 * Name: rv1126b_mailbox_disable
 *
 * Description:
 *   Disable RX interrupt (MBOX7 A2B bit0) and clear any pending status.
 *   Caller's responsibility to hold irqsave if synchronizing with ISR.
 *
 ****************************************************************************/

void rv1126b_mailbox_disable(void);

/****************************************************************************
 * Name: rv1126b_mailbox_deinit
 *
 * Description:
 *   Detach IRQ handler, disable TX TRIG_MODE, and clear platform state.
 *   After deinit the mailbox is quiesced; a subsequent init is permitted.
 *
 ****************************************************************************/

void rv1126b_mailbox_deinit(void);

/****************************************************************************
 * Name: rv1126b_mailbox_notify
 *
 * Description:
 *   Send a doorbell notification to Linux A-core.
 *   Routes to the correct TX instance based on vqid:
 *     vqid 0       -> MBOX4 B2A
 *     vqid 1       -> MBOX7 B2A
 *     RPTUN_NOTIFY_ALL (UINT32_MAX) -> both MBOX4 and MBOX7 B2A
 *     other        -> -EINVAL
 *
 * Input Parameters:
 *   vqid - Virtqueue ID to notify (0, 1, or RPTUN_NOTIFY_ALL)
 *
 * Returned Value:
 *   OK on success; -EINVAL on invalid vqid.
 *
 ****************************************************************************/

int rv1126b_mailbox_notify(uint32_t vqid);

#endif /* __BOARD_CHIP_RV1126B_MAILBOX_H */
