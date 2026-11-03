/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * chips/bk7258/include/bk7258_os_adapt.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_BK7258_OS_ADAPT_H
#define __ARCH_ARM_SRC_BK7258_BK7258_OS_ADAPT_H

#include <nuttx/config.h>

#include <stdint.h>

/* Machine-readable counters for SDK synchronization control blocks.  A
 * non-zero psram_rejections count means an SDK semaphore/mutex/queue/event
 * would otherwise have been placed in PSRAM on AP SMP and was refused.
 */

struct bk7258_os_sync_pool_stats_s
{
  uint32_t small_slots;
  uint32_t large_slots;
  uint32_t small_in_use;
  uint32_t large_in_use;
  uint32_t small_high_water;
  uint32_t large_high_water;
  uint32_t fallback_allocations;
  uint32_t oversize_requests;
  uint32_t psram_rejections;
  uint32_t small_base;
  uint32_t large_base;
};

#ifdef CONFIG_BK7258_SDK_SRAM_HEAP
int bk7258_os_sram_heap_initialize(void);
#endif

int bk7258_os_sync_pool_get_stats(
  struct bk7258_os_sync_pool_stats_s *stats);

#endif /* __ARCH_ARM_SRC_BK7258_BK7258_OS_ADAPT_H */
