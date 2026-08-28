/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/nuttx/kmalloc.h
 *
 * Host shim for the NuttX kernel allocator surface used by the AP board
 * helpers.  kmm_memalign() returns an allocation whose base is a multiple
 * of `alignment`; the host implementation uses posix_memalign so the
 * returned pointer is valid for kmm_free().  The implementation lives in
 * mock_nuttx_ap.c.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_KMALLOC_H
#define __MOCK_NUTTX_KMALLOC_H

#include <stddef.h>

#ifndef FAR
#define FAR
#endif

FAR void *kmm_memalign(size_t alignment, size_t size);
void kmm_free(FAR void *ptr);

#endif /* __MOCK_NUTTX_KMALLOC_H */
