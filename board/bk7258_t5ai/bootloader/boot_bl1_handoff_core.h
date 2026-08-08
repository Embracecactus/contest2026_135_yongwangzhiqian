/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_BOOT_BL1_HANDOFF_CORE_H
#define BK7258_BOOT_BL1_HANDOFF_CORE_H

#include <stdint.h>

struct bk7258_bl1_vector_s
{
  uint32_t msp;
  uint32_t reset;
};

struct bk7258_bl1_handoff_window_s
{
  uint32_t stack_start;
  uint32_t stack_end;
  uint32_t execute_start;
  uint32_t execute_end;
};

/* Validate the two words that define a Cortex-M handoff.  The recovered
 * BK7236 BootROM compares these words before requesting its hardware reset
 * handoff.  This pure board core additionally enforces the already-proven
 * BK7258 SRAM execution window; it performs no MMIO or branch. */
int bk7258_bl1_handoff_vector_valid(
  const struct bk7258_bl1_vector_s *authorized,
  const struct bk7258_bl1_vector_s *loaded,
  const struct bk7258_bl1_handoff_window_s *window);

#endif /* BK7258_BOOT_BL1_HANDOFF_CORE_H */
