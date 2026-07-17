/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_start.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, tri-core Cortex-M33) C reset entry for NuttX
 * Stage N1.
 *
 * GOAL (Stage N1, minimal):
 *   Prove that the NuttX build chain (linker script, vector table, app
 *   magic, postbuild CRC-expansion) produces an image that the on-board
 *   Tier-1 bootloader can validate and jump into.  When this __start runs,
 *   the hardware has already loaded MSP from vector slot [0]; we mask
 *   IRQs, relocate VTOR onto our flash-resident table, push the banner
 *   "NUTTX N1\r\n" out of UART1 the same way the verified probe does, and
 *   spin.  We deliberately do NOT call nx_start().
 *
 * WHAT IS INTENTIONALLY LEFT FOR STAGE N2:
 *   - .data copy from flash LMA and .bss zero-initialisation
 *   - arm_earlyserialinit() / console bringup
 *   - heap sizing, boardinitialize(), nx_start(), NSH
 *   N1 keeps .data/.bss empty (only static-const strings and locals), so
 *   skipping those loops is safe here.  N2 will insert the standard
 *   mps_start.c-style sequence immediately before the banner.
 *
 * Freestanding: no libc, no NuttX headers beyond config.h.  The UART1
 * addresses and the magic value are shared verbatim with
 * docs/bk7258-t5ai/probe/probe.c (board-verified 2026-07-15).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/* We intentionally avoid <stdint.h> here to stay freestanding; the build
 * is compiled with -nostdlib/-nostdinc++ equivalents for the libcless
 * boot path.  Use compiler-builtin unsigned types.
 */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Fixed memory-mapped registers / words (shared with probe.c). */

#define BK7258_SCB_VTOR          (*(volatile unsigned int *)0xe000ed08u)
#define BK7258_SCB_CPACR         (*(volatile unsigned int *)0xe000ed88u)

/* UART1 FIFO registers.  The Tier-1 bootloader already configured UART1
 * (pinmux, 26 MHz XTAL, clock gate, global_ctrl, config); we MUST NOT
 * touch UART1_CFG (0x45830010) because that would clash with the
 * bootloader's divider.  We only push bytes into fifo_port, exactly like
 * the Zephyr soc_reset_hook and our probe.
 */

#define BK7258_UART1_FIFO_STAT   (*(volatile unsigned int *)0x45830018u)
#define BK7258_UART1_FIFO_PORT   (*(volatile unsigned int *)0x4583001Cu)
#define BK7258_UART1_FIFO_READY  (1u << 20)

/* Our vector table lives at the very start of the app image, which the
 * bootloader maps at logical flash address 0x02010000.  Tell VTOR to
 * fetch exceptions from there.
 */

#define BK7258_VTOR_VALUE        0x02010000u

/* Idle-stack top symbol exported by the linker script.  The common ARM
 * code (arm_initialize, arm_getintstack) expects g_idle_topstack to exist
 * even though N1 never reaches nx_start; provide it as a read-only
 * constant so the link succeeds.  Value matches the canonical
 * "_ebss + CONFIG_IDLETHREAD_STACKSIZE" convention used by other CM33
 * chips (mps_start.c).  Because N1's .bss is empty, _ebss == _sbss ==
 * start of RAM (0x28000000) + an 0x0-length .data/.bss; the post-link
 * value is computed by ld and emitted into the const.
 */

extern unsigned int _ebss;

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Referenced by nuttx/arch/arm/src/common/arm_initialize.c via
 * up_get_idle_stack().  Defined here because N1 ships its own start file
 * instead of reusing mps_start.c.  Const so it lands in .rodata (flash).
 */

const unsigned int g_idle_topstack =
    (unsigned int)&_ebss + 2048;  /* CONFIG_IDLETHREAD_STACKSIZE=2048 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Polled UART1 output -- identical to probe.c. */

static void bk7258_uart_putc(unsigned char c)
{
  while ((BK7258_UART1_FIFO_STAT & BK7258_UART1_FIFO_READY) == 0)
    {
    }

  BK7258_UART1_FIFO_PORT = (unsigned int)(c & 0xffu);
}

static void bk7258_uart_puts(const char *s)
{
  while (*s)
    {
      bk7258_uart_putc((unsigned char)*s);
      s++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __start
 *
 * Description:
 *   Cortex-M reset entry.  Reached from vector slot [1] after the
 *   bootloader validated the BK7236 magic at image offset 0x100 and jumped
 *   in.  The hardware has already loaded MSP from slot [0]; we run with
 *   interrupts whatever the bootloader left them at -- first instruction
 *   masks them.
 *
 ****************************************************************************/

void __start(void)
{
  /* 1. Mask all interrupts immediately. */

  __asm volatile ("cpsid i");

  /* 2. Point VTOR at our flash-resident vector table (0x02010000).  The
   *    bootloader may or may not have set this; make it deterministic.
   *    Barrier so subsequent exception entry (if ever enabled in N2)
   *    observes the new VTOR.
   */

  BK7258_SCB_VTOR = BK7258_VTOR_VALUE;
  __asm volatile ("dsb; isb");

  /* 3. Enable CP10/CP11 (FPU) full access.  Cheap and matches the probe;
   *    avoids accidental traps if the toolchain emits FP prologues.
   */

  BK7258_SCB_CPACR = BK7258_SCB_CPACR | ((3u << 20) | (3u << 22));

  /* 4. Push the N1 banner.  This is the proof-of-life: if we see this
   *    string on UART1 (115200 8N1, inherited from the bootloader), the
   *    NuttX image was validated by the bootloader and __start executed.
   *
   *    N2 will replace this block with .data/.bss init +
   *    arm_earlyserialinit() + nx_start().
   */

  bk7258_uart_puts("NUTTX N1\r\n");

  /* 5. Halt forever.  N1 does not start the scheduler. */

  for (; ; )
    {
    }
}
