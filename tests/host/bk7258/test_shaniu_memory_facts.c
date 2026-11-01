/****************************************************************************
 * apps/bk7258/test_shaniu_memory_facts.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_memory_facts.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static size_t g_remaining;
static size_t g_live;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void *limited_malloc(size_t size)
{
  void *value;

  if (g_remaining == 0)
    {
      return NULL;
    }

  g_remaining--;
  value = malloc(size);
  if (value != NULL)
    {
      g_live++;
    }

  return value;
}

static void counted_free(void *value)
{
  if (value != NULL)
    {
      assert(g_live > 0);
      g_live--;
    }

  free(value);
}

static void check_json_allocation_failure(struct bkmemory_facts_s *facts)
{
  cJSON_Hooks hooks =
  {
    limited_malloc, counted_free
  };

  char json[512];

  /* Fail each allocation before the output string is created. */

  cJSON_InitHooks(&hooks);
  for (size_t i = 0; i < 17; i++)
    {
      g_remaining = i;
      g_live = 0;
      assert(bkmemory_facts_json(facts, 7, 1, json, sizeof(json)) < 0);
      assert(g_live == 0);
    }

  cJSON_InitHooks(NULL);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  struct bkmemory_facts_s facts;
  struct bkmemory_facts_s restored;
  uint8_t data[512];
  size_t size;
  size_t used;
  uint64_t expected;
  char json[512];

  memset(&facts, 0, sizeof(facts));
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"value\":\"小山\",\"expected_revision\":\"0\"}",
         0, &expected) == 0);
  assert(!strcmp(facts.slot[0].key, "name"));
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"topic\",\"value\":\"天气\",\"expected_revision\":\"1\"}",
         0, &expected) == -ESTALE);
  assert(bkmemory_facts_encode(&facts, data, sizeof(data), &size) == 0);
  assert(bkmemory_facts_decode(&restored, data, size, &used) == 0);
  assert(used == size && !strcmp(restored.slot[0].value, "小山"));
  check_json_allocation_failure(&restored);
  assert(bkmemory_facts_json(&restored, 7, 1, json, sizeof(json)) == 0);
  assert(strstr(json, "\"revision\":\"7\"") != NULL);
  assert(bkmemory_facts_json(&restored, 7, 0, json, sizeof(json)) == 0);
  assert(strstr(json, "\"facts\":[]") != NULL);
  assert(bkmemory_facts_extract(&facts, "我喜欢蓝色") == 1);
  assert(bkmemory_facts_extract(&facts, "助手猜我喜欢红色") == 0);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"delete\",\"key\":\"name\",\"expected_revision\":\"0\"}",
         0, &expected) == 0);
  assert(!facts.slot[0].key[0]);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"value\":\"x\",\"expected_revision\":\"0\",\"extra\":1}",
         0, &expected) == -EINVAL);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"key\":\"topic\",\"value\":\"x\",\"expected_revision\":\"0\"}",
         0, &expected) == -EINVAL);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"value\":\"\\u0001\",\"expected_revision\":\"0\"}",
         0, &expected) == -EINVAL);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"value\":\"\\ud800\",\"expected_revision\":\"0\"}",
         0, &expected) == -EINVAL);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"value\":\"x\",\"expected_revision\":\"-0\"}",
         0, &expected) == -EINVAL);
  assert(bkmemory_facts_apply(&facts,
         "{\"op\":\"set\",\"key\":\"name\",\"value\":\"x\",\"expected_revision\":\" 0\"}",
         0, &expected) == -EINVAL);
  puts("MEMORY_FACTS_PASS");
  return 0;
}
