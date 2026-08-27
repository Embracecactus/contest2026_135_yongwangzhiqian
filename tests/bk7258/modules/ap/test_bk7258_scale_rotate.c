/****************************************************************************
 * tests/bk7258/modules/ap/test_bk7258_scale_rotate.c
 *
 * Host unit suite for chips/bk7258/ap/bk7258_scale_rotate.c (true
 * source, patched copy built via framework/patch.py, see Makefile).
 *
 * The driver wraps the SDK scale (bk_hw_scale_*) and rotator (bk_rott_*)
 * bundles plus the CPU1 interrupt-route ABI.  The suite mock SDK mirrors
 * the caching/logging style of mock_sdk_yuv_h264: programmable results,
 * captured ISR callbacks re-fired from a tickwait hook (the driver blocks
 * in a single nxsem_tickwait per operation), config snapshots, and the
 * Scale1 write-burst register poke through mock_reg32.
 *
 * Determinism rules (same as the yuv suite):
 *   - all media buffers must be static (32-bit SDK address contract)
 *   - SDK results are snapshotted at driver-return time
 *   - tickwait with no posted event times out (-ETIMEDOUT)
 *   - a tickwait hook is the only way to fire ISRs inside the wait
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include <common/bk_err.h>
#include <nuttx/semaphore.h>

#include "mock_sdk_scale_rotate.h"
#include "../../framework/mock_reg32.h"
#include "bk7258_scale_rotate.h"

/* Media geometry: 64x64 RGB565 (2 bytes/pixel). */
#define FRAME_BYTES   (64u * 64u * 2u)
#define ROTATE_BYTES  (64u * 48u * 2u)

#define SCALE1_THRESHOLD_REG 0x480e0040u
#define SCALE1_THRESHOLD_MASK 0x00000fffu

static uint8_t g_src[FRAME_BYTES];
static uint8_t g_dst[FRAME_BYTES];
static uint8_t g_rsrc[ROTATE_BYTES];
static uint8_t g_rdst[ROTATE_BYTES];

struct bk7258_scale_rotate_s *g_priv;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                */
/* ---------------------------------------------------------------------- */

static void sr_setup(void)
{
  mock_sr_sdk_reset();
  mock_reg32_reset();
  g_priv = NULL;
}

static int sr_group_setup(void **state)
{
  (void)state;
  return 0;
}

static int sr_group_teardown(void **state)
{
  (void)state;
  if (g_priv != NULL)
    {
      (void)bk7258_scale_rotate_uninitialize(g_priv);
      g_priv = NULL;
    }

  return 0;
}

static int sr_setup_reset(void **state)
{
  (void)state;
  sr_setup();
  return 0;
}

static int sr_setup_ok(void **state)
{
  sr_setup();
  assert_int_equal(bk7258_scale_rotate_initialize(&g_priv,
                                                  BK7258_SCALE_ROTATE_SCALE0),
                   0);
  return 0;
}

static int sr_setup_rotator(void **state)
{
  sr_setup();
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   0);
  return 0;
}

static int sr_teardown_ok(void **state)
{
  (void)state;
  if (g_priv != NULL)
    {
      (void)bk7258_scale_rotate_uninitialize(g_priv);
      g_priv = NULL;
    }

  return 0;
}

static void sr_fire_scale0_hook(void)
{
  mock_sr_fire_scale(HW_SCALE0);
}

static void sr_fire_scale1_hook(void)
{
  mock_sr_fire_scale(HW_SCALE1);
}

static void sr_fire_rott_complete_hook(void)
{
  mock_sr_fire_rott_complete();
}

static void sr_fire_rott_error_hook(void)
{
  mock_sr_fire_rott_error();
}

static void sr_post_spurious_hook(void)
{
  mock_sr_post_completion();
}

static struct bk7258_scale_request_s sr_scale_request(uint8_t *src,
                                                      uint8_t *dst,
                                                      size_t src_size,
                                                      size_t dst_size)
{
  struct bk7258_scale_request_s request;

  memset(&request, 0, sizeof(request));
  request.src = src;
  request.dst = dst;
  request.src_size = src_size;
  request.dst_size = dst_size;
  request.src_width = 64;
  request.src_height = 64;
  request.dst_width = 64;
  request.dst_height = 64;
  request.format = BK7258_SCALE_ROTATE_RGB565;
  request.timeout_ms = 100;
  return request;
}

static struct bk7258_rotate_request_s sr_rotate_request(uint8_t *src,
                                                        uint8_t *dst,
                                                        size_t src_size,
                                                        size_t dst_size)
{
  struct bk7258_rotate_request_s request;

  memset(&request, 0, sizeof(request));
  request.src = src;
  request.dst = dst;
  request.src_size = src_size;
  request.dst_size = dst_size;
  request.src_width = 64;
  request.src_height = 48;
  request.block_width = 16;
  request.block_height = 16;
  request.block_count = 12;
  request.watermark_block = 0;
  request.format = BK7258_SCALE_ROTATE_RGB565;
  request.angle = BK7258_SCALE_ROTATE_90;
  request.input_flow = BK7258_SCALE_ROTATE_INPUT_NORMAL;
  request.output_flow = BK7258_SCALE_ROTATE_OUTPUT_NORMAL;
  request.timeout_ms = 100;
  return request;
}

/* ---------------------------------------------------------------------- */
/* Initialize / uninitialize                                              */
/* ---------------------------------------------------------------------- */

static void sr_init_ok_scale0(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  assert_non_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_INIT), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_ISR_REGISTER), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_INT_ENABLE), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SYS_GROUP2_DISABLE), 0);
  assert_int_equal(mock_sr_call(0)->a0, HW_SCALE0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

static void sr_init_ok_scale1(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE1),
                   0);
  assert_int_equal(mock_sr_call(0)->a0, HW_SCALE1);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

static void sr_init_ok_rotator(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   0);
  assert_non_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_INIT), 0);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DRIVER_INIT), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ISR_REGISTER), 2);
  assert_int_equal(mock_sr_call(0)->fn, MOCK_SR_FN_ROTT_DRIVER_INIT);
  assert_int_equal(mock_sr_call(1)->a0, ROTATE_COMPLETE_INT);
  assert_int_equal(mock_sr_call(2)->a0, ROTATE_CFG_ERR_INT);
  assert_int_equal(mock_sr_call(3)->a0,
                   ROTATE_COMPLETE_INT | ROTATE_CFG_ERR_INT);
  assert_int_equal(mock_sr_call(3)->a1, 1);      /* enable */
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SYS_GROUP2_DISABLE), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SYS_GROUP2_ENABLE), 1);
  assert_int_equal(mock_sr_call(4)->a0, 2);      /* disable core2 */
  assert_int_equal(mock_sr_call(5)->a0, 1);      /* enable core1 */
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

static void sr_init_null_out(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     NULL, BK7258_SCALE_ROTATE_SCALE0),
                   -EINVAL);
  assert_int_equal(mock_sr_call_count(), 0);
}

static void sr_init_invalid_engine(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, (enum bk7258_scale_rotate_engine_e)42),
                   -EINVAL);
  assert_null(g_priv);
  assert_int_equal(mock_sr_call_count(), 0);
}

static void sr_init_double(void **state)
{
  FAR struct bk7258_scale_rotate_s *once;
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  once = g_priv;
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   -EBUSY);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_INIT), 1);
  assert_int_equal(bk7258_scale_rotate_uninitialize(once), 0);
}

static void sr_init_scale_driver_init_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_ret(MOCK_SR_FN_SCALE_DRIVER_INIT, BK_ERR_NO_DEV);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   -ENODEV);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_MEM_FREE), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_DEINIT), 0);
}

static void sr_init_scale_isr_register_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_ret(MOCK_SR_FN_SCALE_ISR_REGISTER, BK_ERR_NO_MEM);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   -ENOMEM);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_DEINIT), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_MEM_FREE), 1);
}

static void sr_init_scale_int_enable_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_ret(MOCK_SR_FN_SCALE_INT_ENABLE, BK_ERR_TIMEOUT);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   -ETIMEDOUT);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_ISR_UNREGISTER), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_DEINIT), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_MEM_FREE), 1);
}

static void sr_init_rott_driver_init_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_ret(MOCK_SR_FN_ROTT_DRIVER_INIT, BK_ERR_NOT_INIT);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   -ENODEV);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ISR_REGISTER), 0);
}

static void sr_init_rott_complete_isr_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_ret(MOCK_SR_FN_ROTT_ISR_REGISTER, BK_ERR_NO_MEM);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   -ENOMEM);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DRIVER_DEINIT), 1);
}

static void sr_init_rott_int_enable_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_ret(MOCK_SR_FN_ROTT_INT_ENABLE, BK_ERR_PARAM);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   -EINVAL);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ISR_REGISTER), 4);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DRIVER_DEINIT), 1);
}

static void sr_init_rott_route_fail(void **state)
{
  (void)state;
  sr_setup();

  mock_sr_set_sys_enable(123);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   -EIO);
  assert_null(g_priv);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_INT_ENABLE), 2);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ISR_REGISTER), 4);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DRIVER_DEINIT), 1);
  /* route fail: disable(2) + enable-fail disable(1) + from_cpu1 disable(1) */

  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SYS_GROUP2_DISABLE), 3);
}

static void sr_uninit_twice(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), -ENODEV);
}

static void sr_uninit_bad_priv(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_uninitialize(
                     (struct bk7258_scale_rotate_s *)&g_src),
                   -EINVAL);
}

static void sr_uninit_scale0_ok(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_INT_ENABLE), 2);
  assert_int_equal(mock_sr_call(2)->a1, 1);      /* init enable(true) */
  assert_int_equal(mock_sr_call(3)->a1, 0);      /* uninit enable(false) */
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_ISR_UNREGISTER), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_DEINIT), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_MEM_FREE), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_STOP), 0);
}

static void sr_uninit_rotator_ok(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_INT_ENABLE), 2);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ISR_REGISTER), 4);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_SOFT_RESET), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DRIVER_DEINIT), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SYS_GROUP2_DISABLE), 2);
}

static void sr_uninit_scale_int_disable_fail(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  /* One-shot: teardown's retry must be able to complete the teardown. */

  mock_sr_set_ret_once(MOCK_SR_FN_SCALE_INT_ENABLE, BK_ERR_BUSY);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), -EBUSY);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_DRIVER_DEINIT), 0);
}

static void sr_reinit_engine_switch(void **state)
{
  (void)state;
  sr_setup();

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE1),
                   0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

/* ---------------------------------------------------------------------- */
/* Scale validation + errors                                              */
/* ---------------------------------------------------------------------- */

static void sr_scale_null_request(void **state)
{
  (void)state;


  assert_int_equal(bk7258_scale(g_priv, NULL), -EINVAL);
}

static void sr_scale_on_rotator(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  assert_int_equal(bk7258_scale(g_priv, &request), -ENOTSUP);
}

static void sr_scale_unsupported_format(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  request.format = BK7258_SCALE_ROTATE_UVYY;
  assert_int_equal(bk7258_scale(g_priv, &request), -ENOTSUP);
}

static void sr_scale_dst_width_not_multiple_16(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  request.dst_width = 17;
  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
}

static void sr_scale_zero_dims(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  request.src_width = 0;
  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
}

static void sr_scale_dim_too_large(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  request.dst_height = 1281;
  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
}

static void sr_scale_src_size_too_small(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES - 1,
                                                            FRAME_BYTES);

  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
}

static void sr_scale_dst_size_too_small(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES - 1);

  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
}

static void sr_scale_overlap(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_src,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
}

static void sr_scale_stack_buffers(void **state)
{
  (void)state;


  uint8_t local[FRAME_BYTES];

  struct bk7258_scale_request_s request = sr_scale_request(local, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  assert_int_equal(bk7258_scale(g_priv, &request), -EOVERFLOW);
}

static void sr_scale_hw_frame_fail(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  mock_sr_set_ret(MOCK_SR_FN_SCALE_FRAME, BK_ERR_PARAM);
  assert_int_equal(bk7258_scale(g_priv, &request), -EINVAL);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_FRAME), 1);
  assert_int_equal(bk7258_scale(g_priv, &request), -EIO);
}

static void sr_scale_timeout(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  assert_int_equal(bk7258_scale(g_priv, &request), -ETIMEDOUT);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_STOP), 1);
  assert_int_equal(bk7258_scale(g_priv, &request), -EIO);
}

static void sr_scale_spurious_wake(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);

  mock_sr_set_tickwait_hook(sr_post_spurious_hook);
  assert_int_equal(bk7258_scale(g_priv, &request), -EIO);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_STOP), 1);
}

/* ---------------------------------------------------------------------- */
/* Scale happy paths                                                      */
/* ---------------------------------------------------------------------- */

static void sr_scale_scale0_happy(void **state)
{
  (void)state;


  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);
  const scale_drv_config_t *config;

  mock_sr_set_tickwait_hook(sr_fire_scale0_hook);
  assert_int_equal(bk7258_scale(g_priv, &request), 0);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_FRAME), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_SCALE_STOP), 0);
  config = mock_sr_scale_config();
  assert_int_equal(config->src_width, 64);
  assert_int_equal(config->src_height, 64);
  assert_int_equal(config->dst_width, 64);
  assert_int_equal(config->dst_height, 64);
  assert_int_equal(config->scale_mode, FRAME_SCALE);
  assert_int_equal(config->pixel_fmt, PIXEL_FMT_RGB565);
  assert_int_equal(config->line_cycle, 16);
  assert_int_equal(config->line_mask, 0x1f);
  assert_ptr_equal(config->src_addr, g_src);
  assert_ptr_equal(config->dst_addr, g_dst);
}

static void sr_scale_scale1_happy(void **state)
{
  (void)state;
  sr_setup();

  struct bk7258_scale_request_s request;
  uint32_t value;

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE1),
                   0);
  mock_reg32_set(SCALE1_THRESHOLD_REG, 0xf0000000u);

  mock_sr_set_tickwait_hook(sr_fire_scale1_hook);
  request = sr_scale_request(g_src, g_dst, FRAME_BYTES, FRAME_BYTES);
  request.dst_width = 128;
  request.dst_height = 32;
  assert_int_equal(bk7258_scale(g_priv, &request), 0);
  value = mock_reg32_read(SCALE1_THRESHOLD_REG);
  assert_int_equal(value & SCALE1_THRESHOLD_MASK, 64);
  assert_int_equal(value & ~SCALE1_THRESHOLD_MASK, 0xf0000000u);

  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

static void sr_scale_scale1_threshold_variants(void **state)
{
  (void)state;
  sr_setup();

  struct bk7258_scale_request_s request;

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE1),
                   0);
  mock_sr_set_tickwait_hook(sr_fire_scale1_hook);

  request = sr_scale_request(g_src, g_dst, FRAME_BYTES, FRAME_BYTES);
  request.dst_width = 96;
  request.dst_height = 32;
  assert_int_equal(bk7258_scale(g_priv, &request), 0);
  assert_int_equal(mock_reg32_read(SCALE1_THRESHOLD_REG) &
                     SCALE1_THRESHOLD_MASK,
                   16);

  request.dst_width = 80;
  request.dst_height = 32;
  assert_int_equal(bk7258_scale(g_priv, &request), 0);
  assert_int_equal(mock_reg32_read(SCALE1_THRESHOLD_REG) &
                     SCALE1_THRESHOLD_MASK,
                   8);

  request.dst_width = 64;
  request.dst_height = 32;
  assert_int_equal(bk7258_scale(g_priv, &request), 0);
  assert_int_equal(mock_reg32_read(SCALE1_THRESHOLD_REG) &
                     SCALE1_THRESHOLD_MASK,
                   32);

  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

/* ---------------------------------------------------------------------- */
/* Rotate validation                                                      */
/* ---------------------------------------------------------------------- */

static void sr_rotate_on_scale(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  assert_int_equal(bk7258_rotate(g_priv, &request), -ENOTSUP);
}

static void sr_rotate_invalid_input_flow(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.input_flow = (enum bk7258_scale_rotate_input_flow_e)7;
  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_invalid_output_flow(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.output_flow = (enum bk7258_scale_rotate_output_flow_e)7;
  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_angle_180(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.angle = BK7258_SCALE_ROTATE_180;
  assert_int_equal(bk7258_rotate(g_priv, &request), -ENOTSUP);
}

static void sr_rotate_block_count_mismatch(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.block_width = 32;
  request.block_height = 32;
  request.block_count = 3;
  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_watermark_ge_count(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.watermark_block = 12;
  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_block_pixels_exceed(void **state)
{
  (void)state;


  static uint8_t big_src[420u * 420u * 2u];
  static uint8_t big_dst[420u * 420u * 2u];
  struct bk7258_rotate_request_s request = sr_rotate_request(
    big_src, big_dst, sizeof(big_src), sizeof(big_dst));

  request.src_width = 420;
  request.src_height = 420;
  request.block_width = 70;
  request.block_height = 70;
  request.block_count = 36;
  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_src_size_too_small(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES - 1,
                                                              ROTATE_BYTES);

  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_overlap(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rsrc,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
}

static void sr_rotate_config_fail(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  mock_sr_set_ret(MOCK_SR_FN_ROTT_CONFIG, BK_ERR_IN_PROGRESS);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EBUSY);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ENABLE), 0);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EIO);
}

static void sr_rotate_data_reverse_fail(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  mock_sr_set_ret(MOCK_SR_FN_ROTT_DATA_REVERSE, BK_ERR_PARAM);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EINVAL);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ENABLE), 0);
}

/* ---------------------------------------------------------------------- */
/* Rotate happy paths + errors                                            */
/* ---------------------------------------------------------------------- */

static void sr_rotate_happy_90(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);
  const rott_config_t *config;

  request.watermark_block = 2;
  mock_sr_set_tickwait_hook(sr_fire_rott_complete_hook);
  assert_int_equal(bk7258_rotate(g_priv, &request), 0);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_CONFIG), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DATA_REVERSE), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_ENABLE), 1);
  config = mock_sr_rott_config();
  assert_int_equal(config->rot_mode, ROTATE_90);
  assert_ptr_equal(config->input_addr, g_rsrc);
  assert_ptr_equal(config->output_addr, g_rdst);
  assert_int_equal(config->input_fmt, PIXEL_FMT_RGB565);
  assert_int_equal(config->picture_xpixel, 64);
  assert_int_equal(config->picture_ypixel, 48);
  assert_int_equal(config->block_xpixel, 16);
  assert_int_equal(config->block_ypixel, 16);
  assert_int_equal(config->block_cnt, 12);
  assert_int_equal(config->watermark_blk, 2);
  assert_int_equal(mock_sr_reverse_input(), ROTT_INPUT_NORMAL);
  assert_int_equal(mock_sr_reverse_output(), ROTT_OUTPUT_NORMAL);
}

static void sr_rotate_happy_none(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.angle = BK7258_SCALE_ROTATE_NONE;
  mock_sr_set_tickwait_hook(sr_fire_rott_complete_hook);
  assert_int_equal(bk7258_rotate(g_priv, &request), 0);
  assert_int_equal(mock_sr_rott_config()->rot_mode, ROTATE_NONE);
  assert_int_equal(mock_sr_rott_config()->picture_xpixel, 64);
  assert_int_equal(mock_sr_rott_config()->picture_ypixel, 48);
}

static void sr_rotate_happy_reversed_flows(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  request.input_flow = BK7258_SCALE_ROTATE_INPUT_REVERSE_HALFWORD;
  request.output_flow = BK7258_SCALE_ROTATE_OUTPUT_REVERSE_HALFWORD;
  mock_sr_set_tickwait_hook(sr_fire_rott_complete_hook);
  assert_int_equal(bk7258_rotate(g_priv, &request), 0);
  assert_int_equal(mock_sr_reverse_input(),
                   ROTT_INPUT_REVESE_HALFWORD_BY_HALFWORD);
  assert_int_equal(mock_sr_reverse_output(),
                   ROTT_OUTPUT_REVESE_HALFWORD_BY_HALFWORD);
}

static void sr_rotate_error_isr(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  mock_sr_set_tickwait_hook(sr_fire_rott_error_hook);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EIO);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_SOFT_RESET), 0);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EIO);
}

static void sr_rotate_timeout(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  assert_int_equal(bk7258_rotate(g_priv, &request), -ETIMEDOUT);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_SOFT_RESET), 1);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_DRIVER_DEINIT), 0);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EIO);
}

static void sr_rotate_spurious_wake(void **state)
{
  (void)state;


  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  mock_sr_set_tickwait_hook(sr_post_spurious_hook);
  assert_int_equal(bk7258_rotate(g_priv, &request), -EIO);
  assert_int_equal(mock_sr_calls_of(MOCK_SR_FN_ROTT_SOFT_RESET), 1);
}

static void sr_fault_then_reinit(void **state)
{
  (void)state;
  sr_setup();

  struct bk7258_rotate_request_s request = sr_rotate_request(g_rsrc, g_rdst,
                                                              ROTATE_BYTES,
                                                              ROTATE_BYTES);

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   0);
  assert_int_equal(bk7258_rotate(g_priv, &request), -ETIMEDOUT);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_ROTATOR),
                   0);
  mock_sr_set_tickwait_hook(sr_fire_rott_complete_hook);
  assert_int_equal(bk7258_rotate(g_priv, &request), 0);
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
}

static void sr_scale_after_uninit_fails(void **state)
{
  (void)state;
  sr_setup();

  struct bk7258_scale_request_s request = sr_scale_request(g_src, g_dst,
                                                            FRAME_BYTES,
                                                            FRAME_BYTES);
  struct bk7258_scale_rotate_s *dead;

  assert_int_equal(bk7258_scale_rotate_initialize(
                     &g_priv, BK7258_SCALE_ROTATE_SCALE0),
                   0);
  dead = g_priv;
  assert_int_equal(bk7258_scale_rotate_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_scale(dead, &request), -ENODEV);
  assert_int_equal(bk7258_scale_rotate_uninitialize(dead), -ENODEV);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    cmocka_unit_test_setup_teardown(sr_init_ok_scale0, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_ok_scale1, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_ok_rotator, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_null_out, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_invalid_engine, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_double, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_scale_driver_init_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_scale_isr_register_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_scale_int_enable_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_rott_driver_init_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_rott_complete_isr_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_rott_int_enable_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_init_rott_route_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_uninit_twice, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_uninit_bad_priv, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_uninit_scale0_ok, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_uninit_rotator_ok, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_uninit_scale_int_disable_fail, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_reinit_engine_switch, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_null_request, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_on_rotator, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_unsupported_format, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_dst_width_not_multiple_16, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_zero_dims, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_dim_too_large, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_src_size_too_small, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_dst_size_too_small, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_overlap, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_stack_buffers, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_hw_frame_fail, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_timeout, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_spurious_wake, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_scale0_happy, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_scale1_happy, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_scale1_threshold_variants, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_on_scale, sr_setup_ok,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_invalid_input_flow, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_invalid_output_flow, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_angle_180, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_block_count_mismatch, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_watermark_ge_count, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_block_pixels_exceed, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_src_size_too_small, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_overlap, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_config_fail, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_data_reverse_fail, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_happy_90, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_happy_none, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_happy_reversed_flows, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_error_isr, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_timeout, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_rotate_spurious_wake, sr_setup_rotator,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_fault_then_reinit, sr_setup_reset,
                                sr_teardown_ok),
    cmocka_unit_test_setup_teardown(sr_scale_after_uninit_fails, sr_setup_reset,
                                sr_teardown_ok),
  };

  return cmocka_run_group_tests_name("bk7258_scale_rotate", tests,
                             sr_group_setup, sr_group_teardown);
}
