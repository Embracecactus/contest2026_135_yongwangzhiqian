/****************************************************************************
 * apps/bk7258/native-apps/nudge.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/* "Nudge": a short reminder, normally started by the schedule (for example
 * a daily drink-water time).  It shows a happy face and requests a pulse at
 * 0, 20 and 40 seconds.  Picking up or tilting the device completes it with
 * result 1; no response within the run seconds completes with result 2.
 *
 * Saved state (16 bytes) keeps today's completions and a daily streak.  The
 * streak needs trusted local time ("clock"); without it only the total
 * count advances.
 *
 * Manifest: abi 3, permissions display,motion,haptic,clock,storage,
 * results ["完成", "错过"].
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_abi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NUDGE_PULSE_MS 20000u
#define NUDGE_PULSES 3u
#define NUDGE_FACE_MS 2500u
#define NUDGE_HAPPY 1u
#define NUDGE_THINKING 2u
#define NUDGE_DONE 1u
#define NUDGE_MISSED 2u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct nudge_saved_s
{
  uint32_t day;           /* local day of count_today, or unknown */
  uint32_t count_today;
  uint32_t streak;        /* consecutive local days with a completion */
  uint32_t total;
};

struct nudge_s
{
  struct nudge_saved_s saved;  /* first: loaded/saved by the manager */
  uint32_t duration;
  uint32_t pulses;
  uint32_t face_shown;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int nudge_start(void *memory, uint32_t argument)
{
  struct nudge_s *state = memory;
  state->duration = argument * 1000u;
  state->pulses = 0;
  state->face_shown = UINT32_MAX;
  return 0;
}

static void nudge_record(struct nudge_saved_s *saved, uint32_t today)
{
  saved->total++;
  if (today == BKNATIVE_TIME_UNKNOWN)
    {
      return;
    }

  if (saved->day == today)
    {
      saved->count_today++;
      return;
    }

  saved->streak = saved->day != BKNATIVE_TIME_UNKNOWN &&
                  saved->day + 1u == today && saved->count_today ?
                  saved->streak + 1u : 1u;
  saved->day = today;
  saved->count_today = 1;
}

static int nudge_step(void *memory, uint32_t elapsed,
                      const struct bknative_input3_s *input,
                      struct bknative_output3_s *output)
{
  struct nudge_s *state = memory;

  if (state->pulses &&
      (input->base.events & (BKNATIVE_EVENT_MOVED | BKNATIVE_EVENT_TILTED)))
    {
      nudge_record(&state->saved, input->day);
      output->result = NUDGE_DONE;
      return 1;
    }

  if (elapsed >= state->duration)
    {
      output->result = NUDGE_MISSED;
      return 1;
    }

  output->base.frame.progress = elapsed / (state->duration / 100u + 1u);
  if (state->pulses < NUDGE_PULSES &&
      elapsed >= state->pulses * NUDGE_PULSE_MS)
    {
      state->pulses++;
      output->base.haptic = 1;
      output->base.frame.expression = NUDGE_HAPPY;
      output->base.frame.duration_ms = NUDGE_FACE_MS;
      state->face_shown = elapsed;
      return 0;
    }

  if (elapsed - state->face_shown >= NUDGE_FACE_MS - 500u)
    {
      output->base.frame.expression = NUDGE_THINKING;
      output->base.frame.duration_ms = NUDGE_FACE_MS;
      state->face_shown = elapsed;
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
    .state_size = sizeof(struct nudge_s),
    .start = nudge_start,
    .step3 = nudge_step,
    .saved_size = sizeof(struct nudge_saved_s)
  };

  return &program;
}
