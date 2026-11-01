/****************************************************************************
 * apps/bk7258/bk7258_runtime_skills_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_runtime_skills_store.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "bk7258_provision_store.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef BKRUNTIME_SKILLS_ROOT
#  define BKRUNTIME_SKILLS_ROOT "/cpdata/shaniu/runtime-skills"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void transaction(uint8_t value[16], uint64_t revision)
{
  memcpy(value, "RSK1", 4);
  for (int i = 11; i >= 4; i--)
    {
      value[i] = revision;
      revision >>= 8;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkruntime_skills_store_load(void *buffer, size_t capacity,
                               size_t *actual, uint64_t *revision)
{
  struct bkprov_store_s store;
  int ret;

  if (buffer == NULL || actual == NULL || revision == NULL ||
      capacity < BKRUNTIME_SKILLS_STORE_MAX)
    {
      return -EINVAL;
    }

  *actual = 0;
  *revision = 0;
  ret = bkprov_store_open(&store, BKRUNTIME_SKILLS_ROOT);
  if (ret == -ENOENT)
    {
      return 0;
    }

  if (ret < 0)
    {
      return ret;
    }

  ret = bkprov_store_load(&store, buffer, capacity, actual, revision, NULL);
  if (ret == -ENOSPC)
    {
      return -EBADMSG;
    }

  if (ret == 0 && (*actual == 0 || *actual > BKRUNTIME_SKILLS_STORE_MAX))
    {
      return -EBADMSG;
    }

  return ret;
}

int bkruntime_skills_store_commit(uint64_t expected, const void *buffer,
                                 size_t size, uint64_t *revision)
{
  struct bkprov_store_s store;
  uint8_t record[16] =
  {
    0
  };

  int ret;

  if (buffer == NULL || revision == NULL || size == 0 ||
      size > BKRUNTIME_SKILLS_STORE_MAX || expected == UINT64_MAX)
    {
      return -EINVAL;
    }

  *revision = 0;
  if (mkdir(BKRUNTIME_SKILLS_ROOT, 0700) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  ret = bkprov_store_open(&store, BKRUNTIME_SKILLS_ROOT);
  if (ret < 0)
    {
      return ret;
    }

  transaction(record, expected + 1);
  ret = bkprov_store_commit(&store, expected, record, buffer, size);
  if (ret == 0)
    {
      *revision = expected + 1;
    }

  return ret;
}
