/****************************************************************************
 * apps/bk7258/bk7258_memory_facts.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_MEMORY_FACTS_H
#define __APP_BK7258_MEMORY_FACTS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stddef.h>
#include <stdint.h>

#define BKMEMORY_FACT_SLOTS 3u
#define BKMEMORY_FACT_KEY 24u
#define BKMEMORY_FACT_VALUE 96u

struct bkmemory_fact_s
{
  char key[BKMEMORY_FACT_KEY + 1];
  char value[BKMEMORY_FACT_VALUE + 1];
};

struct bkmemory_facts_s
{
  struct bkmemory_fact_s slot[BKMEMORY_FACT_SLOTS];
};

int bkmemory_facts_apply(struct bkmemory_facts_s *facts,
                         const char *json, uint64_t revision,
                         uint64_t *expected);
int bkmemory_facts_extract(struct bkmemory_facts_s *facts,
                           const char *text);
int bkmemory_facts_json(const struct bkmemory_facts_s *facts,
                        uint64_t revision, int enabled, char *json,
                        size_t capacity);
int bkmemory_facts_encode(const struct bkmemory_facts_s *facts,
                          uint8_t *data, size_t capacity, size_t *used);
int bkmemory_facts_decode(struct bkmemory_facts_s *facts,
                          const uint8_t *data, size_t size, size_t *used);

#endif
