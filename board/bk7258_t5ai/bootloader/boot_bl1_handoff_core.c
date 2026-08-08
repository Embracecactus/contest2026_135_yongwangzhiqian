/* SPDX-License-Identifier: Apache-2.0 */

#include "boot_bl1_handoff_core.h"

int bk7258_bl1_handoff_vector_valid(
  const struct bk7258_bl1_vector_s *authorized,
  const struct bk7258_bl1_vector_s *loaded,
  const struct bk7258_bl1_handoff_window_s *window)
{
  uint32_t reset_address;

  if (authorized == (const void *)0 || loaded == (const void *)0 ||
      window == (const void *)0 ||
      window->stack_start >= window->stack_end ||
      window->execute_start >= window->execute_end ||
      authorized->msp != loaded->msp ||
      authorized->reset != loaded->reset ||
      (loaded->msp & 3u) != 0u ||
      loaded->msp < window->stack_start ||
      loaded->msp > window->stack_end ||
      (loaded->reset & 1u) == 0u)
    {
      return 0;
    }

  reset_address = loaded->reset & ~1u;
  return reset_address >= window->execute_start &&
         reset_address < window->execute_end;
}
