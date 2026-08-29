/****************************************************************************
 * chips/bk7258/ap/bk7258_pinmux.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Serialized BK7258 SoC pin-function selector updates.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_CORE

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <arch/chip/bk7258_pinmux.h>

#include "arm_internal.h"

#define BK7258_PINMUX_SYS_BASE        0x44010000u
#define BK7258_PINMUX_GPIO_BASE       0x44000400u
#define BK7258_PINMUX_SELECTOR_BASE   (BK7258_PINMUX_SYS_BASE + 0xc0u)
#define BK7258_PINMUX_PINS_PER_WORD   8u
#define BK7258_PINMUX_WIDTH           4u
#define BK7258_PINMUX_FUNCTION_MAX    0x0fu
#define BK7258_PINMUX_PIN_COUNT       56u
#define BK7258_PINMUX_PERIPHERAL_BIT  (1u << 6)

extern uint32_t sys_amp_res_acquire(void);
extern uint32_t sys_amp_res_release(void);

static uintptr_t bk7258_pinmux_selector_address(uint8_t pin)
{
  return BK7258_PINMUX_SELECTOR_BASE +
         pin / BK7258_PINMUX_PINS_PER_WORD * sizeof(uint32_t);
}

static uintptr_t bk7258_pinmux_pad_address(uint8_t pin)
{
  return BK7258_PINMUX_GPIO_BASE + pin * sizeof(uint32_t);
}

int bk7258_pinmux_apply(const struct bk7258_pinmux_config_s *configs,
                        size_t count)
{
  irqstate_t flags;
  size_t index;
  int ret = OK;

  if (configs == NULL || count == 0u)
    {
      return -EINVAL;
    }

  for (index = 0u; index < count; index++)
    {
      if (configs[index].pin >= BK7258_PINMUX_PIN_COUNT ||
          configs[index].function > BK7258_PINMUX_FUNCTION_MAX)
        {
          return -ERANGE;
        }
    }

  if (sys_amp_res_acquire() != 0u)
    {
      return -EBUSY;
    }

  flags = up_irq_save();
  for (index = 0u; index < count; index++)
    {
      uintptr_t selector =
        bk7258_pinmux_selector_address(configs[index].pin);
      uintptr_t pad = bk7258_pinmux_pad_address(configs[index].pin);
      uint32_t shift =
        configs[index].pin % BK7258_PINMUX_PINS_PER_WORD *
        BK7258_PINMUX_WIDTH;
      uint32_t mask = BK7258_PINMUX_FUNCTION_MAX << shift;
      uint32_t value = getreg32(selector);

      putreg32((value & ~mask) |
               (uint32_t)configs[index].function << shift, selector);
      value = getreg32(pad);
      putreg32(configs[index].peripheral ?
               value | BK7258_PINMUX_PERIPHERAL_BIT :
               value & ~BK7258_PINMUX_PERIPHERAL_BIT, pad);
    }

  __asm volatile ("dmb sy" ::: "memory");
  up_irq_restore(flags);

  if (sys_amp_res_release() != 0u)
    {
      ret = -EIO;
    }

  return ret;
}

#endif /* CONFIG_BK7258_AP_CORE */
