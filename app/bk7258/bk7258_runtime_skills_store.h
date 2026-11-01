/****************************************************************************
 * apps/bk7258/bk7258_runtime_skills_store.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_RUNTIME_SKILLS_STORE_H
#define __APP_BK7258_RUNTIME_SKILLS_STORE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKRUNTIME_SKILLS_STORE_MAX 8192u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bkruntime_skills_store_load(void *buffer, size_t capacity,
                               size_t *actual, uint64_t *revision);
int bkruntime_skills_store_commit(uint64_t expected, const void *buffer,
                                 size_t size, uint64_t *revision);

#endif
