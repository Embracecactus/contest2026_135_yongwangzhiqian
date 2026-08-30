/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/nuttx/cache.h
 *
 * Host shim for the NuttX AON/CPU data-cache maintenance surface used by
 * the AP board helpers.  The implementation lives in mock_nuttx_ap.c and
 * records every cleaned/flushed/invalidated range so tests can assert the
 * exact spans the driver requests.
 *
 * up_get_dcache_linesize() returns the value installed by
 * mock_cache_set_linesize(); the default 0 mirrors the maintained AP
 * handoff, which disables D-cache and maps DMA SRAM non-cacheable.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_CACHE_H
#define __MOCK_NUTTX_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define MOCK_CACHE_LOG_MAX 32

struct mock_cache_log_s
{
  uintptr_t start[MOCK_CACHE_LOG_MAX];
  uintptr_t end[MOCK_CACHE_LOG_MAX];
  size_t count;
};

/* NuttX ABI. */
size_t up_get_dcache_linesize(void);
void up_clean_dcache(uintptr_t start, uintptr_t end);
void up_flush_dcache(uintptr_t start, uintptr_t end);
void up_invalidate_dcache(uintptr_t start, uintptr_t end);

/* Test control. */
void mock_cache_set_linesize(size_t linesize);

extern struct mock_cache_log_s g_mock_cache_clean;
extern struct mock_cache_log_s g_mock_cache_flush;
extern struct mock_cache_log_s g_mock_cache_invalidate;

#endif /* __MOCK_NUTTX_CACHE_H */
