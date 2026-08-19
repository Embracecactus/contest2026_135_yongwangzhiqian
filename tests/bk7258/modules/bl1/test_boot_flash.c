/*
 * test_boot_flash.c - host unit tests for boot_flash.c (BL1 raw-flash read).
 *
 * The flash controller data window auto-increments on read; the patched
 * source pulls the 8 words through mock_flash_fifo_ref().  The test seeds
 * the FIFO with a distinct byte value per word: word i holds (i+1) in all
 * four byte lanes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "mock_reg32.h"
#include "mock_flash.h"
#include "boot_flash.h"

#define FLASH_OP_CTRL    0x44030010u
#define FLASH_OP_CMD     0x44030054u

#define FLASH_OP_SW      (1u << 29)
#define FLASH_BUSY_SW    (1u << 31)
#define FLASH_COMMAND_SHIFT 24u
#define FLASH_COMMAND_READ 5u

static void seed_fifo(void)
{
  uint8_t words[32];
  uint32_t index;

  for (index = 0; index < 8; index++)
    {
      words[index * 4u + 0u] = (uint8_t)(index + 1u);
      words[index * 4u + 1u] = (uint8_t)(index + 1u);
      words[index * 4u + 2u] = (uint8_t)(index + 1u);
      words[index * 4u + 3u] = (uint8_t)(index + 1u);
    }

  mock_flash_fifo_seed(words);
}

static int setup(void **state)
{
  mock_reg32_reset();
  seed_fifo();
  return 0;
}

static void test_argument_validation(void **state)
{
  uint8_t buffer[64];

  assert_int_equal(bk7258_bl1_flash_read(0u, NULL, 64u), -1);
  assert_int_equal(bk7258_bl1_flash_read(0u, buffer, 0u), -1);
  assert_int_equal(bk7258_bl1_flash_read(0x00800000u, buffer, 1u), -1);
  assert_int_equal(bk7258_bl1_flash_read(0x007fffffu, buffer, 2u), -1);
  assert_int_equal(bk7258_bl1_flash_read(0u, buffer, 0x00800001u), -1);
}

static void test_read_granule_unaligned(void **state)
{
  uint8_t buffer[64];
  uint32_t index;

  /* Reading byte 4 of the granule pulls the same eight FIFO words and
   * copies from offset 4, i.e. the word-1 bytes. */
  assert_int_equal(bk7258_bl1_flash_read(4u, buffer, 4u), 0);
  for (index = 0; index < 4; index++)
    {
      assert_int_equal(buffer[index], 2u);
    }
}

static void test_read_unaligned_source(void **state)
{
  uint8_t buffer[8];

  /* An unaligned source address: the granule is 32-byte aligned, so the
   * copy starts at block[1] (word 0 tail) and spans into word 2. */
  assert_int_equal(bk7258_bl1_flash_read(0x21u, buffer, 8u), 0);
  assert_int_equal(buffer[0], 1u);
  assert_int_equal(buffer[1], 1u);
  assert_int_equal(buffer[2], 1u);
  assert_int_equal(buffer[3], 2u);
  assert_int_equal(buffer[6], 2u);
  assert_int_equal(buffer[7], 3u);
}

static void test_wait_idle_timeout(void **state)
{
  uint8_t buffer[64];

  /* Busy stays asserted: both the pre-command and post-command waits time
   * out after FLASH_WAIT_BUDGET iterations and the read fails. */
  mock_reg32_write(FLASH_OP_CTRL, FLASH_BUSY_SW);
  assert_int_equal(bk7258_bl1_flash_read(0u, buffer, 4u), -1);
}

static void test_command_sequence(void **state)
{
  uint8_t buffer[32];
  uint32_t command;

  mock_reg32_write(FLASH_OP_CTRL, 0);
  mock_reg32_write(FLASH_OP_CMD, 0x12345678u);

  assert_int_equal(bk7258_bl1_flash_read(0x40u, buffer, 32u), 0);

  /* After the read the command register must hold the granule address with
   * the READ opcode and nothing else from the previous value. */
  command = mock_reg32_read(FLASH_OP_CMD);
  assert_int_equal(command & 0x00ffffffu, 0x40u);
  assert_int_equal((command >> FLASH_COMMAND_SHIFT) & 0x1fu,
                   FLASH_COMMAND_READ);
  assert_int_equal(mock_reg32_read(FLASH_OP_CTRL) & FLASH_OP_SW,
                   FLASH_OP_SW);
}

static void test_read_across_granules(void **state)
{
  uint8_t buffer[40];
  uint32_t index;

  /* Two granules: [0x20,0x40) and [0x40,0x48).  Each granule starts a fresh
   * 8-word FIFO read (the auto-increment window wraps), so the second
   * granule re-reads words 0..1 and its 8 bytes replicate the head. */
  assert_int_equal(bk7258_bl1_flash_read(0x20u, buffer, 40u), 0);
  for (index = 0; index < 8; index++)
    {
      assert_int_equal(buffer[index * 4u], (uint8_t)(index + 1u));
    }

  assert_int_equal(buffer[32u], 1u);
  assert_int_equal(buffer[36u], 2u);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_argument_validation, setup, NULL),
    cmocka_unit_test_setup_teardown(test_read_granule_unaligned, setup, NULL),
    cmocka_unit_test_setup_teardown(test_read_unaligned_source,
                                    setup, NULL),
    cmocka_unit_test_setup_teardown(test_wait_idle_timeout, setup, NULL),
    cmocka_unit_test_setup_teardown(test_command_sequence, setup, NULL),
    cmocka_unit_test_setup_teardown(test_read_across_granules, setup, NULL),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}