/*
 * test_boot_clock.c - host unit tests for boot_clock.c (cold-start DPLL).
 *
 * MMIO register traffic is redirected into the framework map by the patch
 * profile; the analog-SPI busy bit (ANA_SPI_STATE) drives the timeout path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "mock_reg32.h"
#include "boot_clock.h"

#define ANA_SPI_STATE     0x440100E8u
#define ANA_REG0          0x44010100u
#define ANA_REG2          0x44010108u
#define ANA_REG3          0x4401010Cu
#define ANA_REG5          0x44010114u
#define ANA_REG8          0x44010120u
#define ANA_REG9          0x44010124u
#define ANA_REG10         0x44010128u
#define ANA_REG11         0x4401012Cu
#define ANA_REG12         0x44010130u
#define ANA_REG13         0x44010134u
#define ANA_REG25         0x44010164u

#define CPU1_HALT_CLK_OP  0x44010014u
#define CPU_CLK_DIV_MODE1 0x44010020u
#define CPU_CLK_DIV_MODE2 0x44010024u

#define EN_DPLL_BIT       (1u << 5)

#define M1_CLKDIV_MASK    0x0fu
#define M1_CKSEL_MASK     (0x3u << 4)
#define M1_CKSEL_DPLL480  (0x3u << 4)
#define M2_FLASH_CKSEL_MASK (0x3u << 24)
#define M2_FLASH_CKSEL_DPLL (0x1u << 24)

static int setup(void **state)
{
  mock_reg32_reset();
  return 0;
}

static void expect_vendor_handoff(void **state)
{
  uint32_t m1 = mock_reg32_read(CPU_CLK_DIV_MODE1);
  uint32_t m2 = mock_reg32_read(CPU_CLK_DIV_MODE2);

  assert_int_equal(m1 & M1_CLKDIV_MASK, 3u);
  assert_int_equal(m1 & M1_CKSEL_MASK, M1_CKSEL_DPLL480);
  assert_int_equal(m2 & M2_FLASH_CKSEL_MASK, M2_FLASH_CKSEL_DPLL);
  assert_true((mock_reg32_read(CPU1_HALT_CLK_OP) & (1u << 4)) != 0);
}

static void test_warm_path_skips_analog(void **state)
{
  /* EN_DPLL already set: the analog sequence is skipped, handoff applied. */
  mock_reg32_write(ANA_REG5, EN_DPLL_BIT);
  mock_reg32_write(ANA_REG0, 0xa5a5a5a5u);

  boot_clock_cold_init();

  expect_vendor_handoff(state);
  /* The analog sequence must not have touched ANA_REG0's content. */
  assert_int_equal(mock_reg32_read(ANA_REG0), 0xa5a5a5a5u);
}

static void test_cold_path_full_sequence(void **state)
{
  boot_clock_cold_init();

  /* step1: EN_DPLL on, NC_3_3 on, EN_DCO on, PWDAUDPLL re-set, adc_div 0x1. */
  {
    uint32_t reg5 = mock_reg32_read(ANA_REG5);

    assert_true((reg5 & ((1u << 5) | (1u << 3) | (1u << 2))) ==
                ((1u << 5) | (1u << 3) | (1u << 2)));
    assert_true((reg5 & (1u << 13)) != 0);
    assert_int_equal((reg5 >> 10) & 0x3u, 0x1u);
  }

  /* step2: band 0x13, DS PLL test writes.  step5's recalibration raises
   * ANA_REG0.spitrig (bit 19), which remains set (the analog side clears
   * it); mask both dynamic fields off the compare. */
  {
    uint32_t reg0 = mock_reg32_read(ANA_REG0);

    assert_int_equal((reg0 >> 20) & 0x1fu, 0x13u);
    assert_true((reg0 & (1u << 19)) != 0);
    assert_int_equal(reg0 & ~((0x1fu << 20) | (1u << 19) | (1u << 26)),
                     0xF1305B56u & ~((0x1fu << 20) | (1u << 19) | (1u << 26)));
    assert_int_equal(reg0 & (1u << 26), 0u);
  }

  /* step3: xtal ctune values.  REG3 is modified again by step4
   * (ANA_REG3.inbufen0v9 = 1), so the expected final value carries bit 6. */
  assert_int_equal(mock_reg32_read(ANA_REG2), 0x7E003450u);
  assert_int_equal(mock_reg32_read(ANA_REG3), 0xC5F00BC8u);

  /* step4: latched block values; latch ends off. */
  assert_int_equal(mock_reg32_read(ANA_REG8), 0x57E62F26u);
  assert_int_equal(mock_reg32_read(ANA_REG9), 0x787BC8A4u);
  assert_int_equal(mock_reg32_read(ANA_REG10), 0xC3D543A7u);
  assert_int_equal(mock_reg32_read(ANA_REG11), 0xB47E99F8u);
  assert_int_equal(mock_reg32_read(ANA_REG12), 0xB47ECF20u);
  assert_int_equal(mock_reg32_read(ANA_REG13), 0x727070EEu);
  assert_int_equal(mock_reg32_read(ANA_REG25), 0x0961FAA4u);
  assert_int_equal(mock_reg32_read(ANA_REG9) & (1u << 9), 0u);
  assert_int_equal(mock_reg32_read(ANA_REG3) & (1u << 6), (1u << 6));

  expect_vendor_handoff(state);

  /* Cold path printed the "BClk A5=" evidence line: the last byte written
   * to the boot UART FIFO (0x4583001c) must be part of "\r\n" -> '\n'. */
  assert_int_equal(mock_reg32_read(0x4583001cu) & 0xffu, '\n');
}

static void test_spi_timeout_aborts_fail_closed(void **state)
{
  /* ANA_REG5 is register index 5: hold ANA_SPI_STATE bit 5 asserted so the
   * first analog write times out; the sequence aborts and the handoff must
   * NOT be applied.  Console gets the "BClk FAIL" report: last FIFO byte
   * is '\n' from the "\r\n". */
  mock_reg32_write(ANA_SPI_STATE, 1u << 5u);

  boot_clock_cold_init();

  assert_int_equal(mock_reg32_read(CPU_CLK_DIV_MODE1), 0u);
  assert_int_equal(mock_reg32_read(CPU_CLK_DIV_MODE2), 0u);
  assert_int_equal(mock_reg32_read(CPU1_HALT_CLK_OP), 0u);
  assert_int_equal(mock_reg32_read(0x4583001cu) & 0xffu, '\n');
}

static void test_spi_timeout_mid_sequence(void **state)
{
  /* Time out on ANA_REG9 (index 9) instead: steps 1..3 succeed and step 4
   * aborts on its first REG9 latch-on write, so the REG8..25 block and the
   * handoff are never reached. */
  mock_reg32_write(ANA_SPI_STATE, 1u << 9u);

  boot_clock_cold_init();

  assert_int_equal(mock_reg32_read(ANA_REG2), 0x7E003450u);
  assert_int_equal(mock_reg32_read(ANA_REG3), 0xC5F00B88u);
  assert_int_equal(mock_reg32_read(ANA_REG8), 0u);
  assert_int_equal(mock_reg32_read(ANA_REG9), 1u << 9u);
  assert_int_equal(mock_reg32_read(CPU_CLK_DIV_MODE1), 0u);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_warm_path_skips_analog, setup, NULL),
    cmocka_unit_test_setup_teardown(test_cold_path_full_sequence, setup, NULL),
    cmocka_unit_test_setup_teardown(test_spi_timeout_aborts_fail_closed,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_spi_timeout_mid_sequence,
                                    setup, NULL),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}