/****************************************************************************
 * chips/bk7258/common/bk7258_allocateheap.c
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
#include <arch/chip/bk7258_smp_placement.h>

#include "arm_internal.h"

/****************************************************************************
 * External Function/Symbol Declarations
 ****************************************************************************/

/* End-of-RAM heap limit, exported by scripts/ld.script
 * (_eheap = ORIGIN(RAM) + LENGTH(RAM) - 4 = 0x2809fffc).
 */

extern unsigned char _eheap[];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MM_KERNEL_HEAP
/* Kernel heap occupies [start, end) at the top of the SRAM heap range; the
 * user heap ends exactly at start, so the two never overlap.
 */

static uintptr_t bk7258_kheap_end(void)
{
  return (uintptr_t)_eheap & ~(uintptr_t)7u;
}

static uintptr_t bk7258_kheap_start(void)
{
  return (bk7258_kheap_end() - CONFIG_MM_KERNEL_HEAPSIZE) &
         ~(uintptr_t)7u;
}
#endif

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

#ifdef CONFIG_MM_KERNEL_HEAP
  /* The kernel heap takes the top CONFIG_MM_KERNEL_HEAPSIZE bytes of the
   * same SRAM range (see up_allocate_kheap()); the user heap keeps the rest
   * and later receives the PSRAM regions.
   */

  DEBUGASSERT(bk7258_kheap_start() > (uintptr_t)g_idle_topstack);
  *heap_size = (size_t)(bk7258_kheap_start() -
                        (uintptr_t)g_idle_topstack);
#endif
}

#ifdef CONFIG_MM_KERNEL_HEAP
/****************************************************************************
 * Name: up_allocate_kheap
 *
 * Description:
 *   Return an internal-SRAM-only kernel heap.  TCBs, task groups, kernel
 *   thread stacks and other kmm_*() objects that carry SMP atomic state must
 *   never be satisfied from PSRAM; bk7258_psram_add_system_heap() adds PSRAM
 *   only through kumm_addregion(), so this heap stays SRAM-only.
 *
 ****************************************************************************/

void up_allocate_kheap(void **heap_start, size_t *heap_size)
{
  uintptr_t end = bk7258_kheap_end();
  uintptr_t start = bk7258_kheap_start();

  DEBUGASSERT(start > (uintptr_t)g_idle_topstack);
  DEBUGASSERT(!bk7258_smp_addr_is_psram((void *)start) &&
              !bk7258_smp_addr_is_psram((void *)(end - 1u)));

  *heap_start = (void *)start;
  *heap_size  = (size_t)(end - start);
}
#endif

#if CONFIG_MM_REGIONS > 1
void arm_addregion(void)
{
  /* PSRAM hardware and its role-local allocator are not ready when the
   * common ARM up_initialize() hook runs.  The role-specific late-init path
   * adds the reserved PSRAM system-heap block after PSRAM initialization
   * succeeds.
   */
}
#endif
