/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/modules/ap/test_bk7258_can.c
 *
 * Host test suite for the BK7258 classic-CAN lower-half
 * (chips/bk7258/ap/bk7258_can.c) against the SDK mock
 * (framework/mock_sdk_can.c).  The driver is compiled UNMODIFIED:
 *
 *   - The rx thread is a real pthread: fire_rx_isr() posts the driver's
 *     rx_sem, the thread drains the SDK FIFO (bk_can_receive) and calls
 *     the upper-half can_receive() record.
 *   - Tests synchronize with the rx thread by polling the upper-half
 *     record (3 s deadline), never by sleeping for a fixed duration.
 *   - Every test that enables setup() must leave the driver uninitialized
 *     (uninitialize stops and joins the rx thread) so the singleton and
 *     mock are clean for the next test.
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <cmocka.h>

#include <nuttx/can/can.h>

#include <arch/chip/bk7258_can.h>

#include "mock_sdk_can.h"

static FAR struct can_dev_s *g_dev;
static FAR const struct can_ops_s *g_ops;

static int test_setup(FAR void **state)
{
  mock_sdk_can_reset();
  g_dev = NULL;
  g_ops = NULL;
  return 0;
}

static int test_teardown(FAR void **state)
{
  /* If the test left the driver initialized, tear it down so the rx
   * thread is joined before the mock state is discarded. */
  if (g_dev != NULL)
    {
      (void)bk7258_can_uninitialize(g_dev);
    }

  return 0;
}

static void mock_can_poll_until(unsigned int want)
{
  struct timespec a;
  struct timespec b;

  clock_gettime(CLOCK_MONOTONIC, &a);
  while (mock_can_upper_rx_count() < want)
    {
      clock_gettime(CLOCK_MONOTONIC, &b);
      assert_true(b.tv_sec - a.tv_sec < 3);
      usleep(500);
    }
}

static int mock_can_find_call(enum mock_sdk_can_fn_e fn)
{
  FAR struct mock_can_log_s *log = mock_sdk_can_log();
  int i;

  for (i = 0; i < log->count; i++)
    {
      if (log->call[i].fn == fn)
        {
          return i;
        }
    }

  return -1;
}

static void mock_can_settle_rx(void)
{
  int i;

  for (i = 0; i < 50 && !mock_sdk_can_rx_empty(); i++)
    {
      mock_sdk_can_fire_rx_isr();
      usleep(2000);
    }

  assert_true(mock_sdk_can_rx_empty());
}

static void mock_can_setup_dev(void)
{
  assert_int_equal(bk7258_can_initialize(&g_dev), 0);
  g_ops = g_dev->cd_ops;
}

static void mock_can_setup_configured(void)
{
  mock_can_setup_dev();
  assert_int_equal(g_ops->co_setup(g_dev), 0);
  g_ops->co_rxint(g_dev, true);
}

static void mock_can_send_frame(uint16_t id, uint8_t dlc,
                                FAR const uint8_t *data)
{
  struct can_msg_s msg;

  memset(&msg, 0, sizeof(msg));
  msg.cm_hdr.ch_id = id;
  msg.cm_hdr.ch_dlc = dlc;
  if (data != NULL)
    {
      memcpy(msg.cm_data, data, dlc);
    }

  assert_int_equal(g_ops->co_send(g_dev, &msg), 0);
}

static void test_can_init_ok(FAR void **state)
{
  FAR struct can_dev_s *dev = NULL;

  assert_int_equal(bk7258_can_initialize(&dev), 0);
  assert_non_null(dev);
  assert_non_null(dev->cd_ops);
  assert_non_null(dev->cd_priv);
  assert_int_equal(dev->cd_crefs, 0);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_DRIVER_INIT), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_REGISTER_ISR), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_REGISTER_ERR), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_SET_LBMI), 1);
  assert_int_equal(mock_sdk_can_call(0)->fn, MOCK_CAN_FN_DRIVER_INIT);
  assert_int_equal(mock_sdk_can_call(1)->fn, MOCK_CAN_FN_REGISTER_ISR);
  assert_int_equal(mock_sdk_can_call(2)->fn, MOCK_CAN_FN_REGISTER_ERR);
  assert_int_equal(mock_sdk_can_call(3)->fn, MOCK_CAN_FN_SET_LBMI);
  assert_int_equal(mock_sdk_can_call(3)->a0, 0);

  assert_int_equal(bk7258_can_uninitialize(dev), 0);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_DRIVER_DEINIT), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_ABORT_ALL), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_REGISTER_ISR), 2);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_REGISTER_ERR), 2);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_SET_LBMI), 2);
  g_dev = NULL;
}

static void test_can_init_double(FAR void **state)
{
  FAR struct can_dev_s *dev = NULL;
  FAR struct can_dev_s *again = NULL;

  assert_int_equal(bk7258_can_initialize(&dev), 0);
  assert_int_equal(bk7258_can_initialize(&again), -EBUSY);
  assert_null(again);
  assert_int_equal(bk7258_can_uninitialize(dev), 0);
  g_dev = NULL;
}

static void test_can_init_driver_init_fail(FAR void **state)
{
  FAR struct can_dev_s *dev = NULL;

  mock_sdk_can_set_ret(MOCK_CAN_FN_DRIVER_INIT, BK_ERR_NOT_INIT);
  assert_int_equal(bk7258_can_initialize(&dev), -ENODEV);
  assert_null(dev);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_DRIVER_DEINIT), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_REGISTER_ISR), 0);

  mock_sdk_can_set_ret(MOCK_CAN_FN_DRIVER_INIT, 0);
  assert_int_equal(bk7258_can_initialize(&dev), 0);
  assert_non_null(dev);
  assert_int_equal(bk7258_can_uninitialize(dev), 0);
  g_dev = NULL;
}

static void test_can_uninit_never_init(FAR void **state)
{
  FAR struct can_dev_s *dev = NULL;

  assert_int_equal(bk7258_can_uninitialize(dev), -EINVAL);
  assert_int_equal(bk7258_can_uninitialize(NULL), -EINVAL);

  /* A stray non-owning pointer must be rejected. */
  assert_int_equal(bk7258_can_uninitialize((FAR struct can_dev_s *)0x1), -EINVAL);
}

static void test_can_uninit_wrong_dev(FAR void **state)
{
  struct can_dev_s foreign;

  memset(&foreign, 0, sizeof(foreign));
  mock_can_setup_dev();
  assert_int_equal(bk7258_can_uninitialize(&foreign), -EINVAL);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_uninit_crefs(FAR void **state)
{
  mock_can_setup_dev();
  g_dev->cd_crefs = 1;
  assert_int_equal(bk7258_can_uninitialize(g_dev), -EBUSY);
  g_dev->cd_crefs = 0;
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_setup_ok(FAR void **state)
{
  static const uint8_t payload[4] = { 5, 6, 7, 8 };

  mock_can_setup_dev();
  assert_int_equal(g_ops->co_setup(g_dev), 0);
  assert_int_equal(mock_can_kthread_count(), 1);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_SET_LBMI), 2);

  g_ops->co_rxint(g_dev, true);
  mock_sdk_can_rx_enqueue(0x345, payload, sizeof(payload));
  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(1);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_id, 0x345);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_dlc, 4);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_rtr, 0);
  assert_memory_equal(mock_can_upper_rx(0)->data, payload, sizeof(payload));
  assert_true(mock_sdk_can_rx_empty());

  g_ops->co_txint(g_dev, true);
  mock_can_send_frame(0x123, 3, payload);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_SEND_PTB), 1);
  assert_int_equal(mock_sdk_can_last_tx_id(), 0x123);
  assert_int_equal(mock_sdk_can_last_tx_size(), 3);
  assert_memory_equal(mock_sdk_can_last_tx_payload(), payload, 3);
  mock_sdk_can_fire_tx_isr();
  assert_int_equal(mock_can_upper_txdone_count(), 1);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_setup_gate(FAR void **state)
{
  FAR struct can_dev_s *dev = NULL;

  assert_int_equal(bk7258_can_initialize(&dev), 0);
  g_ops = dev->cd_ops;
  g_dev = dev;

  assert_int_equal(g_ops->co_setup(dev), 0);
  assert_int_equal(g_ops->co_setup(dev), -EBUSY);
  g_ops->co_shutdown(dev);
  assert_int_equal(g_ops->co_setup(dev), 0);
  assert_int_equal(mock_can_kthread_count(), 2);
  assert_int_equal(bk7258_can_uninitialize(dev), 0);
  g_dev = NULL;
}

static void test_can_stale_ops_after_uninit(FAR void **state)
{
  struct canioc_connmodes_s modes;
  struct canioc_bittiming_s timing;
  struct can_msg_s msg;

  mock_can_setup_configured();
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);

  /* The lower-half is a single-instance singleton; after uninitialize
   * every entry point must reject without touching the SDK. */
  assert_int_equal(g_ops->co_setup(g_dev), -ENODEV);
  memset(&msg, 0, sizeof(msg));
  msg.cm_hdr.ch_id = 0x100;
  msg.cm_hdr.ch_dlc = 2;
  assert_int_equal(g_ops->co_send(g_dev, &msg), -ENODEV);
  memset(&modes, 0, sizeof(modes));
  modes.bm_loopback = 1;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_CONNMODES,
                                   (unsigned long)&modes), -ENODEV);
  memset(&timing, 0, sizeof(timing));
  timing.bt_baud = 500000;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_BITTIMING,
                                   (unsigned long)&timing), -ENODEV);
  assert_int_equal(g_ops->co_cancel(g_dev, &msg), false);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_SEND_PTB), 0);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_ABORT_PTB), 0);

  g_dev = NULL;
}

static void test_can_send_gate(FAR void **state)
{
  struct can_msg_s msg;
  struct can_msg_s rtr;
  struct can_msg_s big;
  struct can_msg_s wide;
  static const uint8_t data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

  mock_can_setup_configured();

  memset(&msg, 0, sizeof(msg));
  msg.cm_hdr.ch_id = 0x100;
  msg.cm_hdr.ch_dlc = 8;
  memcpy(msg.cm_data, data, 8);
  assert_int_equal(g_ops->co_send(g_dev, &msg), 0);
  mock_sdk_can_fire_tx_isr();

  /* CAN permits a zero-length data frame, including identifier zero. */
  memset(&msg, 0, sizeof(msg));
  assert_int_equal(g_ops->co_send(g_dev, &msg), 0);
  assert_int_equal(mock_sdk_can_last_tx_id(), 0u);
  assert_int_equal(mock_sdk_can_last_tx_size(), 0u);
  mock_sdk_can_fire_tx_isr();
  assert_int_equal(g_ops->co_send(g_dev, NULL), -EINVAL);

  memset(&big, 0, sizeof(big));
  big.cm_hdr.ch_id = 0x100;
  big.cm_hdr.ch_dlc = 9;
  assert_int_equal(g_ops->co_send(g_dev, &big), -EINVAL);

  memset(&rtr, 0, sizeof(rtr));
  rtr.cm_hdr.ch_id = 0x100;
  rtr.cm_hdr.ch_dlc = 8;
  rtr.cm_hdr.ch_rtr = 1;
  assert_int_equal(g_ops->co_send(g_dev, &rtr), -ENOTSUP);

  memset(&wide, 0, sizeof(wide));
  wide.cm_hdr.ch_id = 0x800;
  wide.cm_hdr.ch_dlc = 8;
  assert_int_equal(g_ops->co_send(g_dev, &wide), -EINVAL);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_send_busy(FAR void **state)
{
  struct can_msg_s msg;
  static const uint8_t data[2] = { 1, 2 };

  mock_can_setup_configured();
  g_ops->co_txint(g_dev, true);

  mock_can_send_frame(0x100, 2, data);
  memset(&msg, 0, sizeof(msg));
  msg.cm_hdr.ch_id = 0x100;
  msg.cm_hdr.ch_dlc = 2;
  assert_int_equal(g_ops->co_send(g_dev, &msg), -EBUSY);
  assert_int_equal(g_ops->co_send(g_dev, &msg), -EBUSY);
  mock_sdk_can_fire_tx_isr();
  assert_int_equal(mock_can_upper_txdone_count(), 1);
  mock_can_send_frame(0x100, 2, data);
  mock_sdk_can_fire_tx_isr();
  assert_int_equal(mock_can_upper_txdone_count(), 2);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_send_ptb_fail(FAR void **state)
{
  struct can_msg_s msg;

  mock_can_setup_configured();

  mock_sdk_can_set_ret_once(MOCK_CAN_FN_SEND_PTB, BK_ERR_TIMEOUT);
  memset(&msg, 0, sizeof(msg));
  msg.cm_hdr.ch_id = 0x100;
  msg.cm_hdr.ch_dlc = 2;
  assert_int_equal(g_ops->co_send(g_dev, &msg), -ETIMEDOUT);

  /* The failed submission must have released tx_busy. */
  assert_int_equal(g_ops->co_send(g_dev, &msg), 0);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_SEND_PTB), 2);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_cancel(FAR void **state)
{
  static const uint8_t data[2] = { 9, 8 };
  struct can_msg_s msg;

  mock_can_setup_configured();

  mock_can_send_frame(0x100, 2, data);
  assert_int_equal(g_ops->co_cancel(g_dev, &msg), true);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_ABORT_PTB), 1);
  assert_int_equal(g_ops->co_cancel(g_dev, &msg), false);

  mock_can_send_frame(0x100, 2, data);
  assert_int_equal(g_ops->co_cancel(g_dev, NULL), true);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_ABORT_PTB), 2);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_rx_frame(FAR void **state)
{
  static const uint8_t payload[4] = { 5, 6, 7, 8 };

  mock_can_setup_configured();

  mock_sdk_can_rx_enqueue(0x345, payload, sizeof(payload));
  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(1);

  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_id, 0x345);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_dlc, 4);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_rtr, 0);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_error, 0);
  assert_memory_equal(mock_can_upper_rx(0)->data, payload, sizeof(payload));
  assert_true(mock_sdk_can_rx_empty());

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_rx_overrun_dlc(FAR void **state)
{
  static const uint8_t bad[9] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  static const uint8_t good[2] = { 0xaa, 0xbb };

  mock_can_setup_configured();

  mock_sdk_can_rx_enqueue(0x200, bad, sizeof(bad));
  mock_sdk_can_fire_rx_isr();
  mock_can_settle_rx();
  assert_int_equal(mock_can_upper_rx_count(), 0);

  /* The corrupt record must not wedge the drain state. */
  mock_sdk_can_rx_enqueue(0x201, good, sizeof(good));
  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(1);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_id, 0x201);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_dlc, 2);
  assert_memory_equal(mock_can_upper_rx(0)->data, good, sizeof(good));

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_rx_ext_id_dropped(FAR void **state)
{
  static const uint8_t payload[2] = { 1, 2 };

  mock_can_setup_configured();

  mock_sdk_can_rx_enqueue(0x800, payload, sizeof(payload));
  mock_sdk_can_fire_rx_isr();
  mock_can_settle_rx();
  assert_int_equal(mock_can_upper_rx_count(), 0);

  mock_sdk_can_rx_enqueue(0x100, payload, sizeof(payload));
  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(1);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_id, 0x100);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_rx_budget(FAR void **state)
{
  static const uint8_t payload[2] = { 1, 2 };
  int i;

  mock_can_setup_configured();

  for (i = 0; i < 10; i++)
    {
      mock_sdk_can_rx_enqueue(0x100 + i, payload, sizeof(payload));
    }

  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(10);
  assert_int_equal(mock_can_upper_rx_count(), 10);
  assert_int_equal(mock_can_upper_rx(9)->hdr.ch_id, 0x109);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_err_report(FAR void **state)
{
  mock_can_setup_configured();

  mock_sdk_can_fire_err_isr();
  mock_can_poll_until(1);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_id, CAN_ERROR_CONTROLLER);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_dlc, CAN_ERR_DLC);
  assert_int_equal(mock_can_upper_rx(0)->hdr.ch_error, 1);
  assert_int_equal(mock_can_upper_rx(0)->data[1], CAN_ERROR1_UNSPEC);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_ioctl(FAR void **state)
{
  struct canioc_connmodes_s modes;
  struct canioc_bittiming_s timing;
  struct canioc_connmodes_s silent;
  static const uint8_t data[2] = { 1, 2 };

  mock_can_setup_configured();

  memset(&modes, 0, sizeof(modes));
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_GET_CONNMODES,
                                   (unsigned long)&modes), 0);
  assert_int_equal(modes.bm_loopback, 0);
  assert_int_equal(modes.bm_silent, 0);

  modes.bm_loopback = 1;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_CONNMODES,
                                   (unsigned long)&modes), 0);
  assert_true(mock_sdk_can_lbmi());

  memset(&modes, 0, sizeof(modes));
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_GET_CONNMODES,
                                   (unsigned long)&modes), 0);
  assert_int_equal(modes.bm_loopback, 1);

  memset(&silent, 0, sizeof(silent));
  silent.bm_silent = 1;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_CONNMODES,
                                   (unsigned long)&silent), -ENOTSUP);
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_CONNMODES, 0),
                   -EINVAL);
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_GET_CONNMODES, 0),
                   -EINVAL);

  memset(&timing, 0, sizeof(timing));
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_GET_BITTIMING,
                                   (unsigned long)&timing), 0);
  assert_int_equal(timing.bt_baud, 1000000);

  timing.bt_baud = 500000;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_BITTIMING,
                                   (unsigned long)&timing), 0);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_BITRATE_CONFIG), 1);
  assert_int_equal(mock_sdk_can_last_s_speed(), CAN_BR_500K);
  assert_int_equal(mock_sdk_can_last_f_speed(), CAN_BR_4M);
  assert_int_equal(mock_sdk_can_call(mock_can_find_call(
                                       MOCK_CAN_FN_BITRATE_CONFIG))->a0,
                   CAN_BR_500K);

  memset(&timing, 0, sizeof(timing));
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_GET_BITTIMING,
                                   (unsigned long)&timing), 0);
  assert_int_equal(timing.bt_baud, 500000);

  timing.bt_baud = 123456;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_BITTIMING,
                                   (unsigned long)&timing), -ENOTSUP);
  timing.bt_baud = 500000;
  timing.bt_tseg1 = 1;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_BITTIMING,
                                   (unsigned long)&timing), -ENOTSUP);

  /* tx_busy blocks reconfiguration and mode changes. */
  mock_can_send_frame(0x100, 2, data);
  memset(&timing, 0, sizeof(timing));
  timing.bt_baud = 250000;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_BITTIMING,
                                   (unsigned long)&timing), -EBUSY);
  modes.bm_loopback = 0;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_CONNMODES,
                                   (unsigned long)&modes), -EBUSY);
  mock_sdk_can_fire_tx_isr();

  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_BUSOFF_RECOVERY, 0), 0);
  assert_int_equal(mock_sdk_can_calls(MOCK_CAN_FN_CTRL), 1);
  assert_int_equal(mock_sdk_can_call(mock_can_find_call(MOCK_CAN_FN_CTRL))->fn,
                   MOCK_CAN_FN_CTRL);
  assert_int_equal(mock_sdk_can_call(mock_can_find_call(MOCK_CAN_FN_CTRL))->a0,
                   CMD_CAN_BUSOFF_CLR);

  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_ADD_STDFILTER, 0),
                   -ENOTSUP);
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_DEL_STDFILTER, 0),
                   -ENOTSUP);
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_ADD_EXTFILTER, 0),
                   -ENOTSUP);
  assert_int_equal(g_ops->co_ioctl(g_dev, 0xdead, 0), -ENOTTY);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_unconfigured_gates(FAR void **state)
{
  struct canioc_connmodes_s modes;
  struct canioc_bittiming_s timing;
  struct can_msg_s msg;

  mock_can_setup_dev();

  memset(&msg, 0, sizeof(msg));
  msg.cm_hdr.ch_id = 0x100;
  msg.cm_hdr.ch_dlc = 2;
  assert_int_equal(g_ops->co_send(g_dev, &msg), -ENODEV);
  assert_int_equal(g_ops->co_setup(g_dev), 0);
  g_ops->co_shutdown(g_dev);

  memset(&modes, 0, sizeof(modes));
  modes.bm_loopback = 1;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_CONNMODES,
                                   (unsigned long)&modes), -ENODEV);
  memset(&timing, 0, sizeof(timing));
  timing.bt_baud = 500000;
  assert_int_equal(g_ops->co_ioctl(g_dev, CANIOC_SET_BITTIMING,
                                   (unsigned long)&timing), -ENODEV);

  assert_int_equal(g_ops->co_remoterequest(g_dev, 0x100), -ENOTSUP);

  g_ops->co_errhandle(g_dev);
  assert_int_equal(mock_can_upper_rx_count(), 0);

  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_reset_recycle(FAR void **state)
{
  static const uint8_t payload[2] = { 3, 4 };

  mock_can_setup_configured();

  mock_sdk_can_rx_enqueue(0x100, payload, sizeof(payload));
  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(1);

  g_ops->co_reset(g_dev);
  assert_int_equal(mock_can_upper_rx_count(), 1);

  assert_int_equal(g_ops->co_setup(g_dev), 0);
  assert_int_equal(mock_can_kthread_count(), 2);
  g_ops->co_rxint(g_dev, true);

  mock_sdk_can_rx_enqueue(0x101, payload, sizeof(payload));
  mock_sdk_can_fire_rx_isr();
  mock_can_poll_until(2);
  assert_int_equal(mock_can_upper_rx(1)->hdr.ch_id, 0x101);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

static void test_can_txdone_gating(FAR void **state)
{
  static const uint8_t data[2] = { 1, 2 };

  mock_can_setup_configured();

  mock_can_send_frame(0x100, 2, data);
  mock_sdk_can_fire_tx_isr();
  assert_int_equal(mock_can_upper_txdone_count(), 0);

  g_ops->co_txint(g_dev, true);
  mock_can_send_frame(0x100, 2, data);
  mock_sdk_can_fire_tx_isr();
  assert_int_equal(mock_can_upper_txdone_count(), 1);

  g_ops->co_shutdown(g_dev);
  assert_int_equal(bk7258_can_uninitialize(g_dev), 0);
  g_dev = NULL;
}

int main(int argc, FAR char *argv[])
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(test_can_init_ok, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_init_double, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_init_driver_init_fail,
                                    test_setup, test_teardown),
    cmocka_unit_test_setup_teardown(test_can_uninit_never_init, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_uninit_wrong_dev, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_uninit_crefs, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_setup_ok, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_setup_gate, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_stale_ops_after_uninit,
                                    test_setup, test_teardown),
    cmocka_unit_test_setup_teardown(test_can_send_gate, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_send_busy, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_send_ptb_fail, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_cancel, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_rx_frame, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_rx_overrun_dlc, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_rx_ext_id_dropped, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_rx_budget, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_err_report, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_ioctl, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_unconfigured_gates, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_reset_recycle, test_setup,
                                    test_teardown),
    cmocka_unit_test_setup_teardown(test_can_txdone_gating, test_setup,
                                    test_teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
