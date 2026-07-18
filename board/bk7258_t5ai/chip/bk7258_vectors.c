/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_vectors.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, tri-core Cortex-M33) Cortex-M vector table for
 * NuttX Stage N2.
 *
 * Stage N1 shipped every non-reset slot pointing at a spin loop, which is
 * fine when __start never calls nx_start().  Stage N2 drives the full NuttX
 * boot (scheduler, SysTick, IRQ dispatch), so the table MUST route the
 * system-exception and external-IRQ slots through the standard armv8-m
 * dispatch stubs:
 *
 *   slots [2..14]  -> exception_common   (NMI, HardFault, ..., SVCall,
 *                                         DebugMonitor, PendSV)
 *   slots [15..63] -> exception_direct   (SysTick + 48 external IRQs)
 *
 * Both symbols live in the common armv8-m/arm_m source set that
 * chip/Make.defs pulls in via `include armv8-m/Make.defs`
 * (arm_exception.S provides exception_common, arm_doirq.c provides
 * exception_direct).  The actual handlers (arm_hardfault, arm_svcall,
 * systick_interrupt, ...) are attached at runtime via irq_attach() and
 * dispatched through g_irqvector[] by arm_doirq(), exactly as every other
 * in-tree Cortex-M chip does -- they are NOT placed in the vector table
 * directly.  This is the layout the standard nuttx/arch/arm/src/arm_m/
 * arm_vectors.c uses; we only replicate it so the BK7236 app magic can be
 * appended after slot 63.
 *
 * Why a chip-provided vector table (instead of the shared arm_vectors.c):
 *
 *   The on-board Tier-1 bootloader reads a 64-bit app magic "BK7236\0\0"
 *   from image byte offset 0x100 before it validates-and-jumps to the app
 *   Reset entry.  Image offset 0x100 == vector slot index 64 (0x100 / 4).
 *   The standard NuttX table is only 16 + NR_IRQS entries long (here
 *   16 + 48 = 64, i.e. slots [0..63]), so we must extend the table with two
 *   extra magic words at [64]/[65].  We therefore select
 *   ARCH_HAVE_CUSTOM_VECTORS at the chip Kconfig level (which makes
 *   armv8-m/arm_m drop arm_vectors.c) and provide this file instead.
 *
 * Layout (66 entries, 0x108 bytes total; the .vectors section is pinned
 * at flash origin 0x02010000 by scripts/ld.script):
 *
 *   [0]      0x2809FFFC   initial MSP (top of 0x28000000..0x2809FFFF SRAM)
 *   [1]      __start      Reset entry (Thumb; toolchain sets bit0)
 *   [2..14]  exception_common (13 system exception slots)
 *   [15..63] exception_direct (SysTick + 48 external IRQ slots)
 *   [64]     0x32374B42   app magic word 0: "BK72" little-endian
 *   [65]     0x00003633   app magic word 1: "36\0\0" little-endian
 *
 * Entry [64] sits at byte offset 0x100 -- exactly what the bootloader
 * validates.  This layout is shared verbatim with the bare-metal probe
 * (docs/bk7258-t5ai/probe/probe.c) that has already been boot-verified
 * on the T5-AI board.  Slots [0], [1], [64], [65] are UNCHANGED from N1
 * (the bootloader's hard requirements); only the dispatcher slots in the
 * middle were upgraded from spin loops to the real NuttX handlers.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "chip.h"
#include "arm_internal.h"
#include "ram_vectors.h"
#include "nvic.h"

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

/* UART1 FIFO registers -- bare-MMIO path identical to bk7258_start.c's
 * bk7258_early_putc.  Duplicated here (vectors.c does not include
 * bk7258_start.c, which keeps bk7258_early_putc file-static).  Used only
 * by the diagnostic HardFault/NMI handler below.  Touches only MMIO, so it
 * is safe from a faulted exception context (no .data/.bss dependency).
 */

#define BK7258_FAULT_UART1_FIFO_STAT    (*(volatile unsigned int *)0x45830018u)
#define BK7258_FAULT_UART1_FIFO_PORT    (*(volatile unsigned int *)0x4583001Cu)
#define BK7258_FAULT_UART1_FIFO_READY   (1u << 20)

/* Standard armv8-m exception dispatch entrypoints (assembly / C stubs that
 * save context, decode the IRQ number from IPSR, call arm_doirq(), and on
 * return perform a context switch if needed).  Provided by the common
 * armv8-m source set.
 */

extern void exception_common(void);
extern void exception_direct(void);

/* Chip entry point (defined in bk7258_start.c).  Slot [1] keeps pointing
 * at __start, exactly as N1 / the probe; NuttX expects the symbol to be
 * called __start (see nuttx/arch/arm/src/arm_m/arm_vectors.c).
 */

extern void __start(void);

/* Diagnostic HardFault/NMI handler (defined below).  Replaces the silent
 * exception_common spin on slots [2] (NMI) and [3] (HardFault) only, so a
 * trap during early boot is visible on UART1.  Pure diagnostic: it prints
 * "HF" and parks; it does NOT decode the fault frame and does NOT recover.
 */

void bk7258_hardfault_handler(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare polled UART1 output -- freestanding, MMIO-only, identical to the
 * bk7258_early_putc() in bk7258_start.c.  Duplicated here because vectors.c
 * does not include bk7258_start.c (whose bk7258_early_putc is file-static)
 * and because the fault handler must not depend on any .data/.bss state.
 */

static void bk7258_fault_putc(unsigned char c)
{
  while ((BK7258_FAULT_UART1_FIFO_STAT & BK7258_FAULT_UART1_FIFO_READY) == 0)
    {
    }

  BK7258_FAULT_UART1_FIFO_PORT = (unsigned int)(c & 0xffu);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Diagnostic HardFault/NMI handler.  Pushes "HF" out over UART1 using the
 * same bare-MMIO write as the N2 marker, then parks forever.  NO fault
 * frame decode, NO recovery, NO lockup-safe spin -- pure visibility for
 * board-side bring-up.  Wired into vector slots [2] (NMI) and [3]
 * (HardFault) so either a faulting instruction or an escalated exception
 * becomes observable; all other system-exception slots ([4..14]) keep
 * routing through exception_common.
 */

void bk7258_hardfault_handler(void)
{
  bk7258_fault_putc('H');
  bk7258_fault_putc('F');
  for (; ; )
    {
    }
}

/* DIAGNOSTIC SysTick probe (temporary).  Wired into slot [15] INSTEAD of
 * exception_direct so we can tell whether the SysTick exception ENTRY itself
 * works on this board.  Prints 's', disables the SysTick interrupt (clears
 * SYST_CSR.TICKINT so it does not re-fire), and returns normally (a plain
 * exception return).  If 's' shows on UART1, the CPU entered the handler =>
 * exception entry/return are fine and the real hang is inside NuttX's
 * dispatch (exception_direct -> arm_doirq -> systick_interrupt) or the
 * PendSV context switch.  If 's' does NOT appear, exception ENTRY is broken
 * (vector fetch / VTOR / stack / fault on entry).  Pure diagnostic; revert.
 */
void bk7258_systick_probe(void)
{
  bk7258_fault_putc('s');
  *(volatile unsigned int *)0xE000E010u &= ~(1u << 1);  /* SYST_CSR.TICKINT off */
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* The NuttX arm_m layer looks up symbol "_vectors" and section ".vectors".
 * `used` prevents the compiler from dropping the table at LTO; `aligned(4)`
 * keeps each 4-byte slot naturally aligned.
 *
 * The table is sized explicitly at 66 entries so the two app-magic words at
 * the end are unmistakable in the source and in any hex dump, and so the
 * array extends to byte offset 0x108 (slot [65] at 0x104).
 *
 * GCC range designated initializers fill the dispatcher slots the same way
 * arm_vectors.c does; the magic slots [64]/[65] are then pinned explicitly
 * (they are pure data -- never indexed by the NVIC because the highest
 * supported IRQ is 63).
 */

__attribute__((section(".vectors"), used, aligned(4)))
const void *const _vectors[66] =
{
  /* [0]    initial MSP (loaded by hardware before __start). */
  [0]  = (void *)BK7258_INITIAL_MSP,

  /* [1]    Reset entry (Thumb; toolchain sets bit0). */
  [1]  = (void *)__start,

  /* [2..3] NMI + HardFault -> diagnostic bk7258_hardfault_handler.
   *   Replaces the silent exception_common spin on these two slots so a
   *   trap during early boot is visible (prints "HF" on UART1 then parks).
   *   NMI ([2]) is wired the same way so an escalated fault is observable.
   *   Pure diagnostic -- no frame decode, no recovery.
   */
  [2]  = &bk7258_hardfault_handler,
  [3]  = &bk7258_hardfault_handler,

  /* [4..14] remaining system exceptions -> exception_common
   *   4=MemManage 5=BusFault 6=UsageFault 7=SecureFault 8..10=Reserved
   *   11=SVCall 12=DebugMonitor 13=Reserved 14=PendSV
   */
  [4 ... 14]  = &exception_common,

  /* [15..63] SysTick + 48 external IRQs -> exception_direct
   *   15=SysTick 16..63=BK7258 external IRQs 0..47
   */
  [15 ... 63] = &exception_direct,

  /* [64] app magic word 0: "BK72" little-endian.  Byte offset 0x100. */
  [64] = (void *)BK7258_APP_MAGIC_WORD0,

  /* [65] app magic word 1: "36\0\0" little-endian. Byte offset 0x104. */
  [65] = (void *)BK7258_APP_MAGIC_WORD1
};
