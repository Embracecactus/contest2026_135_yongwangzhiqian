/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_vectors.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 tri-core Cortex-M33 Cortex-M vector table for
 * NuttX Stage A1.
 *
 * Stage A1 drives the full NuttX boot (scheduler, SysTick, IRQ dispatch)
 * with 64 external IRQs (NR_IRQS=80), so the table must route the
 * system-exception and external-IRQ slots through the full armv8-m context
 * saving path:
 *
 *   slots [2..3]   -> bk7258_hardfault_handler (CPU0 NMI/HardFault
 *                                               recorder)
 *   slots [4..63]  -> exception_common          (remaining exceptions,
 *                                               SysTick, lower external IRQs)
 *   slots [64]     -> BK7258_APP_MAGIC_WORD0    (boot magic, runtime-repaired)
 *   slots [65]     -> BK7258_APP_MAGIC_WORD1    (boot magic, runtime-repaired)
 *   slots [66..79] -> exception_common          (upper external IRQs)
 *
 * exception_common lives in the common armv8-m/arm_m source set that
 * chip/Make.defs pulls in via `include armv8-m/Make.defs`.  The actual
 * handlers (arm_svcall, systick_interrupt, UART ISR, ...) are attached at
 * runtime via irq_attach() and dispatched through g_irqvector[] by
 * arm_doirq(); they are not placed in the vector table directly.  We provide
 * this chip-specific table so the BK7236 app magic can be placed at the
 * fixed bootloader-validated slots [64]/[65].
 *
 * Why a chip-provided vector table (instead of the shared arm_vectors.c):
 *
 *   The on-board Tier-1 bootloader reads a 64-bit app magic "BK7236\0\0"
 *   from image byte offset 0x100 before it validates-and-jumps to the app
 *   Reset entry.  Image offset 0x100 == vector slot index 64 (0x100 / 4).
 *   With 64 external IRQs, the standard NuttX table is 16 + 64 = 80 entries
 *   (slots [0..79]), so the magic words at [64]/[65] are inside the table.
 *   We therefore select ARCH_HAVE_CUSTOM_VECTORS at the chip Kconfig level
 *   (which makes armv8-m/arm_m drop arm_vectors.c) and provide this file
 *   instead.  After VTOR switches to RAM, arm_ramvec_attach repairs slots
 *   64/65 to exception_common.
 *
 * Layout (80 entries, 0x140 bytes total; the .vectors section is pinned
 * at flash origin 0x02010000 by scripts/ld.script):
 *
 *   [0]      IDLE stack top       reset stack, later preserved as PSP
 *   [1]      bk7258_reset_entry   installs interrupt MSP, then enters __start
 *   [2..3]   bk7258_hardfault_handler (CPU0 fault recorder)
 *   [4..63]  exception_common (remaining exceptions, SysTick, lower external IRQs)
 *   [64]     0x32374B42   app magic word 0: "BK72" little-endian
 *   [65]     0x00003633   app magic word 1: "36\0\0" little-endian
 *   [66..79] exception_common (upper external IRQs 48..63)
 *
 * Entry [64] sits at byte offset 0x100 -- exactly what the bootloader
 * validates.  This layout is shared verbatim with the bare-metal probe
 * (docs/bk7258-t5ai/probe/probe.c) that has already been boot-verified
 * for the BK7258 boot contract.  Slots [64]/[65] keep the bootloader magic;
 * slots [0]/[1] now follow the standard NuttX PSP/MSP reset contract, while
 * dispatcher slots route through the real NuttX handlers.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>

#include <arch/chip/bk7258_amp.h>

#include "chip.h"
#include "arm_internal.h"
#include <components/system.h>
#include <arch/chip/bk7258_console.h>
#include "ram_vectors.h"
#include "nvic.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Match the common ARM reset contract.  Hardware first loads the IDLE stack
 * into MSP; the reset wrapper preserves it as PSP and moves handler mode to
 * the dedicated interrupt stack before entering __start().
 */

#define BK7258_CP_IDLE_STACK            \
  (_ebss + CONFIG_IDLETHREAD_STACKSIZE)

/* App magic, little-endian.  'B''K''7''2' | '3''6''\0''\0'.
 * Verified against board/bootloader behaviour; see probe.c.
 */

#define BK7258_APP_MAGIC_WORD0          0x32374b42u   /* "BK72" */
#define BK7258_APP_MAGIC_WORD1          0x00003633u   /* "36\0\0" */

/* The fault path uses only the compile-time selected UART MMIO address.  RTT
 * and NONE builds remain silent because an exception handler cannot safely
 * depend on the RTT control block or a UART whose pins it does not own.
 */

#ifdef BK7258_HAVE_UART_CONSOLE
#  define BK7258_FAULT_UART_FIFO_STAT \
  (*(volatile unsigned int *)BK7258_CONSOLE_FIFO_STATUS)
#  define BK7258_FAULT_UART_FIFO_PORT \
  (*(volatile unsigned int *)BK7258_CONSOLE_FIFO_PORT)
#endif

#define BK7258_SCB_CFSR                 (*(volatile uint32_t *)0xe000ed28u)
#define BK7258_SCB_HFSR                 (*(volatile uint32_t *)0xe000ed2cu)
#define BK7258_SCB_MMFAR                (*(volatile uint32_t *)0xe000ed34u)
#define BK7258_SCB_BFAR                 (*(volatile uint32_t *)0xe000ed38u)

#define BK7258_EXCEPTION_FRAME_WORDS    8u
#define BK7258_EXC_RETURN_THREAD_MODE   (1u << 3)
#define BK7258_EXC_RETURN_BASIC_FRAME   (1u << 4)
#define BK7258_FAULT_INVALID_VALUE      UINT32_MAX

#define BK7258_FAULT_AON_WDT_CTRL       (*(volatile uint32_t *)0x44000600u)
#define BK7258_FAULT_WDT_RESET_CTRL     (*(volatile uint32_t *)0x44800008u)
#define BK7258_FAULT_WDT_CTRL           (*(volatile uint32_t *)0x44800010u)
#define BK7258_FAULT_WDT_RESET_BIT      (1u << 1)
#define BK7258_FAULT_WDT_KEY1           (0x5au << 16)
#define BK7258_FAULT_WDT_KEY2           (0xa5u << 16)

/****************************************************************************
 * Permanent A1 compile-time invariants
 *
 * These assertions verify the A1 IRQ/vector definitions at compile time.
 * They use #ifdef / _Static_assert(0) branches so the gate fires if a
 * required macro is ever removed.
 *
 * Placement: after chip.h and nvic.h have exposed NR_IRQS,
 * ARMV8M_PERIPHERAL_INTERRUPTS, and NVIC priority macros.
 ****************************************************************************/

/* --- Core IRQ count gates --- */

#ifdef BK7258_EXTERNAL_IRQS
  _Static_assert(BK7258_EXTERNAL_IRQS == 64,
                 "A1 gate: BK7258_EXTERNAL_IRQS must be 64");
#else
  _Static_assert(0,
                 "A1 gate: BK7258_EXTERNAL_IRQS not defined; expected 64");
#endif

_Static_assert(NR_IRQS == 80,
               "A1 gate: NR_IRQS must be 80 (16 + 64 external)");

_Static_assert(ARMV8M_PERIPHERAL_INTERRUPTS == 64,
               "A1 gate: ARMV8M_PERIPHERAL_INTERRUPTS must be 64 (external IRQ count)");

/* --- Vector table sizing --- */

/* ARM_VECTAB_SIZE is defined by arch/arm/src/arm_m/ram_vectors.h as
 * (ARMV8M_PERIPHERAL_INTERRUPTS + NVIC_IRQ_FIRST), i.e. 64 + 16 = 80.
 * Assert its expected A1 value; conditional branch handles the case
 * where the common header has not yet been included.
 */

#ifdef ARM_VECTAB_SIZE
  _Static_assert(ARM_VECTAB_SIZE == 80,
                 "A1 gate: ARM_VECTAB_SIZE must be 80");
#else
  _Static_assert(0,
                 "A1 gate: ARM_VECTAB_SIZE not defined; expected 80");
#endif

#ifdef VECTAB_ALIGN
  _Static_assert(VECTAB_ALIGN == 512,
                 "A1 gate: VECTAB_ALIGN must be 512");
#else
  _Static_assert(0,
                 "A1 gate: VECTAB_ALIGN not defined; expected 512");
#endif

/* --- IRQ anchor gates (future A1 production names) --- */

#ifdef BK7258_IRQ_ETHERNET
  _Static_assert(BK7258_IRQ_ETHERNET == 64,
                 "A1 gate: ETHERNET must be NuttX IRQ 64");
#else
  _Static_assert(0,
                 "A1 gate: BK7258_IRQ_ETHERNET not defined; expected 64");
#endif

#ifdef BK7258_IRQ_SCALE0
  _Static_assert(BK7258_IRQ_SCALE0 == 65,
                 "A1 gate: SCALE0 must be NuttX IRQ 65");
#else
  _Static_assert(0,
                 "A1 gate: BK7258_IRQ_SCALE0 not defined; expected 65");
#endif

#ifdef BK7258_IRQ_MAILBOX
  _Static_assert(BK7258_IRQ_MAILBOX == 79,
                 "A1 gate: MAILBOX must be NuttX IRQ 79");
#else
  _Static_assert(0,
                 "A1 gate: BK7258_IRQ_MAILBOX not defined; expected 79");
#endif

/* --- Magic slot / offset structural gates --- */

#define BK7258_MAGIC_SLOT0      64      /* vector slot for magic word 0 */
#define BK7258_MAGIC_SLOT1      65      /* vector slot for magic word 1 */

_Static_assert(BK7258_MAGIC_SLOT0 * 4 == BK7258_MAGIC_BOOT0_OFFSET,
               "A1 gate: magic slot 64 must be at flash offset 0x100");
_Static_assert(BK7258_MAGIC_SLOT1 * 4 == BK7258_MAGIC_BOOT1_OFFSET,
               "A1 gate: magic slot 65 must be at flash offset 0x104");

/* Standard armv8-m exception dispatch entrypoints (assembly / C stubs that
 * save context, decode the IRQ number from IPSR, call arm_doirq(), and on
 * return perform a context switch if needed).  Provided by the common
 * armv8-m source set.
 */

extern void exception_common(void);

/* Chip C entry point (defined in bk7258_start.c).  Slot [1] enters the local
 * reset wrapper, which establishes PSP/MSP and then branches to __start.
 */

extern void __start(void);

/* Diagnostic HardFault/NMI entry (defined below).  Replaces the silent
 * exception_common spin on slots [2] (NMI) and [3] (HardFault), captures the
 * hardware frame in shared SRAM, prints the key registers, and then parks.
 */

void bk7258_hardfault_handler(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_reset_entry(void)
{
  /* Preserve the reset/IDLE stack as PSP and dedicate MSP to exceptions,
   * matching the common NuttX ARM reset wrapper and the AP image.
   */

  arm_initialize_stack();

  __asm volatile ("mov lr, %0\n\t"
                  "bx %1\n\t"
                  :
                  : "r"(0), "r"(__start));
}

static void bk7258_fault_putc(unsigned char c)
{
#ifdef BK7258_HAVE_UART_CONSOLE
  uint32_t count;

  for (count = 0; count < BK7258_UART_TX_POLL_LIMIT; count++)
    {
      if ((BK7258_FAULT_UART_FIFO_STAT & BK7258_UART_TX_READY) != 0)
        {
          break;
        }
    }

  BK7258_FAULT_UART_FIFO_PORT = (unsigned int)(c & 0xffu);
#else
  (void)c;
#endif
}

static void bk7258_fault_puthex(uint32_t value)
{
  static const char hex[] = "0123456789abcdef";
  int shift;

  for (shift = 28; shift >= 0; shift -= 4)
    {
      bk7258_fault_putc(hex[(value >> shift) & 0x0fu]);
    }
}

static void bk7258_fault_putfield(unsigned char tag, uint32_t value)
{
  bk7258_fault_putc(' ');
  bk7258_fault_putc(tag);
  bk7258_fault_putc('=');
  bk7258_fault_puthex(value);
}

static void bk7258_fault_stop_watchdogs(void)
{
  /* Stop both the bootloader AON watchdog and the NuttX automonitor watchdog.
   * Otherwise the parked diagnostic handler becomes another reset loop.
   */

  BK7258_FAULT_AON_WDT_CTRL = BK7258_FAULT_WDT_KEY1;
  BK7258_FAULT_AON_WDT_CTRL = BK7258_FAULT_WDT_KEY2;
  BK7258_FAULT_WDT_RESET_CTRL |= BK7258_FAULT_WDT_RESET_BIT;
  BK7258_FAULT_WDT_CTRL = BK7258_FAULT_WDT_KEY1;
  BK7258_FAULT_WDT_CTRL = BK7258_FAULT_WDT_KEY2;
  __asm volatile ("dsb sy" ::: "memory");
}

/* arm_doirq clears the selected TCB's xcp.regs before returning.  Preserve
 * that pointer at nxsched_resume_scheduler() so the STAR no-switch path can
 * recover it if the function return register is unexpectedly zero.
 */

static volatile uint32_t g_bk7258_doirq_active;
static volatile uint32_t g_bk7258_doirq_resume_regs;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern void __real_nxsched_resume_scheduler(struct tcb_s *tcb);

void __wrap_nxsched_resume_scheduler(struct tcb_s *tcb)
{
  if (g_bk7258_doirq_active != 0)
    {
      uint32_t *regs = tcb != NULL ? tcb->xcp.regs : NULL;

      if (regs != NULL &&
          (regs[REG_EXC_RETURN] & BK7258_EXC_RETURN_THREAD_MODE) != 0)
        {
          regs[REG_CONTROL] |= 1u << 1; /* CONTROL.SPSEL */
#ifdef CONFIG_ARCH_FPU
          if ((regs[REG_EXC_RETURN] & BK7258_EXC_RETURN_BASIC_FRAME) == 0)
            {
              regs[REG_CONTROL] |= 1u << 2; /* CONTROL.FPCA */
            }
#endif
        }

      g_bk7258_doirq_resume_regs = (uint32_t)(uintptr_t)regs;
      __asm volatile ("dmb sy" ::: "memory");
    }

  __real_nxsched_resume_scheduler(tcb);
}

extern uint32_t *__real_arm_doirq(int irq, uint32_t *regs);

uint32_t *__wrap_arm_doirq(int irq, uint32_t *regs)
{
  uint32_t basepri = NVIC_SYSH_DISABLE_PRIORITY;

  /* The common dispatcher is not re-entrant without HIPRI support.  The
   * exception frame already contains the original BASEPRI value, which
   * exception_common restores immediately before returning.
   */

  __asm volatile
    (
      "msr basepri, %0\n"
      "dsb sy\n"
      "isb sy\n"
      :
      : "r" (basepri)
      : "memory"
    );

  g_bk7258_doirq_resume_regs = 0;
  g_bk7258_doirq_active = 1;
  __asm volatile ("dmb sy" ::: "memory");

  regs = __real_arm_doirq(irq, regs);

  g_bk7258_doirq_active = 0;
  __asm volatile ("dmb sy" ::: "memory");

  if (regs == NULL)
    {
      regs = (uint32_t *)(uintptr_t)g_bk7258_doirq_resume_regs;
    }

  if (regs == NULL)
    {
      __asm volatile ("cpsid i" ::: "memory");
      bk7258_fault_stop_watchdogs();
      bk7258_fault_putc('H');
      bk7258_fault_putc('F');
      bk7258_fault_putfield('D', 0);
      bk7258_fault_putc('\r');
      bk7258_fault_putc('\n');

      for (; ; )
        {
          __asm volatile ("wfe");
        }
    }

  return regs;
}

/* Capture CPU0 NMI/HardFault state independently from the AP record.  The
 * UART line is deliberately compact so the complete PC/LR/CFSR evidence is
 * emitted before any external reset source can fire.  The same record remains
 * debugger-readable at 0x2809f100.
 */

/* The APB watchdog raises its expiry as an NMI (unmaskable - it fires even
 * under cpsid i, which is exactly what the xTS -r 1 case requires).  Treat
 * NMI as the watchdog bark: record the cause, emit the same debugger-
 * readable dump, then force the AON whole-device reset instead of parking.
 * Genuine HardFault/MemManage/etc. keep the parked-for-inspection path. */

extern void bk_misc_set_reset_reason(uint32_t type);
#ifdef CONFIG_BK7258_WDT
extern void bk7258_wdt_force_system_reset(void)
  __attribute__((noreturn));
#endif

#define BK7258_EXC_NMI 2u

static void __attribute__((noinline, noreturn, used))
bk7258_fault_handler(uint32_t *stack, uint32_t exc_return,
                     uint32_t exception)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  volatile struct bk7258_cp_fault_state_s *fault = bk7258_cp_fault_state();
  uintptr_t frame_addr = (uintptr_t)stack;
  const volatile uint32_t *frame;
  uint32_t stacked_r0 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r1 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r2 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r3 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_r12 = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_lr = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_pc = BK7258_FAULT_INVALID_VALUE;
  uint32_t stacked_xpsr = BK7258_FAULT_INVALID_VALUE;
  uint32_t hfsr;
  uint32_t cfsr;
  uint32_t mmfar;
  uint32_t bfar;

  bk7258_fault_stop_watchdogs();

  hfsr = BK7258_SCB_HFSR;
  cfsr = BK7258_SCB_CFSR;
  mmfar = BK7258_SCB_MMFAR;
  bfar = BK7258_SCB_BFAR;

  /* ARMv8-M stacks R0..xPSR first; an extended exception frame appends
   * S0..S15/FPSCR after those eight words.  The raw SP therefore
   * always addresses the basic frame.  This also matches NuttX arm_m/irq.h.
   */

  if ((frame_addr & (sizeof(uint32_t) - 1u)) == 0 &&
      frame_addr >= BK7258_CP_RAM_BASE &&
      frame_addr <= BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE -
                    BK7258_EXCEPTION_FRAME_WORDS * sizeof(uint32_t))
    {
      frame = (const volatile uint32_t *)frame_addr;
      stacked_r0   = frame[0];
      stacked_r1   = frame[1];
      stacked_r2   = frame[2];
      stacked_r3   = frame[3];
      stacked_r12  = frame[4];
      stacked_lr   = frame[5];
      stacked_pc   = frame[6];
      stacked_xpsr = frame[7];
    }

  fault->magic         = 0;
  fault->version       = BK7258_CP_FAULT_STATE_VERSION;
  fault->size          = sizeof(*fault);
  fault->generation    = state->magic == BK7258_AP_BOOT_STATE_MAGIC ?
                         state->generation : 0;
  fault->exception     = exception;
  fault->reserved      = 0;
  fault->exc_return    = exc_return;
  fault->stack_pointer = (uint32_t)(uintptr_t)stack;
  fault->hfsr          = hfsr;
  fault->cfsr          = cfsr;
  fault->mmfar         = mmfar;
  fault->bfar          = bfar;
  fault->stacked_r0    = stacked_r0;
  fault->stacked_r1    = stacked_r1;
  fault->stacked_r2    = stacked_r2;
  fault->stacked_r3    = stacked_r3;
  fault->stacked_r12   = stacked_r12;
  fault->stacked_lr    = stacked_lr;
  fault->stacked_pc    = stacked_pc;
  fault->stacked_xpsr  = stacked_xpsr;
  __asm volatile ("dmb sy" ::: "memory");
  fault->magic = BK7258_CP_FAULT_STATE_MAGIC;

  bk7258_fault_putc('H');
  bk7258_fault_putc('F');
  bk7258_fault_putfield('E', exception);
  bk7258_fault_putfield('X', exc_return);
  bk7258_fault_putfield('S', (uint32_t)(uintptr_t)stack);
  bk7258_fault_putfield('H', hfsr);
  bk7258_fault_putfield('C', cfsr);
  bk7258_fault_putfield('P', stacked_pc);
  bk7258_fault_putfield('L', stacked_lr);
  bk7258_fault_putfield('Q', stacked_xpsr);
  bk7258_fault_putc('\r');
  bk7258_fault_putc('\n');

  if (exception == BK7258_EXC_NMI)
    {
      /* Watchdog bark: record the xTS-required cause and finish with the
       * documented whole-device reset rather than an inspection park. */

      /* A watchdog-enabled image uses the proven AON whole-device reset.
       * A deliberately watchdog-free benchmark image has no AP runtime to
       * preserve, so the generic Cortex-M system reset is its fail-safe.  Do
       * not stamp that generic path as a watchdog reset: without the WDT
       * contract an NMI does not prove which SoC source asserted it. */

#ifdef CONFIG_BK7258_WDT
      bk_misc_set_reset_reason(RESET_SOURCE_NMI_WDT);
      bk7258_wdt_force_system_reset();
#else
      up_systemreset();
#endif
    }

  for (; ; )
    {
      __asm volatile ("wfe");
    }
}

void __attribute__((naked, noreturn, used))
bk7258_hardfault_handler(void)
{
  __asm volatile
    (
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mov r1, lr\n"
      "mrs r2, ipsr\n"
      "b bk7258_fault_handler\n"
    );
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* The NuttX arm_m layer looks up symbol "_vectors" and section ".vectors".
 * `used` prevents the compiler from dropping the table at LTO; `aligned(4)`
 * keeps each 4-byte slot naturally aligned.
 *
 * The table is sized explicitly at 80 entries (0x140 bytes) so the two
 * app-magic words at [64]/[65] are inside the table and the upper external
 * IRQ slots [66..79] are included.  After VTOR switches to RAM,
 * arm_ramvec_attach repairs slots 64/65 to exception_common.
 *
 * GCC range designated initializers fill the dispatcher slots the same way
 * arm_vectors.c does; the magic slots [64]/[65] are then pinned explicitly
 * (they are runtime-repaired after RAM vector init).
 */

__attribute__((section(".vectors"), used, aligned(4)))
const void *const _vectors[80] =
{
  /* [0]    reset/IDLE stack top, preserved as PSP by the wrapper. */
  [0]  = (void *)BK7258_CP_IDLE_STACK,

  /* [1]    reset wrapper; installs the dedicated interrupt MSP. */
  [1]  = (void *)bk7258_reset_entry,

  /* [2..3] NMI + HardFault -> diagnostic bk7258_hardfault_handler.
   *   Capture the frame at 0x2809f100, print key registers on the selected
   *   UART console when one exists, then
   *   park.  NMI ([2]) uses the same path so escalated faults are observable.
   */
  [2]  = &bk7258_hardfault_handler,
  [3]  = &bk7258_hardfault_handler,

  /* [4..14] remaining system exceptions -> exception_common
   *   4=MemManage 5=BusFault 6=UsageFault 7=SecureFault 8..10=Reserved
   *   11=SVCall 12=DebugMonitor 13=Reserved 14=PendSV
   */
  [4 ... 14]  = &exception_common,

  /* Route SysTick and lower external IRQs through the full context-saving
   * exception path.  SysTick is board-verified on this path; routing the
   * external slots the same way is the first UART RX IRQ recovery step. */

  [15 ... 63] = &exception_common,

  /* [64] app magic word 0: "BK72" little-endian.  Byte offset 0x100. */
  [64] = (void *)BK7258_APP_MAGIC_WORD0,

  /* [65] app magic word 1: "36\0\0" little-endian. Byte offset 0x104. */
  [65] = (void *)BK7258_APP_MAGIC_WORD1,

  /* [66..79] upper external IRQs -> exception_common
   *   These slots correspond to external IRQs 48..63.  Slots 64/65 are
   *   overwritten by the boot magic above and must be runtime-repaired
   *   via arm_ramvec_attach after VTOR switches to RAM.
   */
  [66 ... 79] = &exception_common
};
