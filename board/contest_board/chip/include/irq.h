/****************************************************************************
 * arch/risc-v/include/rv1126b/irq.h
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

#ifndef __ARCH_RISCV_INCLUDE_RV1126B_IRQ_H
#define __ARCH_RISCV_INCLUDE_RV1126B_IRQ_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RV1126B HPMCU INTMUX: 8 groups x 32 IRQs = 256 external IRQs */

#define NR_INTMUX_IRQS      256
#define NR_IRQS             (RISCV_IRQ_EXT + NR_INTMUX_IRQS)

/* INTMUX source to NuttX IRQ conversion.
 * NuttX IRQ = RISCV_IRQ_EXT + intmux_source_id.
 * Source 0 is reserved because it collides with the RISCV_IRQ_MEXT
 * aggregate slot in M-mode.
 */

#define RV1126B_INTMUX_SOURCE_TO_IRQ(s) (RISCV_IRQ_EXT + (s))
#define RV1126B_INTMUX_IRQ_TO_SOURCE(i) ((i) - RISCV_IRQ_EXT)
#define RV1126B_INTMUX_SOURCE_VALID(s)  ((s) > 0 && (s) < NR_INTMUX_IRQS)
#define RV1126B_INTMUX_IRQ_VALID(i)     ((i) > RISCV_IRQ_EXT && \
                                         (i) < NR_IRQS)

/* UART5 interrupt (INTMUX source 61) */

#define RV1126B_UART5_INTMUX_SOURCE     61
#define RV1126B_IRQ_UART5               RV1126B_INTMUX_SOURCE_TO_IRQ(61)

#endif /* __ARCH_RISCV_INCLUDE_RV1126B_IRQ_H */
