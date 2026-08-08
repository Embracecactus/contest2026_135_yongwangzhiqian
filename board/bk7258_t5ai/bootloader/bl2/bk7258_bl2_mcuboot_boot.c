/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/
 * bk7258_mcuboot_boot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Direct-XIP handoff for the NuttX MCUboot port on BK7258.
 *
 * MCUboot treats the CP+AP firmware pair as one atomic image.  Slot A is
 * already visible at the normal XIP address.  Slot B is made visible there
 * using the BK7258 flash remapper before the Cortex-M vector table is used.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_BOOT_IMAGE

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/board.h>

#include <arch/chip/bk7258_amp.h>

#define BK7258_FLASH_CONTROLLER_BASE  0x44030000u
#define BK7258_FLASH_REMAP_BEGIN       (BK7258_FLASH_CONTROLLER_BASE + 0x58u)
#define BK7258_FLASH_REMAP_END         (BK7258_FLASH_CONTROLLER_BASE + 0x5cu)
#define BK7258_FLASH_REMAP_OFFSET      (BK7258_FLASH_CONTROLLER_BASE + 0x60u)
#define BK7258_FLASH_REMAP_ENABLE      (BK7258_FLASH_CONTROLLER_BASE + 0x64u)
#define BK7258_SCB_ICIALLU             0xe000ef50u
#define BK7258_SCB_VTOR                0xe000ed08u
#define BK7258_NVIC_ICER0              0xe000e180u
#define BK7258_NVIC_ICPR0              0xe000e280u
#define BK7258_SYSTICK_CTRL            0xe000e010u

#define BK7258_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define BK7258_REMAP_BEGIN             BK7258_ROLE_SLOT_A_CP_XIP_START
#define BK7258_REMAP_END               BK7258_ROLE_SLOT_A_AP_XIP_END
#define BK7258_REMAP_OFFSET \
  (BK7258_FLASH_XIP_BASE + \
   (BK7258_ROLE_SLOT_B_PAIR_OFFSET / BK7258_FLASH_CRC_TOTAL_SIZE * \
    BK7258_FLASH_CRC_DATA_SIZE) - BK7258_ROLE_SLOT_A_CP_LOGICAL_OFFSET)

static inline __attribute__((always_inline)) void bk7258_mcuboot_dsb_isb(void)
{
  __asm volatile ("dsb sy; isb" ::: "memory");
}

/* Keep this small tail separate from the C validation/remap code.  The
 * official BK7258 A/B bootloader enters the selected image with an otherwise
 * clean integer register file.  In particular, callers cannot rely on the
 * compiler choosing a register which remains valid after the reset MSP is
 * installed, so retain the reset vector in r9 as the official code does.
 */

static void __attribute__((naked, noinline, noreturn, section(".data")))
bk7258_mcuboot_jump(uint32_t msp, uint32_t reset)
{
  __asm volatile
  (
    "mov r9, r1\n"
    "msr msp, r0\n"
    "dsb sy\n"
    "isb sy\n"
    "mov r0, #0\n"
    /* MCUboot may have used the floating-point context while it verifies an
     * image.  A Direct-XIP NuttX image starts as a reset image, so do not
     * leak CONTROL.FPCA/SPSEL into its first exception frame. */
    "msr control, r0\n"
    "isb\n"
    "mov r1, r0\n"
    "mov r2, r0\n"
    "mov r3, r0\n"
    "mov r4, r0\n"
    "mov r5, r0\n"
    "mov r6, r0\n"
    "mov r7, r0\n"
    "mov r8, r0\n"
    "mov r10, r0\n"
    "mov r11, r0\n"
    "mov r12, r0\n"
    "dsb sy\n"
    "isb sy\n"
    "bx r9\n"
  );
}

/* The secondary-slot remap covers the complete A-slot CP/AP XIP window,
 * including the BL2 text currently executing this code.  Consequently, no
 * instruction may be fetched from flash after FLASH_REMAP_ENABLE is set.
 *
 * NuttX copies .data to CP SRAM before mcuboot_loader_main() runs.  Keep the
 * final remap/vector/handoff sequence in that RAM-resident section and do not
 * return to its flash-resident caller.  This is the same essential placement
 * rule as an official BL2 in a dedicated partition, adapted to the existing
 * reverse-engineered BL1 layout.
 */

static void __attribute__((noinline, noreturn, section(".data")))
bk7258_mcuboot_boot_secondary(uint32_t hdr_size)
{
  uintptr_t image = BK7258_CP_FLASH_ADDR + hdr_size;
  uint32_t msp;
  uint32_t reset;

  /* Match the normal handoff ordering.  An exception between switching the
   * physical XIP source and installing the B-slot VTOR would be fatal. */

  __asm volatile ("cpsid i" ::: "memory");

  BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) &= ~1u;
  bk7258_mcuboot_dsb_isb();
  BK7258_REG32(BK7258_FLASH_REMAP_BEGIN) = BK7258_REMAP_BEGIN;
  BK7258_REG32(BK7258_FLASH_REMAP_END) = BK7258_REMAP_END;
  BK7258_REG32(BK7258_FLASH_REMAP_OFFSET) = BK7258_REMAP_OFFSET;
  bk7258_mcuboot_dsb_isb();
  BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) |= 1u;
  bk7258_mcuboot_dsb_isb();

  /* The CP and BL2 use identical XIP addresses.  Drop any A-slot lines
   * fetched while BL2 was validating the image before branching to B. */

  BK7258_REG32(BK7258_SCB_ICIALLU) = 0;
  bk7258_mcuboot_dsb_isb();

  /* The vector is now read through the B-slot mapping. */

  msp = *(volatile const uint32_t *)image;
  reset = *(volatile const uint32_t *)(image + sizeof(uint32_t));
  if ((msp & 7u) != 0 || msp < BK7258_CP_RAM_BASE ||
      msp >= BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE ||
      (reset & 1u) == 0 || (reset & ~1u) < BK7258_CP_FLASH_ADDR ||
      (reset & ~1u) >= BK7258_ROLE_SLOT_A_AP_XIP_START)
    {
      for (;;)
        {
        }
    }

  BK7258_REG32(BK7258_SYSTICK_CTRL) = 0;
  BK7258_REG32(BK7258_NVIC_ICER0) = 0xffffffffu;
  BK7258_REG32(BK7258_NVIC_ICER0 + 4u) = 0xffffffffu;
  BK7258_REG32(BK7258_NVIC_ICPR0) = 0xffffffffu;
  BK7258_REG32(BK7258_NVIC_ICPR0 + 4u) = 0xffffffffu;
  BK7258_REG32(BK7258_SCB_VTOR) = image;
  bk7258_mcuboot_dsb_isb();

  bk7258_mcuboot_jump(msp, reset);
}

static int bk7258_mcuboot_select_slot(bool secondary)
{
  BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) &= ~1u;
  bk7258_mcuboot_dsb_isb();

  if (!secondary)
    {
      return 0;
    }

  BK7258_REG32(BK7258_FLASH_REMAP_BEGIN) = BK7258_REMAP_BEGIN;
  BK7258_REG32(BK7258_FLASH_REMAP_END) = BK7258_REMAP_END;
  BK7258_REG32(BK7258_FLASH_REMAP_OFFSET) = BK7258_REMAP_OFFSET;
  bk7258_mcuboot_dsb_isb();

  if (BK7258_REG32(BK7258_FLASH_REMAP_BEGIN) != BK7258_REMAP_BEGIN ||
      BK7258_REG32(BK7258_FLASH_REMAP_END) != BK7258_REMAP_END ||
      BK7258_REG32(BK7258_FLASH_REMAP_OFFSET) != BK7258_REMAP_OFFSET)
    {
      return -EIO;
    }

  BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) |= 1u;
  bk7258_mcuboot_dsb_isb();
  BK7258_REG32(BK7258_SCB_ICIALLU) = 0;
  bk7258_mcuboot_dsb_isb();
  return (BK7258_REG32(BK7258_FLASH_REMAP_ENABLE) & 1u) != 0 ? 0 : -EIO;
}

int board_boot_image(FAR const char *path, uint32_t hdr_size)
{
  uintptr_t image;
  uint32_t msp;
  uint32_t reset;
  int ret;

  if (path == NULL)
    {
      return -EINVAL;
    }

  if (strcmp(path, CONFIG_MCUBOOT_PRIMARY_SLOT_PATH) == 0)
    {
      ret = bk7258_mcuboot_select_slot(false);
    }
  else if (strcmp(path, CONFIG_MCUBOOT_SECONDARY_SLOT_PATH) == 0)
    {
      /* This does not return: enabling the remap while executing BL2 from
       * A-slot flash would otherwise remap BL2's own next instruction. */

      bk7258_mcuboot_boot_secondary(hdr_size);
    }
  else
    {
      return -ENOENT;
    }

  if (ret < 0 || hdr_size > BK7258_CP_FLASH_SIZE - 8u)
    {
      return ret < 0 ? ret : -EINVAL;
    }

  image = BK7258_CP_FLASH_ADDR + hdr_size;
  msp = *(volatile const uint32_t *)image;
  reset = *(volatile const uint32_t *)(image + sizeof(uint32_t));
  if ((msp & 7u) != 0 || msp < BK7258_CP_RAM_BASE ||
      msp >= BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE ||
      (reset & 1u) == 0 || (reset & ~1u) < BK7258_CP_FLASH_ADDR ||
      (reset & ~1u) >= BK7258_ROLE_SLOT_A_AP_XIP_START)
    {
      return -EINVAL;
    }

  __asm volatile ("cpsid i" ::: "memory");
  BK7258_REG32(BK7258_SYSTICK_CTRL) = 0;
  BK7258_REG32(BK7258_NVIC_ICER0) = 0xffffffffu;
  BK7258_REG32(BK7258_NVIC_ICER0 + 4u) = 0xffffffffu;
  BK7258_REG32(BK7258_NVIC_ICPR0) = 0xffffffffu;
  BK7258_REG32(BK7258_NVIC_ICPR0 + 4u) = 0xffffffffu;
  BK7258_REG32(BK7258_SCB_VTOR) = image;
  bk7258_mcuboot_dsb_isb();

  bk7258_mcuboot_jump(msp, reset);
}

#endif /* CONFIG_BOARDCTL_BOOT_IMAGE */
