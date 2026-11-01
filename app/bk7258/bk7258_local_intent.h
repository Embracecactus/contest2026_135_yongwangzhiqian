/****************************************************************************
 * apps/bk7258/bk7258_local_intent.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef BK7258_LOCAL_INTENT_H
#define BK7258_LOCAL_INTENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bklocal_intent_kind_e
{
  BKLOCAL_NONE = 0,
  BKLOCAL_VOLUME,
  BKLOCAL_EYES,
  BKLOCAL_STOP
};

struct bklocal_intent_s
{
  enum bklocal_intent_kind_e kind;
  int value;
  bool relative;
  const char *expression;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Accepts only one complete, explicit local command.  A miss leaves intent
 * unchanged so its caller can pass the utterance to the existing Agent path.
 */

int bklocal_intent_parse(const char *text, struct bklocal_intent_s *intent);

#endif
