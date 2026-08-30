/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/framework/mock_sdk_scale_rotate.h
 *
 * Host stand-in for the immutable v3.1.1.9 scale + rotator driver bundle
 * used by bk7258_scale_rotate.c, plus the CPU1/CPU2 interrupt-route ABI
 * (sys_drv_core_intr_group2_*).
 *
 * Every SDK entry point is logged in order with its arguments, and every
 * bk_err_t return value is programmable per function, so the driver's
 * rollback/retry paths can be exercised deterministically.  ISR callbacks
 * the driver registers are captured and re-fired on demand, letting tests
 * drive the scale/rotate completion state machine single-threaded (usually
 * from the tickwait hook, which runs inside the driver's blocking wait).
 ****************************************************************************/

#ifndef __MOCK_SDK_SCALE_ROTATE_H
#define __MOCK_SDK_SCALE_ROTATE_H

#include <stdbool.h>
#include <stdint.h>

#include <driver/hw_scale.h>
#include <driver/rott_driver.h>

/* Function ids shared by the call log and the per-function result table. */
enum
{
  MOCK_SR_FN_SCALE_DRIVER_INIT = 0,
  MOCK_SR_FN_SCALE_DRIVER_DEINIT,
  MOCK_SR_FN_SCALE_ISR_REGISTER,
  MOCK_SR_FN_SCALE_ISR_UNREGISTER,
  MOCK_SR_FN_SCALE_INT_ENABLE,
  MOCK_SR_FN_SCALE_MEM_FREE,
  MOCK_SR_FN_SCALE_STOP,
  MOCK_SR_FN_SCALE_FRAME,
  MOCK_SR_FN_ROTT_DRIVER_INIT,
  MOCK_SR_FN_ROTT_DRIVER_DEINIT,
  MOCK_SR_FN_ROTT_SOFT_RESET,
  MOCK_SR_FN_ROTT_ISR_REGISTER,
  MOCK_SR_FN_ROTT_INT_ENABLE,
  MOCK_SR_FN_ROTT_DATA_REVERSE,
  MOCK_SR_FN_ROTT_ENABLE,
  MOCK_SR_FN_ROTT_CONFIG,
  MOCK_SR_FN_SYS_GROUP2_ENABLE,
  MOCK_SR_FN_SYS_GROUP2_DISABLE,
  MOCK_SR_FN_MAX,
};

#define MOCK_SR_LOG_MAX 128

struct mock_sr_call_s
{
  int fn;
  uint32_t a0;
  uint32_t a1;
  uint32_t a2;
};

/* Programmable results -------------------------------------------------- */

void mock_sr_sdk_reset(void);
void mock_sr_set_ret(int fn, int ret);
void mock_sr_set_ret_once(int fn, int ret);
void mock_sr_set_sys_enable(int32_t ret);

/* Post the driver's completion semaphore directly (spurious-wake probe).
 * The driver clears it via nxsem_reset() at prepare time, so the intended
 * use is from a tickwait hook. */
void mock_sr_post_completion(void);

/* Event-loop drive: run before the driver's nxsem_tickwait on an empty
 * semaphore; may fire ISRs to post events deterministically. */
void mock_sr_set_tickwait_hook(void (*hook)(void));

/* Call log -------------------------------------------------------------- */

unsigned int mock_sr_call_count(void);
int mock_sr_calls_of(int fn);
const struct mock_sr_call_s *mock_sr_call(unsigned int index);

/* Captured state -------------------------------------------------------- */

const scale_drv_config_t *mock_sr_scale_config(void);
const rott_config_t *mock_sr_rott_config(void);
rott_input_data_flow_t mock_sr_reverse_input(void);
rott_output_data_flow_t mock_sr_reverse_output(void);
uint32_t mock_sr_rott_int_mask(void);

/* ISR capture and injection --------------------------------------------- */

void mock_sr_fire_scale(scale_id_t id);
void mock_sr_fire_rott_complete(void);
void mock_sr_fire_rott_error(void);

#endif /* __MOCK_SDK_SCALE_ROTATE_H */
