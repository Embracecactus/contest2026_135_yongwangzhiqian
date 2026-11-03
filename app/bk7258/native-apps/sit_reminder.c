/****************************************************************************
 * apps/bk7258/native-apps/sit_reminder.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/* "Sit reminder": stays quiet while the device keeps reporting movement.
 * After the run seconds (the stillness threshold, for example 2700 for 45
 * minutes) without a moved/tilted event it shows a happy face and requests
 * a pulse every 20 seconds, up to three times.  Movement then completes
 * with result 1; no movement within 60 seconds completes with result 2.
 * If the device never stays still for the threshold within four thresholds
 * of run time, it completes with result 3.
 *
 * Like every native app it is stopped by voice, OTA or other priority
 * owners; it is not a background service.
 *
 * Manifest: abi 3, permissions display,motion,haptic,
 * results ["起来了", "还没动", "一直在活动"].
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_abi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SIT_PULSE_MS 20000u
#define SIT_PULSES 3u
#define SIT_ANSWER_MS 60000u
#define SIT_FACE_MS 2500u
#define SIT_HAPPY 1u
#define SIT_UP 1u
#define SIT_MISSED 2u
#define SIT_ACTIVE 3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sit_s
{
  uint32_t threshold;
  uint32_t last_move;
  uint32_t nudged_at;     /* UINT32_MAX until the reminder begins */
  uint32_t pulses;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sit_start(void *memory, uint32_t argument)
{
  struct sit_s *state = memory;
  state->threshold = argument * 1000u;
  state->last_move = 0;
  state->nudged_at = UINT32_MAX;
  state->pulses = 0;
  return 0;
}

static int sit_step(void *memory, uint32_t elapsed,
                    const struct bknative_input3_s *input,
                    struct bknative_output3_s *output)
{
  struct sit_s *state = memory;
  uint32_t moved = (input->base.events &
                    (BKNATIVE_EVENT_MOVED | BKNATIVE_EVENT_TILTED));

  if (state->nudged_at == UINT32_MAX)
    {
      if (moved)
        {
          state->last_move = elapsed;
        }

      if (elapsed - state->last_move >= state->threshold)
        {
          state->nudged_at = elapsed;
        }
      else
        {
          if (elapsed >= state->threshold * 4u)
            {
              output->result = SIT_ACTIVE;
              return 1;
            }

          return 0;
        }
    }
  else if (moved && state->pulses)
    {
      output->result = SIT_UP;
      return 1;
    }

  if (elapsed - state->nudged_at >= SIT_ANSWER_MS)
    {
      output->result = SIT_MISSED;
      return 1;
    }

  if (state->pulses < SIT_PULSES &&
      elapsed - state->nudged_at >= state->pulses * SIT_PULSE_MS)
    {
      state->pulses++;
      output->base.haptic = 1;
      output->base.frame.expression = SIT_HAPPY;
      output->base.frame.duration_ms = SIT_FACE_MS;
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
    .abi = BKNATIVE_ABI_V3,
    .state_size = sizeof(struct sit_s),
    .start = sit_start,
    .step3 = sit_step,
    .saved_size = 0
  };

  return &program;
}
