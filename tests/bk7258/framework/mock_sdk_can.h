/****************************************************************************
 * tests/bk7258/framework/mock_sdk_can.h
 *
 * Host SDK mock for the bk7258 CAN lower-half (bk7258_can.c) + NuttX
 * upper-half records + kernel-thread shims.  Model:
 *
 *   - The 11 SDK ABI calls are recorded in a per-test call log with a
 *     programmable return value (persistent via mock_sdk_can_set_ret,
 *     one-shot via mock_sdk_can_set_ret_once -- the once variant is for
 *     failure paths whose persistent injection would wedge the driver's
 *     all-or-nothing uninitialize).
 *   - The SDK RX FIFO is a fixed ring: bk_can_send_ptb() enqueues the
 *     frame payload, bk_can_receive() dequeues it (returning
 *     BK_ERR_TIMEOUT when empty, matching the driver's polling drain).
 *   - ISR delivery is driven by the tests: fire_rx_isr()/fire_tx_isr()
 *     invoke the callbacks the driver registered at init, which post the
 *     driver's rx_sem.  The rx thread is a REAL pthread (kthread_create)
 *     so the lower-half's blocking-drain behavior is exercised for real;
 *     tests synchronize by polling the can_receive() record.
 *   - The NuttX upper half is not linked; can_receive()/can_txdone() are
 *     recorded (hdr + up to CAN_MAX_DLEN bytes) for assertion.
 ****************************************************************************/

#ifndef __MOCK_SDK_CAN_H
#define __MOCK_SDK_CAN_H

#include <stdint.h>
#include <sys/types.h>

#include <driver/hal/hal_can_types.h>
#include <nuttx/can/can.h>

/* SDK call log ---------------------------------------------------------- */

enum mock_sdk_can_fn_e
{
  MOCK_CAN_FN_DRIVER_INIT = 1,
  MOCK_CAN_FN_DRIVER_DEINIT,
  MOCK_CAN_FN_RECEIVE,
  MOCK_CAN_FN_SEND_PTB,
  MOCK_CAN_FN_REGISTER_ISR,
  MOCK_CAN_FN_REGISTER_ERR,
  MOCK_CAN_FN_BITRATE_CONFIG,
  MOCK_CAN_FN_ABORT_PTB,
  MOCK_CAN_FN_ABORT_ALL,
  MOCK_CAN_FN_SET_LBMI,
  MOCK_CAN_FN_GET_LBMI,
  MOCK_CAN_FN_CTRL,
};

#define MOCK_CAN_LOG_MAX 64

struct mock_can_call_s
{
  enum mock_sdk_can_fn_e fn;
  uint32_t a0;
  uint32_t a1;
};

struct mock_can_log_s
{
  struct mock_can_call_s call[MOCK_CAN_LOG_MAX];
  int count;
};

FAR struct mock_can_log_s *mock_sdk_can_log(void);
int mock_sdk_can_calls(enum mock_sdk_can_fn_e fn);
FAR struct mock_can_call_s *mock_sdk_can_call(int index);

/* Return-value injection ------------------------------------------------ */

void mock_sdk_can_set_ret(enum mock_sdk_can_fn_e fn, int ret);
void mock_sdk_can_set_ret_once(enum mock_sdk_can_fn_e fn, int ret);

/* ISR injection --------------------------------------------------------- */

void mock_sdk_can_fire_rx_isr(void);
void mock_sdk_can_fire_tx_isr(void);
void mock_sdk_can_fire_err_isr(void);

/* SDK RX FIFO (payload-level model of the SDK's internal FIFO) ---------- */

#define MOCK_CAN_FIFO_SLOTS 16
#define MOCK_CAN_FIFO_MAX_PAYLOAD 64

void mock_sdk_can_rx_enqueue(uint32_t id, FAR const uint8_t *payload,
                             uint32_t size);
void mock_sdk_can_rx_flush(void);
bool mock_sdk_can_rx_empty(void);

/* Frame capture (bk_can_send_ptb) --------------------------------------- */

FAR const uint8_t *mock_sdk_can_last_tx_payload(void);
uint32_t mock_sdk_can_last_tx_size(void);
uint32_t mock_sdk_can_last_tx_id(void);

/* Bit-rate config capture ------------------------------------------------ */

uint32_t mock_sdk_can_last_s_speed(void);
uint32_t mock_sdk_can_last_f_speed(void);

/* Loopback mirror (can_hal_set_lbmi/get_lbmi) --------------------------- */

bool mock_sdk_can_lbmi(void);

/* Upper-half records ---------------------------------------------------- */

#define MOCK_CAN_UPPER_RING 16

struct mock_can_upper_rx_s
{
  struct can_hdr_s hdr;
  uint8_t data[CAN_MAX_DLEN];
};

FAR struct mock_can_upper_rx_s *mock_can_upper_rx(int index);
unsigned int mock_can_upper_rx_count(void);
unsigned int mock_can_upper_txdone_count(void);

/* Kernel-thread surface (real pthread) ---------------------------------- */

int mock_can_kthread_count(void);

/* Test helpers ---------------------------------------------------------- */

void mock_sdk_can_reset(void);

#endif /* __MOCK_SDK_CAN_H */
