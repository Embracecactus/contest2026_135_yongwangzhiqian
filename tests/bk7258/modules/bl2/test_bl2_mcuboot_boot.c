/*
 * test_bl2_mcuboot_boot.c - host tests for the BK7258 Direct-XIP handoff
 * (bk7258_bl2_mcuboot_boot.c).
 *
 * The real implementation is compiled from a throwaway patched copy:
 * BK7258_REG32 routed to mock_reg32_ref, ARM barriers neutralized, the
 * naked Cortex-M jump replaced by the framework mock_boot_jump hook, and
 * the secondary-path "invalid vector" infinite loop replaced by the same
 * hook with 0xffffffff markers.  Vector tables are prefilled in the real
 * XIP window (mock_flash mmap at 0x02000000).
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <errno.h>
#include <cmocka.h>

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <arch/board/bk7258_image_layout.h>
#include <arch/chip/bk7258_amp.h>

#include "mock_reg32.h"
#include "mock_flash.h"
#include "mock_boot_jump.h"

/* Flash remapper registers (same addresses as the module under test) and
 * the B-slot remap values the handoff must write. */
#define T_REMAP_BEGIN   0x44030058u
#define T_REMAP_END     0x4403005cu
#define T_REMAP_OFFSET  0x44030060u
#define T_REMAP_ENABLE  0x44030064u
#define T_REMAP_BEGIN_VAL BK7258_ROLE_SLOT_A_CP_XIP_START
#define T_REMAP_END_VAL   BK7258_ROLE_SLOT_A_AP_XIP_END
#define T_REMAP_OFFSET_VAL \
  (BK7258_FLASH_XIP_BASE + \
   (BK7258_ROLE_SLOT_B_PAIR_OFFSET / BK7258_FLASH_CRC_TOTAL_SIZE * \
    BK7258_FLASH_CRC_DATA_SIZE) - BK7258_ROLE_SLOT_A_CP_LOGICAL_OFFSET)

/* SCB/SysTick/NVIC register addresses used by the handoff. */
#define T_SYSTICK_CTRL  0xe000e010u
#define T_NVIC_ICER0    0xe000e180u
#define T_NVIC_ICPR0    0xe000e280u
#define T_SCB_VTOR      0xe000ed08u
#define T_SCB_ICIALLU   0xe000ef50u

#define T_IMAGE_HDR     0x100u

#define T_MSP           (BK7258_CP_RAM_BASE + 0x10000u)
#define T_RESET         (BK7258_CP_FLASH_ADDR + 0x101u)
#define T_BAD_MSP_LOW   (BK7258_CP_RAM_BASE - 4u)
#define T_BAD_MSP_NOALIGN (BK7258_CP_RAM_BASE + 4u)
#define T_BAD_MSP_HIGH  (BK7258_CP_RAM_BASE + BK7258_CP_RAM_SIZE)
#define T_BAD_RESET_EVEN (BK7258_CP_FLASH_ADDR + 0x100u)
#define T_BAD_RESET_LOW (BK7258_CP_FLASH_ADDR - 1u)
#define T_BAD_RESET_HIGH BK7258_ROLE_SLOT_A_AP_XIP_START

static void write_vectors(uint32_t hdr_size, uint32_t msp, uint32_t reset)
{
  volatile uint32_t *vec =
    (volatile uint32_t *)(uintptr_t)(BK7258_CP_FLASH_ADDR + hdr_size);

  vec[0] = msp;
  vec[1] = reset;
}

static int setup(void **state)
{
  (void)state;
  assert_non_null(mock_flash_map());
  mock_flash_erase_fill(0, BK7258_HOST_FLASH_SIZE);
  mock_reg32_reset();
  return 0;
}

static int teardown(void **state)
{
  (void)state;
  mock_flash_unmap();
  return 0;
}

static void test_null_path_einval(void **state)
{
  (void)state;
  assert_int_equal(board_boot_image(NULL, 0), -EINVAL);
}

static void test_unknown_path_enoent(void **state)
{
  (void)state;
  assert_int_equal(board_boot_image("/dev/nonexistent", 0), -ENOENT);
  assert_int_equal(board_boot_image("/dev/nonexistent", 0x400000), -ENOENT);
}

static void test_primary_hdr_overflow_einval(void **state)
{
  (void)state;
  /* select_slot(false) runs first, then the hdr_size check rejects. */
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    BK7258_CP_FLASH_SIZE - 7u), -EINVAL);
  assert_int_equal(mock_reg32_read(T_REMAP_ENABLE) & 1u, 0);
}

static void test_primary_bad_msp_low(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_BAD_MSP_LOW, T_RESET);
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    T_IMAGE_HDR), -EINVAL);
}

static void test_primary_bad_msp_unaligned(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_BAD_MSP_NOALIGN, T_RESET);
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    T_IMAGE_HDR), -EINVAL);
}

static void test_primary_bad_msp_above_ram(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_BAD_MSP_HIGH, T_RESET);
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    T_IMAGE_HDR), -EINVAL);
}

static void test_primary_bad_reset_even(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_MSP, T_BAD_RESET_EVEN);
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    T_IMAGE_HDR), -EINVAL);
}

static void test_primary_bad_reset_below_flash(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_MSP, T_BAD_RESET_LOW);
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    T_IMAGE_HDR), -EINVAL);
}

static void test_primary_bad_reset_above_ap_slot(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_MSP, T_BAD_RESET_HIGH);
  assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                    T_IMAGE_HDR), -EINVAL);
}

static void test_primary_valid_jumps_with_teardown(void **state)
{
  uint32_t image = BK7258_CP_FLASH_ADDR + T_IMAGE_HDR;

  (void)state;
  write_vectors(T_IMAGE_HDR, T_MSP, T_RESET);
  if (sigsetjmp(g_mock_boot_jump_env, 1) == 0)
    {
      assert_int_equal(board_boot_image(CONFIG_MCUBOOT_PRIMARY_SLOT_PATH,
                                        T_IMAGE_HDR), 0);
      fail_msg("primary path returned instead of jumping");
    }

  assert_int_equal(g_mock_boot_jump_msp, T_MSP);
  assert_int_equal(g_mock_boot_jump_reset, T_RESET);

  /* Remap disabled for the primary slot; the pair registers are left at
   * their power-on values (0 in the mock). */
  assert_int_equal(mock_reg32_read(T_REMAP_ENABLE) & 1u, 0);
  assert_int_equal(mock_reg32_read(T_REMAP_BEGIN), 0);
  assert_int_equal(mock_reg32_read(T_REMAP_END), 0);
  assert_int_equal(mock_reg32_read(T_REMAP_OFFSET), 0);

  /* Interrupt/SysTick teardown and vector-table install. */
  assert_int_equal(mock_reg32_read(T_SYSTICK_CTRL), 0);
  assert_int_equal(mock_reg32_read(T_NVIC_ICER0), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_NVIC_ICER0 + 4u), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_NVIC_ICPR0), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_NVIC_ICPR0 + 4u), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_SCB_VTOR), image);
}

static void test_secondary_valid_jumps_with_remap(void **state)
{
  uint32_t image = BK7258_CP_FLASH_ADDR + T_IMAGE_HDR;

  (void)state;
  write_vectors(T_IMAGE_HDR, T_MSP, T_RESET);
  if (sigsetjmp(g_mock_boot_jump_env, 1) == 0)
    {
      assert_int_equal(board_boot_image(CONFIG_MCUBOOT_SECONDARY_SLOT_PATH,
                                        T_IMAGE_HDR), 0);
      fail_msg("secondary path returned instead of jumping");
    }

  assert_int_equal(g_mock_boot_jump_msp, T_MSP);
  assert_int_equal(g_mock_boot_jump_reset, T_RESET);

  /* The B-slot remap pair is written and enabled. */
  assert_int_equal(mock_reg32_read(T_REMAP_BEGIN),
                   T_REMAP_BEGIN_VAL);
  assert_int_equal(mock_reg32_read(T_REMAP_END),
                   T_REMAP_END_VAL);
  assert_int_equal(mock_reg32_read(T_REMAP_OFFSET),
                   T_REMAP_OFFSET_VAL);
  assert_int_equal(mock_reg32_read(T_REMAP_ENABLE) & 1u, 1);
  assert_int_equal(mock_reg32_read(T_SCB_ICIALLU), 0);

  /* Same teardown + VTOR sequence as the primary path. */
  assert_int_equal(mock_reg32_read(T_SYSTICK_CTRL), 0);
  assert_int_equal(mock_reg32_read(T_NVIC_ICER0), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_NVIC_ICER0 + 4u), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_NVIC_ICPR0), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_NVIC_ICPR0 + 4u), UINT32_MAX);
  assert_int_equal(mock_reg32_read(T_SCB_VTOR), image);
}

static void test_secondary_invalid_vector_hangs(void **state)
{
  (void)state;
  write_vectors(T_IMAGE_HDR, T_BAD_MSP_LOW, T_RESET);
  if (sigsetjmp(g_mock_boot_jump_env, 1) == 0)
    {
      assert_int_equal(board_boot_image(CONFIG_MCUBOOT_SECONDARY_SLOT_PATH,
                                        T_IMAGE_HDR), 0);
      fail_msg("secondary path returned instead of hanging");
    }

  /* The mock hook reports the hang with marker values. */
  assert_int_equal(g_mock_boot_jump_msp, UINT32_MAX);
  assert_int_equal(g_mock_boot_jump_reset, UINT32_MAX);

  /* The remap was already applied before the vector check... */
  assert_int_equal(mock_reg32_read(T_REMAP_ENABLE) & 1u, 1);
  assert_int_equal(mock_reg32_read(T_REMAP_BEGIN),
                   T_REMAP_BEGIN_VAL);
  assert_int_equal(mock_reg32_read(T_SCB_ICIALLU), 0);

  /* ...but the interrupt teardown and VTOR install never ran. */
  assert_int_equal(mock_reg32_read(T_SYSTICK_CTRL), 0);
  assert_int_equal(mock_reg32_read(T_NVIC_ICER0), 0);
  assert_int_equal(mock_reg32_read(T_NVIC_ICPR0), 0);
  assert_int_equal(mock_reg32_read(T_SCB_VTOR), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_null_path_einval, setup, teardown),
    cmocka_unit_test_setup_teardown(test_unknown_path_enoent, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_hdr_overflow_einval, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_bad_msp_low, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_bad_msp_unaligned, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_bad_msp_above_ram, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_bad_reset_even, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_bad_reset_below_flash, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_bad_reset_above_ap_slot, setup, teardown),
    cmocka_unit_test_setup_teardown(test_primary_valid_jumps_with_teardown, setup, teardown),
    cmocka_unit_test_setup_teardown(test_secondary_valid_jumps_with_remap, setup, teardown),
    cmocka_unit_test_setup_teardown(test_secondary_invalid_vector_hangs, setup, teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}