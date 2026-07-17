/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_vectors.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, tri-core Cortex-M33) Cortex-M vector table for
 * NuttX Stage N1.
 *
 * Why a chip-provided vector table (instead of the shared
 * nuttx/arch/arm/src/arm_m/arm_vectors.c)?
 *
 *   The on-board Tier-1 bootloader (board/bk7258_t5ai/bootloader) reads a
 *   64-bit app magic "BK7236\0\0" from image byte offset 0x100 before it
 *   validates-and-jumps to the app Reset entry.  Image offset 0x100 ==
 *   vector slot index 64 (0x100 / 4).  The standard NuttX table is only
 *   16 + NR_IRQS entries long (here 16 + 48 = 64, i.e. slots [0..63]),
 *   so we must extend the table with two extra magic words at [64]/[65].
 *
 *   The clean way to do that is to select ARCH_HAVE_CUSTOM_VECTORS at the
 *   chip Kconfig level (which makes armv8-m/arm_m drop arm_vectors.c) and
 *   provide this file instead.  The symbol name _vectors and the section
 *   attribute .vectors match what arm_vectors.c and the linker scripts
 *   already expect.
 *
 * Layout (66 entries, 0x108 bytes total; the .vectors section is pinned
 * at flash origin 0x02010000 by scripts/ld.script):
 *
 *   [0]      0x2809FFFC   initial MSP (top of 0x28000000..0x2809FFFF SRAM)
 *   [1]      __start      Reset entry (Thumb; toolchain sets bit0)
 *   [2..15]  14 system exception handlers (all default-spin)
 *   [16..63] 48 external IRQ handlers      (all default-spin)
 *   [64]     0x32374B42   app magic word 0: "BK72" little-endian
 *   [65]     0x00003633   app magic word 1: "36\0\0" little-endian
 *
 * Entry [64] sits at byte offset 0x100 -- exactly what the bootloader
 * validates.  This layout is shared verbatim with the bare-metal probe
 * (docs/bk7258-t5ai/probe/probe.c) that has already been boot-verified
 * on the T5-AI board.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Top of usable SRAM (0x28000000 + 0xA0000 = 0x280A0000; keep the last
 * word free for safety, matching the probe).  This is what the hardware
 * loads into MSP from slot [0] before jumping to slot [1].
 */

#define BK7258_INITIAL_MSP              0x2809fffcu

/* App magic, little-endian.  'B''K''7''2' | '3''6''\0''\0'.
 * Verified against board/bootloader behaviour; see probe.c.
 */

#define BK7258_APP_MAGIC_WORD0          0x32374b42u   /* "BK72" */
#define BK7258_APP_MAGIC_WORD1          0x00003633u   /* "36\0\0" */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* All unused exception/IRQ slots spin forever.  N1 never enables
 * interrupts, so the only entry that ever runs is slot [1] (__start).
 */

static void bk7258_default_handler(void)
{
  for (; ; )
    {
    }
}

/* Chip entry point (defined in bk7258_start.c).  Declared here so we can
 * store its address into vector slot [1].  NuttX expects the symbol to be
 * called __start (see nuttx/arch/arm/src/arm_m/arm_vectors.c).
 */

extern void __start(void);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* The NuttX arm_m layer looks up symbol "_vectors" and section ".vectors".
 * `used` prevents the compiler from dropping the table at LTO; `aligned(4)`
 * keeps each 4-byte slot naturally aligned.
 *
 * We size the table explicitly at 66 entries rather than relying on the
 * [2 ... N] designated-initializer range trick that arm_vectors.c uses,
 * so the two magic words at the end are unmistakable in the source and
 * in any hex dump.
 */

__attribute__((section(".vectors"), used, aligned(4)))
const void *const _vectors[66] =
{
  /* [0] */ (void *)BK7258_INITIAL_MSP,
  /* [1] */ (void *)__start,

  /* [2..15] system exception handlers (14 slots) */
  /* [2]  */ (void *)bk7258_default_handler,   /* NMI */
  /* [3]  */ (void *)bk7258_default_handler,   /* HardFault */
  /* [4]  */ (void *)bk7258_default_handler,   /* MemManage */
  /* [5]  */ (void *)bk7258_default_handler,   /* BusFault */
  /* [6]  */ (void *)bk7258_default_handler,   /* UsageFault */
  /* [7]  */ (void *)bk7258_default_handler,   /* SecureFault (ARMv8-M) */
  /* [8]  */ (void *)bk7258_default_handler,   /* Reserved */
  /* [9]  */ (void *)bk7258_default_handler,   /* Reserved */
  /* [10] */ (void *)bk7258_default_handler,   /* Reserved */
  /* [11] */ (void *)bk7258_default_handler,   /* SVCall */
  /* [12] */ (void *)bk7258_default_handler,   /* DebugMonitor */
  /* [13] */ (void *)bk7258_default_handler,   /* Reserved */
  /* [14] */ (void *)bk7258_default_handler,   /* PendSV */
  /* [15] */ (void *)bk7258_default_handler,   /* SysTick */

  /* [16..63] external IRQ handlers (48 slots).  N1 never un-masks IRQs,
   * so every slot points at the default spin loop.
   */
  /* [16] */ (void *)bk7258_default_handler,
  /* [17] */ (void *)bk7258_default_handler,
  /* [18] */ (void *)bk7258_default_handler,
  /* [19] */ (void *)bk7258_default_handler,
  /* [20] */ (void *)bk7258_default_handler,
  /* [21] */ (void *)bk7258_default_handler,
  /* [22] */ (void *)bk7258_default_handler,
  /* [23] */ (void *)bk7258_default_handler,
  /* [24] */ (void *)bk7258_default_handler,
  /* [25] */ (void *)bk7258_default_handler,
  /* [26] */ (void *)bk7258_default_handler,
  /* [27] */ (void *)bk7258_default_handler,
  /* [28] */ (void *)bk7258_default_handler,
  /* [29] */ (void *)bk7258_default_handler,
  /* [30] */ (void *)bk7258_default_handler,
  /* [31] */ (void *)bk7258_default_handler,
  /* [32] */ (void *)bk7258_default_handler,
  /* [33] */ (void *)bk7258_default_handler,
  /* [34] */ (void *)bk7258_default_handler,
  /* [35] */ (void *)bk7258_default_handler,
  /* [36] */ (void *)bk7258_default_handler,
  /* [37] */ (void *)bk7258_default_handler,
  /* [38] */ (void *)bk7258_default_handler,
  /* [39] */ (void *)bk7258_default_handler,
  /* [40] */ (void *)bk7258_default_handler,
  /* [41] */ (void *)bk7258_default_handler,
  /* [42] */ (void *)bk7258_default_handler,
  /* [43] */ (void *)bk7258_default_handler,
  /* [44] */ (void *)bk7258_default_handler,
  /* [45] */ (void *)bk7258_default_handler,
  /* [46] */ (void *)bk7258_default_handler,
  /* [47] */ (void *)bk7258_default_handler,
  /* [48] */ (void *)bk7258_default_handler,
  /* [49] */ (void *)bk7258_default_handler,
  /* [50] */ (void *)bk7258_default_handler,
  /* [51] */ (void *)bk7258_default_handler,
  /* [52] */ (void *)bk7258_default_handler,
  /* [53] */ (void *)bk7258_default_handler,
  /* [54] */ (void *)bk7258_default_handler,
  /* [55] */ (void *)bk7258_default_handler,
  /* [56] */ (void *)bk7258_default_handler,
  /* [57] */ (void *)bk7258_default_handler,
  /* [58] */ (void *)bk7258_default_handler,
  /* [59] */ (void *)bk7258_default_handler,
  /* [60] */ (void *)bk7258_default_handler,
  /* [61] */ (void *)bk7258_default_handler,
  /* [62] */ (void *)bk7258_default_handler,
  /* [63] */ (void *)bk7258_default_handler,

  /* [64] app magic word 0: "BK72" little-endian.  Byte offset 0x100. */
  /* [64] */ (void *)BK7258_APP_MAGIC_WORD0,
  /* [65] app magic word 1: "36\0\0" little-endian. Byte offset 0x104. */
  /* [65] */ (void *)BK7258_APP_MAGIC_WORD1
};
