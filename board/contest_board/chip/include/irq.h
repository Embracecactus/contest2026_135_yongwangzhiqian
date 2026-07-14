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

#endif /* __ARCH_RISCV_INCLUDE_RV1126B_IRQ_H */
