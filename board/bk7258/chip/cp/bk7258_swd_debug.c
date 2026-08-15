/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_swd_debug.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 board-owned SWD pinmux bridge, independent of console selection.
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/chip/bk7258_debug.h>
#include <arch/chip/bk7258_debug_route.h>

#define BK7258_DEMCR_REG               0xe000edfcu
#define BK7258_VTOR_REG                0xe000ed08u
#define BK7258_SYS_DEBUG0_REG          0x440100e0u
#define BK7258_SYS_DEBUG1_REG          0x440100e4u

#ifdef CONFIG_BK7258_SWD_PINS_P20_P21
#  define BK7258_SWD_FUNCTION_REG       BK7258_SYS_GPIO16_23_FUNC_REG
#  define BK7258_SWD_FUNCTION_MASK      BK7258_SWD_GROUP0_FUNC_MASK
#  define BK7258_SWD_FUNCTION_VALUE     BK7258_SWD_GROUP0_FUNC_VALUE
#  define BK7258_SWD_CLK_CTRL_REG       BK7258_GPIO20_CTRL_REG
#  define BK7258_SWD_IO_CTRL_REG        BK7258_GPIO21_CTRL_REG
#else
#  define BK7258_SWD_FUNCTION_REG       BK7258_SYS_GPIO0_7_FUNC_REG
#  define BK7258_SWD_FUNCTION_MASK      BK7258_SWD_GROUP1_FUNC_MASK
#  define BK7258_SWD_FUNCTION_VALUE     BK7258_SWD_GROUP1_FUNC_VALUE
#  define BK7258_SWD_CLK_CTRL_REG       BK7258_GPIO0_CTRL_REG
#  define BK7258_SWD_IO_CTRL_REG        BK7258_GPIO1_CTRL_REG
#endif

#if defined(CONFIG_BK7258_SWD_TARGET_AP0)
#  define BK7258_SWD_TARGET_CORE        BK7258_SWD_TARGET_CPU1
#elif defined(CONFIG_BK7258_SWD_TARGET_AP1)
#  define BK7258_SWD_TARGET_CORE        BK7258_SWD_TARGET_CPU2
#else
#  define BK7258_SWD_TARGET_CORE        BK7258_SWD_TARGET_CPU0
#endif

_Static_assert(sizeof(struct bk7258_swd_trace_s) <= 0x800u,
               "SWD trace exceeds the reserved CP SRAM tail");

static inline uint32_t bk7258_swd_getreg(uintptr_t address)
{
  return *(volatile uint32_t *)address;
}

static inline void bk7258_swd_putreg(uint32_t value, uintptr_t address)
{
  *(volatile uint32_t *)address = value;
}

int __wrap_gpio_hal_default_map_init(void *hal)
{
  /* This wrapper is linked only when the CP owns an SWD route.
   * The SDK driver's remaining initialization is still executed, including
   * HAL setup, mailbox resource setup and GPIO IRQ registration.  Suppress
   * only its all-pin default-map pass: that pass writes P0/P1 between the
   * BL2 debug handshake and board bring-up, and a DWT watchpoint proved that
   * the transient write is what drops an attached probe.  Chip GPIO clients
   * configure only their claimed pins after shared driver initialization.
   */

  (void)hal;
  bk7258_swd_maintain();
  return 0;
}

void bk7258_swd_trace_begin(void)
{
  volatile struct bk7258_swd_trace_s *trace =
    (volatile struct bk7258_swd_trace_s *)BK7258_SWD_TRACE_ADDRESS;
  uint32_t boot_count = 1u;

  if (trace->magic == BK7258_SWD_TRACE_MAGIC &&
      trace->version == BK7258_SWD_TRACE_VERSION)
    {
      boot_count = trace->boot_count + 1u;
    }

  trace->magic = 0u;
  __asm volatile ("dsb sy" ::: "memory");
  trace->version = BK7258_SWD_TRACE_VERSION;
  trace->boot_count = boot_count;
  trace->count = 0u;
  trace->capacity = BK7258_SWD_TRACE_SAMPLES;
  trace->sample_words = sizeof(struct bk7258_swd_trace_sample_s) /
                        sizeof(uint32_t);
  trace->reserved[0] = 0u;
  trace->reserved[1] = 0u;
  __asm volatile ("dsb sy" ::: "memory");
  trace->magic = BK7258_SWD_TRACE_MAGIC;
  __asm volatile ("dsb sy" ::: "memory");
}

void bk7258_swd_trace_snapshot(uint32_t stage)
{
  volatile struct bk7258_swd_trace_s *trace =
    (volatile struct bk7258_swd_trace_s *)BK7258_SWD_TRACE_ADDRESS;
  volatile struct bk7258_swd_trace_sample_s *sample;
  uint32_t index;
  uint32_t primask;

  if (trace->magic != BK7258_SWD_TRACE_MAGIC ||
      trace->version != BK7258_SWD_TRACE_VERSION)
    {
      return;
    }

  index = trace->count;
  if (index >= BK7258_SWD_TRACE_SAMPLES)
    {
      return;
    }

  __asm volatile ("mrs %0, primask" : "=r" (primask));
  sample = &trace->sample[index];
  sample->stage = stage;
  sample->sequence = 0u;
  sample->dhcsr = bk7258_swd_getreg(BK7258_DHCSR_REG);
  sample->demcr = bk7258_swd_getreg(BK7258_DEMCR_REG);
  sample->dauthctrl = bk7258_swd_getreg(BK7258_DAUTHCTRL_REG);
  sample->sys_debug0 = bk7258_swd_getreg(BK7258_SYS_DEBUG0_REG);
  sample->sys_debug1 = bk7258_swd_getreg(BK7258_SYS_DEBUG1_REG);
  sample->cpu_route = bk7258_swd_getreg(BK7258_SYS_CPU_ROUTE_REG);
  sample->gpio_function =
    bk7258_swd_getreg(BK7258_SWD_FUNCTION_REG);
  sample->gpio_clk_ctrl = bk7258_swd_getreg(BK7258_SWD_CLK_CTRL_REG);
  sample->gpio_io_ctrl = bk7258_swd_getreg(BK7258_SWD_IO_CTRL_REG);
  sample->vtor = bk7258_swd_getreg(BK7258_VTOR_REG);
  sample->primask = primask;
  sample->caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
  __asm volatile ("dsb sy" ::: "memory");
  sample->sequence = index + 1u;
  __asm volatile ("dsb sy" ::: "memory");
  trace->count = index + 1u;
  __asm volatile ("dsb sy" ::: "memory");
}

void bk7258_swd_maintain(void)
{
  uint32_t regval;
  uint32_t desired;

  /* These are the exact register end states produced by the official
   * v3.1.1.9 core/group helpers and already used by BL1.  Commit them after
   * the SDK calls as well: AP release initializes a second SDK SYS/GPIO view
   * and can otherwise overwrite the shared route after CP configured it.
   */

  regval = bk7258_swd_getreg(BK7258_SYS_CPU_ROUTE_REG);
  regval &= ~BK7258_SYS_JTAG_CORE_MASK;
  regval |= BK7258_SWD_TARGET_CORE << BK7258_SYS_JTAG_CORE_SHIFT;
  if (regval != bk7258_swd_getreg(BK7258_SYS_CPU_ROUTE_REG))
    {
      bk7258_swd_putreg(regval, BK7258_SYS_CPU_ROUTE_REG);
    }

  regval = bk7258_swd_getreg(BK7258_SWD_FUNCTION_REG);
  regval &= ~BK7258_SWD_FUNCTION_MASK;
  regval |= BK7258_SWD_FUNCTION_VALUE;
  if (regval != bk7258_swd_getreg(BK7258_SWD_FUNCTION_REG))
    {
      bk7258_swd_putreg(regval, BK7258_SWD_FUNCTION_REG);
    }

  /* Match gpio_hal_func_map() without comparing or overwriting unrelated
   * fields.  It disables GPIO input/output and pull, then enables the second
   * function: bits 2/3/5/6 become 0/1/0/1.  Bits 14..31 read back live AON
   * status on this silicon even though the SDK structure labels them
   * reserved; an earlier full-word comparison therefore caused continuous
   * P0/P1 writes from up_idle().
   */

  regval = bk7258_swd_getreg(BK7258_SWD_CLK_CTRL_REG);
  desired = (regval & ~BK7258_GPIO_FUNC_CTRL_MASK) |
            BK7258_GPIO_JTAG_CTRL;
  if (regval != desired)
    {
      bk7258_swd_putreg(desired, BK7258_SWD_CLK_CTRL_REG);
    }

  regval = bk7258_swd_getreg(BK7258_SWD_IO_CTRL_REG);
  desired = (regval & ~BK7258_GPIO_FUNC_CTRL_MASK) |
            BK7258_GPIO_JTAG_CTRL;
  if (regval != desired)
    {
      bk7258_swd_putreg(desired, BK7258_SWD_IO_CTRL_REG);
    }

  __asm volatile ("dsb sy; isb sy" ::: "memory");
}

int bk7258_swd_initialize(void)
{
  /* Preserve the compile-time core and pin group selected by BL1/BL2.  The
   * selected board resource graph must exclude conflicts before either
   * P0/P1 or P20/P21 is routed to the configured target core.
   *
   * Do not call bk_gpio_driver_init() here.  Chip consumers such as UART,
   * SARADC and the GPIO lower half share that boot-lifetime SDK runtime and
   * initialize it before mapping their own pins.  This profile wraps only
   * its destructive all-pin default-map pass so the HAL/IRQ service remains
   * available without a transient P0/P1 remap.  A DWT write watchpoint proved
   * that the old call from this function was long enough to drop an attached
   * SWD session.  The route below is the complete, idempotent end state
   * produced by sys_drv_set_jtag_mode(0) + gpio_jtag_sel(1).
   */

  bk7258_swd_maintain();

  return 0;
}
