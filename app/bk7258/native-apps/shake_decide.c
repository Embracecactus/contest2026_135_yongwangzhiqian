/****************************************************************************
 * apps/bk7258/native-apps/shake_decide.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/* "Shake to decide": waits with a thinking face, and each time the product
 * motion classifier reports movement it shows a happy (yes) or neutral (no)
 * answer for three seconds with one haptic pulse.  The pulse is a request;
 * the product cooldown and quiet window decide whether it is felt.
 *
 * Manifest permissions: display, motion, haptic.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_abi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DECIDE_ANSWER_MS 3000u
#define DECIDE_IDLE_MS 2000u
#define DECIDE_HAPPY 1u
#define DECIDE_THINKING 2u
#define DECIDE_NEUTRAL 3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct decide_s
{
  uint32_t duration;
  uint32_t answer_until;
  uint32_t idle_shown;
  uint32_t answers;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int decide_start(void *memory, uint32_t argument)
{
  struct decide_s *state = memory;
  state->duration = argument * 1000u;
  state->answer_until = 0;
  state->idle_shown = UINT32_MAX;
  state->answers = 0;
  return 0;
}

static int decide_step(void *memory, uint32_t elapsed,
                       const struct bknative_input_s *input,
                       struct bknative_output_s *output)
{
  struct decide_s *state = memory;

  if (elapsed >= state->duration)
    {
      return 1;
    }

  output->frame.progress = elapsed / (state->duration / 100u + 1u);
  if ((input->events & BKNATIVE_EVENT_MOVED) &&
      elapsed >= state->answer_until)
    {
      output->frame.expression = (input->random & 1u) ? DECIDE_HAPPY :
                                                        DECIDE_NEUTRAL;
      output->frame.duration_ms = DECIDE_ANSWER_MS;
      output->haptic = 1;
      state->answer_until = elapsed + DECIDE_ANSWER_MS;
      state->idle_shown = UINT32_MAX;
      state->answers++;
      return 0;
    }

  /* Refresh the waiting face before its display intent expires. */

  if (elapsed >= state->answer_until &&
      (state->idle_shown == UINT32_MAX ||
       elapsed - state->idle_shown >= DECIDE_IDLE_MS))
    {
      output->frame.expression = DECIDE_THINKING;
      output->frame.duration_ms = DECIDE_IDLE_MS + 500u;
      state->idle_shown = elapsed;
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
    .abi = BKNATIVE_ABI_V2,
    .state_size = sizeof(struct decide_s),
    .start = decide_start,
    .step2 = decide_step
  };

  return &program;
}
