/****************************************************************************
 * apps/bk7258/bk7258_native_abi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __APP_BK7258_NATIVE_ABI_H
#define __APP_BK7258_NATIVE_ABI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKNATIVE_ABI 1u
#define BKNATIVE_STATE_MAX 256u

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bknative_frame_s
{
  /* 0 keeps the current frame; 1 happy, 2 thinking, 3 neutral.
   * Display intents expire within 1..5000 ms and never persist a default.
   */

  uint32_t expression;
  uint32_t duration_ms;
  uint32_t progress;
};

struct bknative_program_s
{
  uint32_t abi;
  uint32_t state_size;
  /* Cooperative callbacks must return promptly without imports, allocation,
   * threads, hardware access or retaining pointers outside their state.
   * start: 0 accepted, negative error.
   * step: 0 active, 1 done, negative error.
   */

  int (*start)(void *state, uint32_t argument);
  int (*step)(void *state, uint32_t elapsed_ms,
              struct bknative_frame_s *frame);
};

typedef const struct bknative_program_s *(*bknative_entry_t)(void);

#endif
