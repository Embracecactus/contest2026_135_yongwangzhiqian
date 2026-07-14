/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_atomic.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal GCC atomic builtins for RV1126B (no libatomic available).
 * RV1126B SCR1 is single-core M-mode, so these are simple non-atomic
 * implementations that are safe without real hardware atomics.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool __atomic_compare_exchange_4(volatile void *ptr, void *expected,
                                 uint32_t desired, bool weak,
                                 int success_memorder, int failure_memorder)
{
  uint32_t *ep = (uint32_t *)expected;
  volatile uint32_t *pp = (volatile uint32_t *)ptr;
  uint32_t old = *ep;

  (void)weak;
  (void)success_memorder;
  (void)failure_memorder;

  if (*pp == old)
    {
      *pp = desired;
      return true;
    }

  *ep = *pp;
  return false;
}

uint32_t __atomic_fetch_add_4(volatile void *ptr, uint32_t val,
                              int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;

  (void)memorder;
  uint32_t old = *pp;
  *pp = old + val;
  return old;
}

uint32_t __atomic_fetch_sub_4(volatile void *ptr, uint32_t val,
                              int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;

  (void)memorder;
  uint32_t old = *pp;
  *pp = old - val;
  return old;
}

uint32_t __atomic_fetch_or_4(volatile void *ptr, uint32_t val,
                             int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;

  (void)memorder;
  uint32_t old = *pp;
  *pp = old | val;
  return old;
}

uint32_t __atomic_fetch_and_4(volatile void *ptr, uint32_t val,
                              int memorder)
{
  volatile uint32_t *pp = (volatile uint32_t *)ptr;

  (void)memorder;
  uint32_t old = *pp;
  *pp = old & val;
  return old;
}
