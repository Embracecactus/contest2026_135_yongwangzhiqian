/****************************************************************************
 * app/testing/bk7258/bk7258_board_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Target-side tests use only NuttX device and BK7258 public diagnostic APIs.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#include <sys/stat.h>

#include <cmocka.h>
#include <unistd.h>

#include <arch/chip/bk7258_amp.h>

#define BK7258_TEST_POLL_US       100000u
#define BK7258_TEST_POLL_ATTEMPTS 100u

static void bk7258_test_console_devices(void **state)
{
  struct stat metadata;

  (void)state;

  assert_int_equal(stat("/dev/console", &metadata), 0);
#ifdef CONFIG_BK7258_UART0
  assert_int_equal(stat("/dev/ttyS0", &metadata), 0);
#endif
}

static void bk7258_test_ap_ready(void **state)
{
  struct bk7258_ap_boot_state_s status;
  unsigned int attempt;

  (void)state;

  for (attempt = 0; attempt < BK7258_TEST_POLL_ATTEMPTS; attempt++)
    {
      bk7258_ap_get_status(&status);
      if (status.state == BK7258_AP_STATE_READY)
        {
          break;
        }

      usleep(BK7258_TEST_POLL_US);
    }

  assert_int_equal(status.magic, BK7258_AP_BOOT_STATE_MAGIC);
  assert_int_equal(status.version, BK7258_AP_BOOT_STATE_VERSION);
  assert_int_equal(status.size, sizeof(status));
  assert_int_equal(status.state, BK7258_AP_STATE_READY);
  assert_int_equal(status.error, BK7258_AP_ERROR_NONE);
}

#ifdef CONFIG_BK7258_AP_SUPERVISOR
static void bk7258_test_ap_supervisor_healthy(void **state)
{
  struct bk7258_ap_supervisor_status_s status;
  unsigned int attempt;
  int ret = -1;

  (void)state;

  for (attempt = 0; attempt < BK7258_TEST_POLL_ATTEMPTS; attempt++)
    {
      ret = bk7258_ap_supervisor_get_status(&status);
      if (ret == 0 && status.state == BK7258_AP_SUPERVISOR_HEALTHY)
        {
          break;
        }

      usleep(BK7258_TEST_POLL_US);
    }

  assert_int_equal(ret, 0);
  assert_int_equal(status.version, BK7258_AP_SUPERVISOR_STATUS_VERSION);
  assert_int_equal(status.size, sizeof(status));
  assert_int_equal(status.state, BK7258_AP_SUPERVISOR_HEALTHY);
  assert_int_equal(status.reason, BK7258_AP_SUPERVISOR_REASON_NONE);
}
#endif

int main(int argc, char *argv[])
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test(bk7258_test_console_devices),
    cmocka_unit_test(bk7258_test_ap_ready),
#ifdef CONFIG_BK7258_AP_SUPERVISOR
    cmocka_unit_test(bk7258_test_ap_supervisor_healthy),
#endif
  };

  (void)argc;
  (void)argv;
  printf("BK7258_TEST_BOARD=%s\n", CONFIG_ARCH_BOARD_CUSTOM_NAME);
  return cmocka_run_group_tests(tests, NULL, NULL);
}
