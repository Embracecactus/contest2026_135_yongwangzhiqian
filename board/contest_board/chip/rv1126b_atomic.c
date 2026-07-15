/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_atomic.c
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
 * Minimal GCC atomic builtins for RV1126B (no libatomic available).
 *
 * RV1126B SCR1 is single-core M-mode without the RISC-V A extension.
 * Each read-modify-write operation is wrapped with up_irq_save() /
 * up_irq_restore() and a UP_DMB() barrier to ensure atomicity against
 * interrupt preemption.
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <stdbool.h>
#include <stdint.h>

#include <arch/barriers.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool __atomic_compare_exchange_4(volatile void *ptr, void *expected,
                                 uint32_t desired, bool weak,
                                 int success_memorder, int failure_memorder)
{
  uint32_t *ep = (uint32_t *)expected;
  volatile uint32_t *pp = (volatile uint32_t *)ptr;
  uint32_t old;
  irqstate_t flags;
  bool result;

  (void)weak;
  (void)success_memorder;
  (void)failure_memorder;

  flags = up_irq_save();
  UP_DMB();

  old = *ep;
  if (*pp == old)
    {
      *pp = desired;
      result = true;
    }
  else
    {
      /* CAS failure: write the observed current value back to *expected */

      *ep = *pp;
      result = false;
    }

  UP_DMB();
  up_irq_restore(flags);
  return result;
}

uint32_t __atomic_fetch_add_4(volatile void *ptr, uint32_t val,
                              int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;
  irqstate_t flags;
  uint32_t old;

  (void)memorder;

  flags = up_irq_save();
  UP_DMB();

  old = *pp;
  *pp = old + val;

  UP_DMB();
  up_irq_restore(flags);
  return old;
}

uint32_t __atomic_fetch_sub_4(volatile void *ptr, uint32_t val,
                              int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;
  irqstate_t flags;
  uint32_t old;

  (void)memorder;

  flags = up_irq_save();
  UP_DMB();

  old = *pp;
  *pp = old - val;

  UP_DMB();
  up_irq_restore(flags);
  return old;
}

uint32_t __atomic_fetch_or_4(volatile void *ptr, uint32_t val,
                             int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;
  irqstate_t flags;
  uint32_t old;

  (void)memorder;

  flags = up_irq_save();
  UP_DMB();

  old = *pp;
  *pp = old | val;

  UP_DMB();
  up_irq_restore(flags);
  return old;
}

uint32_t __atomic_fetch_and_4(volatile void *ptr, uint32_t val,
                              int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;
  irqstate_t flags;
  uint32_t old;

  (void)memorder;

  flags = up_irq_save();
  UP_DMB();

  old = *pp;
  *pp = old & val;

  UP_DMB();
  up_irq_restore(flags);
  return old;
}
