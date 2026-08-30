/*
 * test_boot_runtime.c - host unit tests for boot_runtime.c (reset/handoff).
 *
 * MMIO is redirected into the framework map; the ARM barriers are
 * neutralized by the patch profile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "mock_reg32.h"
#include "boot_runtime.h"

#define SYS_BASE           0x44010000u
#define SYS_CPU_RUN_STATUS (SYS_BASE + 0x0cu)
#define SYS_CPU1_CONTROL   (SYS_BASE + 0x14u)
#define SYS_CPU2_CONTROL   (SYS_BASE + 0x18u)
#define SYS_FLASH_CLOCK    (SYS_BASE + 0x24u)
#define SYS_BOOT_UART_CLOCK (SYS_BASE + 0x30u)
#define SYS_UART1_DEVICE_CLK (SYS_BASE + 0x80u)

#define FLASH_CTRL_BASE   0x44030000u
#define FLASH_OP_CTRL     (FLASH_CTRL_BASE + 0x10u)
#define FLASH_ID          (FLASH_CTRL_BASE + 0x20u)
#define FLASH_CONFIG      (FLASH_CTRL_BASE + 0x28u)
#define FLASH_OP_CMD      (FLASH_CTRL_BASE + 0x54u)
#define FLASH_BUSY        (1u << 31)
#define FLASH_OP_SW       (1u << 29)
#define FLASH_OP_TYPE_RDID (20u << 24)
#define SYS_FLASH_DIV_MASK (0x3u << 26)
#define SYS_FLASH_DIV_FAST (0x1u << 26)
#define SYS_FLASH_DIV_SAFE (0x3u << 26)

#define OTP_PUF_BUSY       0x4b1002c4u
#define OTP_MEM_REPAIR_CTRL 0x4b1002c8u
#define OTP_MEM_REPAIR_VALUE 0x4b1007c8u
#define MEM_CHECK_BASE     0x44890000u

#define AP_BOOT_STATE_MAGIC 0x2809f000u

#define SCB_VTOR            0xe000ed08u
#define SCB_CCR             0xe000ed14u
#define SCB_SHCSR           0xe000ed24u
#define SCB_CLIDR           0xe000ed78u
#define SCB_CCSIDR          0xe000ed80u
#define SCB_CSSELR          0xe000ed84u
#define SCB_ICIALLU         0xe000ef50u
#define SCB_DCISW           0xe000ef60u
#define SCB_DCCISW          0xe000ef74u
#define SCB_CCR_DC          (1u << 16)
#define SCB_CCR_IC          (1u << 17)
#define SCB_SHCSR_MEMFAULTENA (1u << 16)
#define MPU_TYPE            0xe000ed90u
#define MPU_CTRL            0xe000ed94u
#define MPU_RNR             0xe000ed98u
#define MPU_RLAR            0xe000eda0u
#define ITCMCR              0xe001e010u
#define DTCMCR              0xe001e014u

#define BOOT_UART1_BASE     0x45830000u
#define BOOT_UART_CONFIG    (BOOT_UART1_BASE + 0x10u)
#define BOOT_UART_FIFO_CONFIG (BOOT_UART1_BASE + 0x14u)
#define BOOT_UART_FIFO_STATUS (BOOT_UART1_BASE + 0x18u)
#define BOOT_UART_INT_ENABLE (BOOT_UART1_BASE + 0x20u)
#define BOOT_UART_TX_FINISHED (1u << 17)

static int setup(void **state)
{
  mock_reg32_reset();
  return 0;
}

static void test_early_soc_init(void **state)
{
  /* 0x44010040 bit 3 set -> cleared. */
  mock_reg32_write(SYS_BASE + 0x40u, 1u << 3u);
  /* 0x44010030 bit 15 clear -> set. */
  mock_reg32_write(SYS_BASE + 0x30u, 0u);
  /* OTP mem repair ctrl low bits not 3 -> or 3. */
  mock_reg32_write(OTP_MEM_REPAIR_CTRL, 0x5u);
  /* PUF idle and repair value 7 -> MEM_CHECK write. */
  mock_reg32_write(OTP_PUF_BUSY, 0u);
  mock_reg32_write(OTP_MEM_REPAIR_VALUE, 0x7u);

  boot_reset_prepare();

  assert_int_equal(mock_reg32_read(SYS_BASE + 0x40u), 0u);
  assert_int_equal(mock_reg32_read(SYS_BASE + 0x30u) & (1u << 15u),
                   (1u << 15u));
  assert_int_equal(mock_reg32_read(OTP_MEM_REPAIR_CTRL) & 0x3u, 0x3u);
  assert_int_equal(mock_reg32_read(MEM_CHECK_BASE + 0x08u), 0x7u);
}

static void test_early_soc_init_rmw_preserves_masks(void **state)
{
  /* 0x44010030 already at bit 15: must not be written back with spurious
   * bits, but the read-modify-write preserves other bits. */
  mock_reg32_write(SYS_BASE + 0x30u, 0x8000u | 0x0002u);
  mock_reg32_write(OTP_MEM_REPAIR_CTRL, 0x3u);
  mock_reg32_write(OTP_PUF_BUSY, 1u);
  mock_reg32_write(OTP_MEM_REPAIR_VALUE, 0x0u);

  boot_reset_prepare();

  assert_int_equal(mock_reg32_read(SYS_BASE + 0x30u), 0x8002u);
  assert_int_equal(mock_reg32_read(OTP_MEM_REPAIR_CTRL), 0x3u);
  assert_int_equal(mock_reg32_read(MEM_CHECK_BASE + 0x08u), 0u);
}

static void test_flash_reset_prepare_known_id_fast(void **state)
{
  mock_reg32_write(FLASH_ID, 0x00c86516u);

  boot_flash_reset_prepare();

  /* The divisor field is two bits: FAST is bit 26 with SAFE as its
   * superset, so compare the whole field. */
  assert_int_equal(mock_reg32_read(SYS_FLASH_CLOCK) & SYS_FLASH_DIV_MASK,
                   SYS_FLASH_DIV_FAST);
  /* RDID operation issued, op_sw asserted. */
  assert_int_equal(mock_reg32_read(FLASH_OP_CMD) & (0x1fu << 24u),
                   FLASH_OP_TYPE_RDID);
  assert_int_equal(mock_reg32_read(FLASH_OP_CTRL) & FLASH_OP_SW, FLASH_OP_SW);
  /* The run-status bit 9 was cleared. */
  assert_int_equal(mock_reg32_read(SYS_CPU_RUN_STATUS) & (1u << 9u), 0u);
}

static void test_flash_reset_prepare_unknown_id_safe(void **state)
{
  mock_reg32_write(FLASH_ID, 0x12345678u);

  boot_flash_reset_prepare();

  assert_int_equal(mock_reg32_read(SYS_FLASH_CLOCK) & SYS_FLASH_DIV_MASK,
                   SYS_FLASH_DIV_SAFE);
}

static void test_secondary_cores_power_down(void **state)
{
  /* Both cores already stopped (mock hosts no run-status transition
   * logic): the stop loop returns immediately, and both sub-sequences ran,
   * so the AP session is invalidated. */
  mock_reg32_write(SYS_CPU_RUN_STATUS, 0u);
  mock_reg32_write(AP_BOOT_STATE_MAGIC, 0x5a5aa5a5u);

  boot_reset_prepare();

  /* CPU2 stopped first, then CPU1: both have reset cleared, halt set,
   * power-down set. */
  {
    uint32_t c1 = mock_reg32_read(SYS_CPU1_CONTROL);
    uint32_t c2 = mock_reg32_read(SYS_CPU2_CONTROL);

    assert_int_equal(c1 & ((1u << 0u) | (1u << 1u) | (1u << 3u)),
                     (1u << 1u) | (1u << 3u));
    assert_int_equal(c2 & ((1u << 0u) | (1u << 1u) | (1u << 3u)),
                     (1u << 1u) | (1u << 3u));
  }

  /* Both stopped -> AP session invalidated. */
  assert_int_equal(mock_reg32_read(AP_BOOT_STATE_MAGIC), 0u);
  /* VTOR/vector establish and TCM enables. */
  assert_int_equal(mock_reg32_read(SCB_VTOR), 0x02000000u);
}

static void test_secondary_core_stuck_keeps_session(void **state)
{
  /* CPU1's running bit never deasserts: the stop loop times out and the AP
   * session must be preserved (fail-closed). */
  mock_reg32_write(SYS_CPU_RUN_STATUS, (1u << 5u) | (1u << 6u));
  mock_reg32_write(AP_BOOT_STATE_MAGIC, 0xabcd1234u);

  boot_reset_prepare();

  assert_int_equal(mock_reg32_read(AP_BOOT_STATE_MAGIC), 0xabcd1234u);
}

static void test_icache_enable_path(void **state)
{
  /* CLIDR bit0 set (I-cache implemented), CCR IC clear: full invalidate +
   * enable.  CCSIDR zero keeps the set/way sweep to one iteration. */
  mock_reg32_write(SCB_CLIDR, 1u);

  boot_reset_prepare();

  assert_int_equal(mock_reg32_read(SCB_CCR) & SCB_CCR_IC, SCB_CCR_IC);
  assert_int_equal(mock_reg32_read(SCB_ICIALLU), 0u);
}

static void test_app_handoff_prepare(void **state)
{
  mock_reg32_write(SCB_CCR, SCB_CCR_DC | SCB_CCR_IC);
  mock_reg32_write(SCB_SHCSR, SCB_SHCSR_MEMFAULTENA);
  mock_reg32_write(MPU_CTRL, 1u);

  boot_prepare_app_handoff();

  assert_int_equal(mock_reg32_read(SCB_CCR) & SCB_CCR_DC, 0u);
  assert_int_equal(mock_reg32_read(SCB_SHCSR) & SCB_SHCSR_MEMFAULTENA, 0u);
  assert_int_equal(mock_reg32_read(MPU_CTRL) & 1u, 0u);
  /* All MPU regions were walked to 0: the loop stopped at region 15 (16
   * regions on BK7258), and the last RLAR clear is visible. */
  assert_int_equal(mock_reg32_read(MPU_RNR), 15u);
  assert_int_equal(mock_reg32_read(MPU_RLAR), 0u);
}

static void test_console_prepare_app_handoff(void **state)
{
  /* TX finished already set: no drain, then quiesce register writes. */
  mock_reg32_write(BOOT_UART_FIFO_STATUS, BOOT_UART_TX_FINISHED);
  mock_reg32_write(BOOT_UART_CONFIG, 0xffffffffu);
  mock_reg32_write(BOOT_UART_FIFO_CONFIG, 0xffffffffu);
  mock_reg32_write(BOOT_UART_INT_ENABLE, 0xffffffffu);
  mock_reg32_write(SYS_UART1_DEVICE_CLK, 0xffffffffu);
  mock_reg32_write(SYS_BOOT_UART_CLOCK, 0xffffffffu);

  boot_console_prepare_app_handoff();

  assert_int_equal(mock_reg32_read(BOOT_UART_CONFIG), 0xffffffe4u);
  assert_int_equal(mock_reg32_read(BOOT_UART_FIFO_CONFIG), 0xffffcfbfu);
  assert_int_equal(mock_reg32_read(BOOT_UART_INT_ENABLE), 0xffffffbdu);
  assert_int_equal(mock_reg32_read(SYS_UART1_DEVICE_CLK), 0xffff7fffu);
  assert_int_equal(mock_reg32_read(SYS_BOOT_UART_CLOCK), 0xfffffbffu);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_early_soc_init, setup, NULL),
    cmocka_unit_test_setup_teardown(test_early_soc_init_rmw_preserves_masks,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_flash_reset_prepare_known_id_fast,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_flash_reset_prepare_unknown_id_safe,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_secondary_cores_power_down,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_secondary_core_stuck_keeps_session,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_icache_enable_path, setup, NULL),
    cmocka_unit_test_setup_teardown(test_app_handoff_prepare, setup, NULL),
    cmocka_unit_test_setup_teardown(test_console_prepare_app_handoff,
                                    setup, NULL),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}