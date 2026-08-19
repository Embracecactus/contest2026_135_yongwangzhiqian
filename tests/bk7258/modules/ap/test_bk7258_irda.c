/****************************************************************************
 * tests/bk7258/modules/ap/test_bk7258_irda.c
 *
 * Host unit suite for board/bk7258/chip/cp/bk7258_irda.c (true source,
 * patched copy built via framework/patch.py, see Makefile).
 *
 * The driver is the board-owned register-level NEC decoder against the
 * BK7258 IRDA base 0x458b0000.  patch.py routes bk7258_irda_reg_read/write
 * through mock_reg32, so every register transaction lands in the mock RAM
 * window.  The SDK surface (gpio_dev_map, bk_int_isr_register,
 * register_driver) is programmable via framework/mock_sdk_irda, and the
 * key-debounce wdog is fired explicitly via mock_wdog_fire().
 *
 * NEC frame injection:
 *   - leader:   IRDA_INT |= RIGHT
 *   - data:     IRDA_RX_FIFO = 32-bit key, IRDA_INT |= END
 *   - repeat:   IRDA_INT |= REPEAT
 * The registered ISR is fetched from mock_irda_isr() after open().
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "mock_sdk_irda.h"
#include "../../framework/mock_reg32.h"
#include <nuttx/fs/fs.h>
#include <nuttx/wdog.h>
#include <arch/chip/bk7258_irda.h>

#ifndef OK
#define OK 0
#endif

/* The char-device callbacks are exposed (non-static) in the patched copy. */

extern int bk7258_irda_open(FAR struct file *filep);
extern int bk7258_irda_close(FAR struct file *filep);
extern ssize_t bk7258_irda_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen);
extern ssize_t bk7258_irda_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen);
extern int bk7258_irda_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg);

/* Host-only reset hook injected by framework/patch.py into the patched
 * copy: clears the driver singleton so every test starts clean. */

extern void bk7258_irda_test_reset(void);

#define GET_KEY_TYPE(msg)  (((msg) >> 24) & 0xffu)
#define GET_KEY_VALUE(msg) ((msg) & 0xffu)

/* Register addresses (must match the driver). */

#define IRDA_CTRL     0x458b0000u
#define IRDA_INT_MASK 0x458b0004u
#define IRDA_INT      0x458b0008u
#define IRDA_RX_FIFO  0x458b000cu

#define IRDA_NEC_EN     (1u << 0)
#define IRDA_POLARITY   (1u << 1)
#define IRDA_CLK_POSI   8
#define IRDA_CLK_MASK   0xffffu

#define IRDA_END_INT    (1u << 0)
#define IRDA_RIGHT_INT  (1u << 1)
#define IRDA_REPEAT_INT (1u << 2)

#define IRDA_KEY_CODE_MASK     0xff0000u
#define IRDA_KEY_CODE_SHIFT    16
#define IRDA_KEY_CODE_INV_MASK 0xff000000u
#define IRDA_KEY_CODE_INV_SHIFT 24

#define TEST_USERCODE   0x1234u
#define TEST_KEY_CODE   0x5au

/* A valid NEC key word for TEST_USERCODE / TEST_KEY_CODE. */

#define NEC_KEY(u, k) \
  ((uint32_t)(u) | ((uint32_t)(k) << IRDA_KEY_CODE_SHIFT) | \
   ((uint32_t)(~(k) & 0xffu) << IRDA_KEY_CODE_INV_SHIFT))

static int irda_group_setup(void **state)
{
  (void)state;

  mock_irda_sdk_reset();
  mock_reg32_reset();
  mock_wdog_reset();
  bk7258_irda_test_reset();
  return 0;
}

static int irda_open_device(void)
{
  int ret;

  ret = bk7258_irda_initialize();
  assert_int_equal(ret, OK);

  ret = bk7258_irda_open(NULL);
  assert_int_equal(ret, OK);

  /* The NEC decoder compares the frame user code against the configured
   * code; every frame in this suite uses TEST_USERCODE.
   */

  ret = bk7258_irda_ioctl(NULL, BKIOC_IRDA_SET_USERCODE, TEST_USERCODE);
  assert_int_equal(ret, OK);
  return ret;
}

static void irda_close_device(void)
{
  (void)bk7258_irda_close(NULL);
}

static void (*irda_isr(void))(void)
{
  void (*isr)(void) = mock_irda_isr();

  assert_non_null(isr);
  return isr;
}

static void irda_fire_leader(void)
{
  void (*isr)(void) = irda_isr();

  mock_reg32_write(IRDA_INT, IRDA_RIGHT_INT);
  isr();
}

static void irda_fire_end(uint32_t key)
{
  void (*isr)(void) = irda_isr();

  mock_reg32_write(IRDA_RX_FIFO, key);
  mock_reg32_write(IRDA_INT, IRDA_END_INT);
  isr();
}

static void irda_fire_repeat(void)
{
  void (*isr)(void) = irda_isr();

  mock_reg32_write(IRDA_INT, IRDA_REPEAT_INT);
  isr();
}

/* ------------------------------------------------------------------ */
/* Initialize / open                                                   */
/* ------------------------------------------------------------------ */

static void test_irda_initialize_registers(void **state)
{
  int ret;

  (void)state;

  ret = bk7258_irda_initialize();
  assert_int_equal(ret, OK);

  assert_int_equal(mock_irda_fs_register_calls(), 1);
  assert_string_equal(mock_irda_fs_register_path(), "/dev/irda0");

  /* Double initialize must be refused. */

  ret = bk7258_irda_initialize();
  assert_int_equal(ret, -EBUSY);
}

static void test_irda_open_bringup(void **state)
{
  (void)state;

  (void)irda_open_device();

  /* Receiver GPIO mux and INT_SRC_IRDA hook. */

  assert_int_equal(mock_irda_gpio_calls(), 1);
  assert_int_equal(mock_irda_gpio_call(0)->id, 25);
  assert_int_equal(mock_irda_int_calls(), 1);
  assert_int_equal(mock_irda_int_call(0)->src, 32);
  assert_true(mock_irda_int_call(0)->isr_registered);

  /* Receiver configuration: NEC enabled, polarity 0, clock divider, and
   * the RIGHT/REPEAT/END interrupt mask.
   */

  uint32_t ctrl = mock_reg32_read(IRDA_CTRL);
  assert_true((ctrl & IRDA_NEC_EN) != 0);
  assert_true((ctrl & IRDA_POLARITY) == 0);
  assert_int_equal((ctrl >> IRDA_CLK_POSI) & IRDA_CLK_MASK, 0x3921);

  assert_int_equal(mock_reg32_read(IRDA_INT_MASK),
                   IRDA_RIGHT_INT | IRDA_REPEAT_INT | IRDA_END_INT);

  irda_close_device();
}

static void test_irda_open_double(void **state)
{
  int ret;

  (void)state;

  (void)irda_open_device();
  ret = bk7258_irda_open(NULL);
  assert_int_equal(ret, -EBUSY);

  irda_close_device();
}

/* ------------------------------------------------------------------ */
/* NEC decode                                                          */
/* ------------------------------------------------------------------ */

static void test_irda_nec_valid_short_key(void **state)
{
  uint32_t key;
  ssize_t n;

  (void)state;

  (void)irda_open_device();

  irda_fire_leader();
  irda_fire_end(NEC_KEY(TEST_USERCODE, TEST_KEY_CODE));

  /* Debounce wdog armed after a valid frame. */

  assert_int_equal(mock_wdog_pending_count(), 1);

  /* No repeat: first wdog fire classifies as SHORT and posts the key. */

  mock_wdog_fire();

  n = bk7258_irda_read(NULL, (char *)&key, sizeof(key));
  assert_int_equal(n, (ssize_t)sizeof(key));
  assert_int_equal(GET_KEY_TYPE(key), BK7258_IRDA_KEY_SHORT);
  assert_int_equal(GET_KEY_VALUE(key), TEST_KEY_CODE);

  irda_close_device();
}

static void test_irda_nec_invalid_usercode(void **state)
{
  (void)state;

  (void)irda_open_device();

  /* Wrong user code: frame rejected, no wdog armed, no key queued. */

  irda_fire_leader();
  irda_fire_end(NEC_KEY(0x0001u, TEST_KEY_CODE));

  assert_int_equal(mock_wdog_pending_count(), 0);

  irda_close_device();
}

static void test_irda_nec_invalid_inverse(void **state)
{
  (void)state;

  (void)irda_open_device();

  /* Key-code inverse byte does not complement: rejected. */

  irda_fire_leader();
  irda_fire_end(TEST_USERCODE | (TEST_KEY_CODE << IRDA_KEY_CODE_SHIFT));

  assert_int_equal(mock_wdog_pending_count(), 0);

  irda_close_device();
}

static void test_irda_nec_long_key(void **state)
{
  uint32_t key;
  ssize_t n;
  int i;

  (void)state;

  (void)irda_open_device();

  irda_fire_leader();
  irda_fire_end(NEC_KEY(TEST_USERCODE, TEST_KEY_CODE));

  /* Three repeat codes: the frame becomes LONG after the debounce
   * timer observes repeat_cnt (3) below the long threshold (8) but at
   * or above the short threshold (3).
   */

  for (i = 0; i < 3; i++)
    {
      irda_fire_repeat();
    }

  /* Fire the wdog until the classification posts. */

  for (i = 0; i < 5; i++)
    {
      mock_wdog_fire();
    }

  n = bk7258_irda_read(NULL, (char *)&key, sizeof(key));
  assert_int_equal(n, (ssize_t)sizeof(key));
  assert_int_equal(GET_KEY_TYPE(key), BK7258_IRDA_KEY_LONG);
  assert_int_equal(GET_KEY_VALUE(key), TEST_KEY_CODE);

  irda_close_device();
}

static void test_irda_nec_hold_key(void **state)
{
  uint32_t key;
  ssize_t n;
  int i;

  (void)state;

  (void)irda_open_device();

  irda_fire_leader();
  irda_fire_end(NEC_KEY(TEST_USERCODE, TEST_KEY_CODE));

  /* Eight repeat codes: the frame classifies as HOLD. */

  for (i = 0; i < 8; i++)
    {
      irda_fire_repeat();
    }

  for (i = 0; i < 10; i++)
    {
      mock_wdog_fire();
    }

  n = bk7258_irda_read(NULL, (char *)&key, sizeof(key));
  assert_int_equal(n, (ssize_t)sizeof(key));
  assert_int_equal(GET_KEY_TYPE(key), BK7258_IRDA_KEY_HOLD);
  assert_int_equal(GET_KEY_VALUE(key), TEST_KEY_CODE);

  irda_close_device();
}

/* ------------------------------------------------------------------ */
/* read / write / ioctl / close                                        */
/* ------------------------------------------------------------------ */

static void test_irda_read_unsupported_write(void **state)
{
  ssize_t n;

  (void)state;

  (void)irda_open_device();

  n = bk7258_irda_write(NULL, (const char *)"x", 1);
  assert_int_equal(n, -ENOTSUP);

  irda_close_device();
}

static void test_irda_ioctl_usercode(void **state)
{
  uint32_t key;
  ssize_t n;
  int ret;

  (void)state;

  (void)irda_open_device();

  ret = bk7258_irda_ioctl(NULL, BKIOC_IRDA_SET_USERCODE, 0x5678u);
  assert_int_equal(ret, OK);

  /* Frame with the new user code decodes. */

  irda_fire_leader();
  irda_fire_end(NEC_KEY(0x5678u, TEST_KEY_CODE));
  mock_wdog_fire();

  n = bk7258_irda_read(NULL, (char *)&key, sizeof(key));
  assert_int_equal(n, (ssize_t)sizeof(key));
  assert_int_equal(GET_KEY_VALUE(key), TEST_KEY_CODE);

  /* Unknown ioctl is refused. */

  ret = bk7258_irda_ioctl(NULL, 0xdeadbeef, 0);
  assert_int_equal(ret, -ENOTTY);

  irda_close_device();
}

static void test_irda_close_teardown(void **state)
{
  (void)state;

  (void)irda_open_device();
  irda_close_device();

  /* Receiver disabled: interrupt mask cleared, NEC disabled. */

  assert_int_equal(mock_reg32_read(IRDA_INT_MASK), 0);
  assert_true((mock_reg32_read(IRDA_CTRL) & IRDA_NEC_EN) == 0);
}

/* ------------------------------------------------------------------ */

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup(test_irda_initialize_registers, irda_group_setup),
    cmocka_unit_test_setup(test_irda_open_bringup, irda_group_setup),
    cmocka_unit_test_setup(test_irda_open_double, irda_group_setup),
    cmocka_unit_test_setup(test_irda_nec_valid_short_key, irda_group_setup),
    cmocka_unit_test_setup(test_irda_nec_invalid_usercode, irda_group_setup),
    cmocka_unit_test_setup(test_irda_nec_invalid_inverse, irda_group_setup),
    cmocka_unit_test_setup(test_irda_nec_long_key, irda_group_setup),
    cmocka_unit_test_setup(test_irda_nec_hold_key, irda_group_setup),
    cmocka_unit_test_setup(test_irda_read_unsupported_write, irda_group_setup),
    cmocka_unit_test_setup(test_irda_ioctl_usercode, irda_group_setup),
    cmocka_unit_test_setup(test_irda_close_teardown, irda_group_setup),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
