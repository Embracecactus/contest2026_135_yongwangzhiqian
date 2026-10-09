/****************************************************************************
 * chips/bk7258/include/bk7258_factory.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __CHIPS_BK7258_INCLUDE_BK7258_FACTORY_H
#define __CHIPS_BK7258_INCLUDE_BK7258_FACTORY_H

#include <stdint.h>

#define BK7258_FACTORY_REQUESTED  1u
#define BK7258_FACTORY_FORMATTING 2u
#define BK7258_FACTORY_READY      3u
#define BK7258_FACTORY_TOKEN_SIZE 32u

struct bk7258_factory_status_s
{
  uint32_t phase;
  uint32_t sequence;
  uint8_t transaction[16];
};

/* CP task context only. No runtime API creates a factory request. Only the
 * explicit host factory materializer writes the initial SFJ1 record.
 */
int bk7258_factory_status(struct bk7258_factory_status_s *status);
int bk7258_factory_advance(uint32_t expected, uint32_t next);

#endif
