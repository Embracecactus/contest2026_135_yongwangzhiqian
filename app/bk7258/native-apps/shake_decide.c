/****************************************************************************
 * apps/bk7258/native-apps/shake_decide.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/* "Shake to decide": waits with a thinking face for up to the run seconds.
 * The first movement reported by the product motion classifier shows a
 * happy (yes) or neutral (no) answer for three seconds with one haptic pulse
 * request, then completes with result 1 or 2.  No movement completes with
 * result 0.  The pulse is a request; the product gates decide if it is felt.
 *
 * Manifest: abi 3, permissions display,motion,haptic,
 * results ["去吧", "再想想"].
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
#define DECIDE_YES 1u
#define DECIDE_NO 2u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct decide_s
{
  uint32_t duration;
  uint32_t answer_until;
  uint32_t idle_shown;
  uint32_t answer;
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
  state->answer = 0;
  return 0;
}

static int decide_step(void *memory, uint32_t elapsed,
                       const struct bknative_input3_s *input,
                       struct bknative_output3_s *output)
{
  struct decide_s *state = memory;

  if (state->answer)
    {
      /* Keep the answer on screen for its full time, then report it. */

      if (elapsed >= state->answer_until)
        {
          output->result = state->answer;
          return 1;
        }

      return 0;
    }

  if (elapsed >= state->duration)
    {
      return 1;
    }

  output->base.frame.progress = elapsed / (state->duration / 100u + 1u);
  if (input->base.events & BKNATIVE_EVENT_MOVED)
    {
      state->answer = (input->base.random & 1u) ? DECIDE_YES : DECIDE_NO;
      output->base.frame.expression = state->answer == DECIDE_YES ?
                                      DECIDE_HAPPY : DECIDE_NEUTRAL;
      output->base.frame.duration_ms = DECIDE_ANSWER_MS;
      output->base.haptic = 1;
      state->answer_until = elapsed + DECIDE_ANSWER_MS;
      return 0;
    }

  /* Refresh the waiting face before its display intent expires. */

  if (state->idle_shown == UINT32_MAX ||
      elapsed - state->idle_shown >= DECIDE_IDLE_MS)
    {
      output->base.frame.expression = DECIDE_THINKING;
      output->base.frame.duration_ms = DECIDE_IDLE_MS + 500u;
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
    .abi = BKNATIVE_ABI_V3,
    .state_size = sizeof(struct decide_s),
    .start = decide_start,
    .step3 = decide_step,
    .saved_size = 0
  };

  return &program;
}
