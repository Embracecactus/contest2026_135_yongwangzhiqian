/****************************************************************************
 * apps/bk7258/native-apps/breathing.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_abi.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct breathing_s
{
  uint32_t duration;
  uint32_t phase;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int breathing_start(void *memory, uint32_t argument)
{
  struct breathing_s *state = memory;
  state->duration = argument * 1000u;
  state->phase = UINT32_MAX;
  return 0;
}

static int breathing_step(void *memory, uint32_t elapsed,
                          struct bknative_frame_s *frame)
{
  struct breathing_s *state = memory;
  uint32_t phase = elapsed / 4000u;
  if (elapsed >= state->duration)
    {
      return 1;
    }

  frame->progress = elapsed / (state->duration / 100u + 1u);
  if (phase != state->phase)
    {
      state->phase = phase;
      frame->expression = phase % 2u ? 2u : 1u;
      frame->duration_ms = 4000u;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

const struct bknative_program_s *shaniu_native_entry(void)
{
  static const struct bknative_program_s program =
  {
    .abi = BKNATIVE_ABI,
    .state_size = sizeof(struct breathing_s),
    .start = breathing_start,
    .step = breathing_step
  };

  return &program;
}
