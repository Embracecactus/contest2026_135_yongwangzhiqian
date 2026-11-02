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

/* ABI 1: display-only frames.  ABI 2: adds declared, product-arbitrated
 * input events and feedback requests.  Both share the entry symbol and the
 * program header; abi selects which step member is valid.
 */

#define BKNATIVE_ABI 1u
#define BKNATIVE_ABI_V2 2u
#define BKNATIVE_STATE_MAX 256u

/* ABI 2 input events (requires the "motion" permission).  Each bit reports
 * that the product's existing motion classifier saw the action at least once
 * since the previous step.  These are classifier candidates, not gestures.
 */

#define BKNATIVE_EVENT_MOVED (1u << 0)
#define BKNATIVE_EVENT_SETTLED (1u << 1)
#define BKNATIVE_EVENT_TILTED (1u << 2)
#define BKNATIVE_EVENT_ALL 7u

/* ABI 2 completion cue (requires the "sound" permission).  It is honored
 * only on the step that returns 1, after the program has been unloaded.
 */

#define BKNATIVE_CUE_NONE 0u
#define BKNATIVE_CUE_RHYTHM 1u

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

struct bknative_input_s
{
  uint32_t events;   /* BKNATIVE_EVENT_* bits; 0 without "motion". */
  uint32_t random;   /* Fresh non-cryptographic value for every step. */
};

struct bknative_output_s
{
  struct bknative_frame_s frame;

  /* 1 requests one short pulse ("haptic").  The product applies its own
   * duration, cooldown, battery, motor-fault and voice gates; a request is
   * a hint that may be dropped, never queued.
   */

  uint32_t haptic;
  uint32_t cue;      /* BKNATIVE_CUE_*; see above. */
};

typedef int (*bknative_step_t)(void *state, uint32_t elapsed_ms,
                               struct bknative_frame_s *frame);
typedef int (*bknative_step2_t)(void *state, uint32_t elapsed_ms,
                                const struct bknative_input_s *input,
                                struct bknative_output_s *output);

struct bknative_program_s
{
  uint32_t abi;
  uint32_t state_size;
  /* Cooperative callbacks must return promptly without imports, allocation,
   * threads, hardware access or retaining pointers outside their state.
   * The manager measures every callback and aborts one that overruns.
   * start: 0 accepted, negative error.
   * step/step2: 0 active, 1 done, negative error.
   */

  int (*start)(void *state, uint32_t argument);
  union
  {
    bknative_step_t step;     /* abi == BKNATIVE_ABI */
    bknative_step2_t step2;   /* abi == BKNATIVE_ABI_V2 */
  };
};

typedef const struct bknative_program_s *(*bknative_entry_t)(void);

#endif
