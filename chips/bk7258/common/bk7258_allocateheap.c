/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_allocateheap.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 Cortex-M33 heap allocation for NuttX N2.
 *
 * Modelled on the flat-build path of nuttx/arch/arm/src/mps/
 * mps_allocateheap.c.  The single user heap spans from the top of the IDLE
 * thread stack (g_idle_topstack, == _ebss + CONFIG_IDLETHREAD_STACKSIZE) up
 * to the end of usable SRAM (the linker-provided _eheap symbol).
 *
 * Resulting RAM layout (flat build), matching scripts/ld.script:
 *
 *   0x28000000  AP SMP spinlocks (official 64 KiB reserved region)
 *   0x28010000  g_intstackalloc  .irq_stack (CONFIG_ARCH_INTERRUPTSTACK)
 *   ...         .data / .bss
 *   _ebss       IDLE thread stack (CONFIG_IDLETHREAD_STACKSIZE)
 *   g_idle_topstack
 *   ...         heap  (grows up)
 *   _eheap      0x2809FFFC (one word below the 0x280A0000 SRAM top)
 *
 *   (The initial MSP at 0x2809FFFC is only used during __start before the
 *   scheduler takes over; after nx_start() tasks run on stacks allocated
 *   from this heap, so the transient overlap is harmless, exactly as in
 *   mps_start.c / mps_allocateheap.c.)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/nuttx.h>

#include <sys/types.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#include <arch/chip/bk7258_amp.h>

#include "arm_internal.h"

/****************************************************************************
 * External Function/Symbol Declarations
 ****************************************************************************/

/* End-of-RAM heap limit, exported by scripts/ld.script
 * (_eheap = ORIGIN(RAM) + LENGTH(RAM) - 4 = 0x2809fffc).
 */

extern unsigned char _eheap[];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   Return the start and size of the single (flat-build) user heap.
 *
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  /* Heap begins right above the IDLE thread stack... */

  *heap_start = (void *)g_idle_topstack;

  /* ...and extends up to the end of usable SRAM (_eheap, from ld.script). */

  *heap_size  = (size_t)((uintptr_t)_eheap - (uintptr_t)g_idle_topstack);
}

#if CONFIG_MM_REGIONS > 1
void arm_addregion(void)
{
  /* AP PSRAM hardware and its role-local allocator are not ready when the
   * common ARM up_initialize() hook runs.  bk7258_ap_main() adds the reserved
   * PSRAM system-heap block after bk7258_psram_initialize() succeeds.
   */
}
#endif
