/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/modules/ap/test_bk7258_yuv_h264.c
 *
 * Host unit tests for chips/bk7258/ap/bk7258_yuv_h264.c, compiled
 * unmodified against the mock SDK surface in mocks/driver/.
 *
 * The mock SDK (framework/mock_sdk_yuv_h264.{c,h}) logs every call in
 * order, records the captured dma/yuv configuration snapshots, exposes
 * programmable bk_err_t results per function, and keeps the ISR callbacks
 * the driver registered so tests can fire LINE/FINAL/error events through
 * the nxsem_tickwait hook.  All event flows are therefore driven
 * single-threaded and deterministically.
 ****************************************************************************/

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <cmocka.h>

#include "bk7258_yuv_h264.h"

#include "mock_sdk_yuv_h264.h"

/* Test geometry: 64x64 YUYV -> frame 8192 B, 16-line block 2048 B,
 * 4 blocks, cache 2 x block = 4096 B.  Output capacity is 2 DMA chunks
 * (20 KiB), which is the minimum multiple-of-chunk size above one chunk. */

#define TEST_WIDTH     64u
#define TEST_HEIGHT    64u
#define FRAME_BYTES    8192u
#define BLOCK_BYTES    2048u
#define CACHE_BYTES    4096u
#define CAPACITY       20480u
#define CHUNK          10240u
#define H264_IRQ_BIT   (1u << 14)
#define ROOT_H264      8u
#define DMA_USER       40u

static struct bk7258_yuv_h264_s *g_priv;
static uint8_t g_cache[CACHE_BYTES] __attribute__((aligned(64)));
static uint8_t g_input[FRAME_BYTES] __attribute__((aligned(64)));
static uint8_t g_out[CAPACITY] __attribute__((aligned(64)));
static uint8_t g_out2[CAPACITY] __attribute__((aligned(64)));

/* Tickwait-hook behavior selection, assigned by each test. */
static void (*g_hook)(void);
static unsigned int g_hook_guard;

static void hook_noop(void)
{
}

static void hook_fire_final(void)
{
  mock_yuv_fire_h264_final();
}

static void hook_fire_happy(void)
{
  if (g_hook_guard++ < 3u)
    {
      mock_yuv_fire_h264_line();
    }
  else
    {
      mock_yuv_fire_h264_final();
    }
}

static void hook_fire_finish_and_final(void)
{
  mock_yuv_fire_dma_finish();
  mock_yuv_fire_h264_final();
}

static void hook_fire_finishes_and_final(void)
{
  mock_yuv_fire_dma_finish();
  mock_yuv_fire_dma_finish();
  mock_yuv_fire_dma_finish();
  mock_yuv_fire_h264_final();
}

static void hook_fire_full_then_silence(void)
{
  if (g_hook_guard++ < 2u)
    {
      mock_yuv_fire_dma_finish();
    }
}

static void hook_error_event(void)
{
  mock_yuv_fire_yuv_error();
}

static void hook_mid_rencode_fail(void)
{
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_RENCODE_START, BK_ERR_BUSY);
  mock_yuv_fire_h264_line();
}

static struct bk7258_yuv_h264_config_s default_config(void)
{
  struct bk7258_yuv_h264_config_s cfg;

  cfg.width = TEST_WIDTH;
  cfg.height = TEST_HEIGHT;
  cfg.format = BK7258_YUV_H264_YUYV;
  cfg.line_cache = g_cache;
  cfg.line_cache_size = CACHE_BYTES;
  cfg.timeout_ms = 100;
  return cfg;
}

static int group_setup(void **state)
{
  (void)state;
  mock_yuv_sdk_reset();
  g_priv = NULL;
  g_hook = hook_noop;
  g_hook_guard = 0;
  memset(g_input, 0x5a, sizeof(g_input));
  memset(g_cache, 0, sizeof(g_cache));
  memset(g_out, 0, sizeof(g_out));
  return 0;
}

static int setup_ok(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  group_setup(state);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
  assert_non_null(g_priv);
  return 0;
}

static int teardown_reset(void **state)
{
  (void)state;
  mock_yuv_sdk_reset();
  g_priv = NULL;
  g_hook = hook_noop;
  g_hook_guard = 0;
  return 0;
}

static int teardown_ok(void **state)
{
  (void)state;
  if (g_priv != NULL)
    {
      bk7258_yuv_h264_uninitialize(g_priv);
      g_priv = NULL;
    }

  mock_yuv_sdk_reset();
  g_hook = hook_noop;
  g_hook_guard = 0;
  return 0;
}

/* Prepare mock values for a successful frame and run encode().  The
 * tickwait hook fires the full line/final sequence. */
static int run_happy_encode(struct bk7258_yuv_h264_output_s *output)
{
  struct bk7258_yuv_h264_input_s input;

  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_happy);
  g_hook_guard = 0;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output->data = g_out;
  output->capacity = CAPACITY;
  output->length = 0;
  return bk7258_yuv_h264_encode(g_priv, &input, output);
}

/* ---------------------------------------------------------------------- */
/* initialize                                                             */
/* ---------------------------------------------------------------------- */

static void init_geometry_reject(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();
  struct bk7258_yuv_h264_s *out = NULL;
  uint16_t bad_width[4] = { 0, 1921, 72, 65 };
  uint16_t bad_height[4] = { 16, 1090, 40, 33 };
  int i;

  (void)state;
  for (i = 0; i < 4; i++)
    {
      cfg.width = bad_width[i];
      cfg.height = TEST_HEIGHT;
      assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
      assert_null(out);
      cfg.width = TEST_WIDTH;
      cfg.height = bad_height[i];
      assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
      assert_null(out);
    }

  cfg.width = TEST_WIDTH;
  cfg.height = TEST_HEIGHT;
  cfg.format = 4;
  assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
  assert_int_equal(mock_yuv_call_count(), 0);
}

static void init_param_checks(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();
  struct bk7258_yuv_h264_s *h = NULL;

  (void)state;
  assert_int_equal(bk7258_yuv_h264_initialize(NULL, &cfg), -EINVAL);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, NULL), -EINVAL);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
  assert_non_null(g_priv);
  h = g_priv;
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EBUSY);
  assert_null(g_priv);
  assert_int_equal(bk7258_yuv_h264_uninitialize(h), 0);
}

static void init_cache_reject(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();
  struct bk7258_yuv_h264_s *out = NULL;

  (void)state;
  cfg.line_cache = NULL;
  assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
  cfg.line_cache = g_cache;
  cfg.line_cache_size = 0;
  assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
  cfg.line_cache_size = CACHE_BYTES - 1;
  assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
  cfg.line_cache_size = CACHE_BYTES;
  cfg.line_cache = g_cache + 1;
  assert_int_equal(bk7258_yuv_h264_initialize(&out, &cfg), -EINVAL);
}

static void init_stream_call_order(void **state)
{
  const struct mock_yuv_call_s *c;
  const yuv_buf_config_t *yc;

  (void)state;
  assert_int_equal(mock_yuv_call_count(), 15);
  c = mock_yuv_call(0);
  assert_int_equal(c->fn, MOCK_YUV_FN_MEDIA_ROOT_INIT);
  assert_int_equal(c->a0, ROOT_H264);
  c = mock_yuv_call(1);
  assert_int_equal(c->fn, MOCK_YUV_FN_DMA_ALLOC);
  assert_int_equal(c->a0, DMA_USER);
  c = mock_yuv_call(2);
  assert_int_equal(c->fn, MOCK_YUV_FN_DMA_REGISTER_ISR);
  assert_int_equal(c->a0, DMA_ID_3);
  c = mock_yuv_call(3);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_INIT);
  c = mock_yuv_call(4);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_NOSENSOR);
  c = mock_yuv_call(5);
  assert_int_equal(c->fn, MOCK_YUV_FN_H264_INIT);
  assert_int_equal(c->a0, TEST_WIDTH);
  assert_int_equal(c->a1, TEST_HEIGHT);
  c = mock_yuv_call(6);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_SET_EM_BASE);
  assert_int_equal(c->a0, (uint32_t)(uintptr_t)g_cache);
  c = mock_yuv_call(7);
  assert_int_equal(c->fn, MOCK_YUV_FN_H264_GET_FIFO);
  c = mock_yuv_call(8);
  assert_int_equal(c->fn, MOCK_YUV_FN_SYS_GROUP2_DISABLE);
  assert_int_equal(c->a0, 2u);
  assert_int_equal(c->a1, H264_IRQ_BIT);
  c = mock_yuv_call(9);
  assert_int_equal(c->fn, MOCK_YUV_FN_SYS_GROUP2_ENABLE);
  assert_int_equal(c->a0, 1u);
  assert_int_equal(c->a1, H264_IRQ_BIT);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_REGISTER_ISR), 2);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_REGISTER_ISR), 1);
  c = mock_yuv_call(13);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_START);
  assert_int_equal(c->a0, H264_MODE);
  c = mock_yuv_call(14);
  assert_int_equal(c->fn, MOCK_YUV_FN_H264_ENCODE_ENABLE);

  yc = mock_yuv_yuv_config();
  assert_true(mock_yuv_yuv_config_valid());
  assert_int_equal(yc->work_mode, H264_MODE);
  assert_int_equal(yc->mclk_div, YUV_MCLK_DIV_4);
  assert_int_equal(yc->x_pixel, 8u);
  assert_int_equal(yc->y_pixel, 8u);
  assert_int_equal(yc->yuv_mode_cfg.yuv_format, YUV_FORMAT_YUYV);
  assert_int_equal(yc->yuv_mode_cfg.vsync, SYNC_HIGH_LEVEL);
  assert_int_equal(yc->yuv_mode_cfg.hsync, SYNC_HIGH_LEVEL);
  assert_null(yc->base_addr);
  assert_null(yc->emr_base_addr);
}

static void init_root_fail_rolls_back(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_root_ret(-EBUSY);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EBUSY);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_call_count(), 1);
  mock_yuv_set_root_ret(0);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
}

static void init_dma_alloc_fail(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();
  const struct mock_yuv_call_s *c;

  (void)state;
  mock_yuv_set_alloc(DMA_ID_MAX);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EBUSY);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_REGISTER_ISR), 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FREE), 0);
  mock_yuv_set_alloc(DMA_ID_3);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);

  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
  c = mock_yuv_call(mock_yuv_call_count() - 1);
  assert_int_equal(c->fn, MOCK_YUV_FN_DMA_FREE);
  assert_int_equal(c->a0, DMA_USER);
  assert_int_equal(c->a1, DMA_ID_3);
}

static void init_register_isr_fail(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_REGISTER_ISR, BK_ERR_DMA_ID);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EINVAL);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FREE), 1);
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_REGISTER_ISR, BK_OK);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
}

static void init_stream_fail_yuv_init(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_INIT, BK_ERR_PARAM);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EINVAL);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_NOSENSOR), 0);
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_INIT, BK_OK);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
}

static void init_stream_fail_h264_init(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_ret(MOCK_YUV_FN_H264_INIT, BK_ERR_TIMEOUT);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -ETIMEDOUT);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FREE), 1);
}

static void init_stream_fail_set_em_base(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_SET_EM_BASE, BK_ERR_PARAM);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EINVAL);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_DEINIT), 1);
}

static void init_stream_fail_route(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_sys_enable(1);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EIO);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_SYS_GROUP2_DISABLE), 2);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_DEINIT), 1);
}

static void init_stream_fail_isr_register(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_REGISTER_ISR,
                   BK_ERR_YUV_BUF_DRIVER_NOT_INIT);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -ENODEV);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_DEINIT), 1);
}

static void init_stream_fail_start(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();

  (void)state;
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_START, BK_ERR_BUSY);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), -EBUSY);
  assert_null(g_priv);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_ENCODE_ENABLE), 0);
}

/* ---------------------------------------------------------------------- */
/* uninitialize                                                           */
/* ---------------------------------------------------------------------- */

static void uninit_flow(void **state)
{
  const struct mock_yuv_call_s *c;
  unsigned int base;

  (void)state;
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), -ENODEV);
  base = 15;
  c = mock_yuv_call(base + 0);
  assert_int_equal(c->fn, MOCK_YUV_FN_H264_ENCODE_DISABLE);
  c = mock_yuv_call(base + 1);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_STOP);
  assert_int_equal(c->a0, H264_MODE);
  c = mock_yuv_call(base + 2);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_SOFT_RESET);
  c = mock_yuv_call(base + 3);
  assert_int_equal(c->fn, MOCK_YUV_FN_YUV_UNREGISTER_ISR);
  assert_int_equal(c->a0, YUV_BUF_H264_ERR);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_UNREGISTER_ISR), 2);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_DEINIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_DEINIT), 1);
  c = mock_yuv_call(base + 6);
  assert_int_equal(c->fn, MOCK_YUV_FN_SYS_GROUP2_DISABLE);
  assert_int_equal(c->a0, 1u);
  assert_int_equal(c->a1, H264_IRQ_BIT);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_REGISTER_ISR), 2);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FREE), 1);
}

static void uninit_rejects_foreign_priv(void **state)
{
  (void)state;
  assert_int_equal(
    bk7258_yuv_h264_uninitialize((struct bk7258_yuv_h264_s *)0x1), -EINVAL);
}

static void encode_not_initialized(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -ENODEV);
}

/* ---------------------------------------------------------------------- */
/* encode: argument validation                                            */
/* ---------------------------------------------------------------------- */

static void encode_arg_validation(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;

  assert_int_equal(
    bk7258_yuv_h264_encode(NULL, &input, &output), -EINVAL);
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, NULL, &output), -EINVAL);
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, NULL), -EINVAL);
  output.data = NULL;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EINVAL);
  output.data = g_out;

  input.length = FRAME_BYTES - 1;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EINVAL);
  input.length = FRAME_BYTES;

  input.data = g_cache;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EINVAL);
  input.data = g_input;

  output.data = g_cache;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EINVAL);
  output.data = g_out;

  output.data = g_input;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EINVAL);
  output.data = g_out;

  input.data = (const uint8_t *)(uintptr_t)0x100000000ULL;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EOVERFLOW);
  input.data = g_input;

  output.data = (uint8_t *)(uintptr_t)0x100000000ULL;
  assert_int_equal(
    bk7258_yuv_h264_encode(g_priv, &input, &output), -EOVERFLOW);
  output.data = g_out;

  assert_int_equal(mock_yuv_call_count(), 15);
}

static void encode_timeout_overflow(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_tickwait_hook(hook_fire_final);
  cfg.timeout_ms = UINT32_MAX;
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -ERANGE);
}

/* ---------------------------------------------------------------------- */
/* encode: happy path and DMA setup                                       */
/* ---------------------------------------------------------------------- */

static void encode_happy_path(void **state)
{
  struct bk7258_yuv_h264_output_s output;
  const dma_config_t *dc;
  int ret;

  (void)state;
  ret = run_happy_encode(&output);
  assert_int_equal(ret, 0);
  assert_int_equal(output.length, 9216u);
  assert_int_equal(mock_yuv_dest_start(), (uint32_t)(uintptr_t)g_out);
  assert_int_equal(mock_yuv_dest_end(),
                   (uint32_t)(uintptr_t)g_out + CAPACITY);
  assert_int_equal(mock_yuv_transfer_len(), CHUNK);
  assert_int_equal(mock_yuv_src_burst(), BURST_LEN_SINGLE);
  assert_int_equal(mock_yuv_dest_burst(), BURST_LEN_INC16);
  assert_int_equal(mock_yuv_src_sec(), DMA_ATTR_SEC);
  assert_int_equal(mock_yuv_dest_sec(), DMA_ATTR_SEC);

  dc = mock_yuv_dma_config();
  assert_true(mock_yuv_dma_config_valid());
  assert_int_equal(dc->mode, DMA_WORK_MODE_REPEAT);
  assert_int_equal(dc->chan_prio, 0);
  assert_int_equal(dc->src.dev, DMA_DEV_H264);
  assert_int_equal(dc->src.width, DMA_DATA_WIDTH_32BITS);
  assert_int_equal(dc->src.addr_inc_en, DMA_ADDR_INC_ENABLE);
  assert_int_equal(dc->src.addr_loop_en, DMA_ADDR_LOOP_ENABLE);
  assert_int_equal(dc->src.start_addr, 0x00c00000u);
  assert_int_equal(dc->src.end_addr, 0x00c00004u);
  assert_int_equal(dc->dst.dev, DMA_DEV_DTCM);
  assert_int_equal(dc->dst.width, DMA_DATA_WIDTH_32BITS);
  assert_int_equal(dc->dst.addr_inc_en, DMA_ADDR_INC_ENABLE);
  assert_int_equal(dc->dst.addr_loop_en, DMA_ADDR_LOOP_ENABLE);
  assert_int_equal(dc->dst.start_addr, (uint32_t)(uintptr_t)g_out);
  assert_int_equal(dc->dst.end_addr,
                   (uint32_t)(uintptr_t)g_out + CAPACITY);

  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_INIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_SET_DEST_ADDR), 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_ENABLE_FINISH), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_RENCODE_START), 4);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FLUSH_SRC), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_STOP), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_DISABLE_FINISH), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_SOFT_RESET), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_GET_COUNT), 1);

  /* The last two blocks were copied into the double-buffer slots. */
  assert_int_equal(g_cache[0], g_input[2 * BLOCK_BYTES]);
  assert_int_equal(g_cache[BLOCK_BYTES], g_input[3 * BLOCK_BYTES]);
  assert_memory_equal(g_cache, g_input + 2 * BLOCK_BYTES, 64);
  assert_memory_equal(g_cache + BLOCK_BYTES, g_input + 3 * BLOCK_BYTES, 64);
}

static void encode_dma_reuse_updates_dest(void **state)
{
  struct bk7258_yuv_h264_output_s output;
  const struct mock_yuv_call_s *c;
  int ret;

  (void)state;
  ret = run_happy_encode(&output);
  assert_int_equal(ret, 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_INIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_SET_DEST_ADDR), 0);

  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_happy);
  g_hook_guard = 0;
  output.data = g_out2;
  output.capacity = CAPACITY;
  output.length = 0;
  {
    struct bk7258_yuv_h264_input_s input;

    input.data = g_input;
    input.length = FRAME_BYTES;
    ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  }
  assert_int_equal(ret, 0);
  assert_int_equal(output.length, 9216u);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_INIT), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_SET_DEST_ADDR), 1);
  c = mock_yuv_call(mock_yuv_call_count() - 1);
  assert_int_equal(c->fn, MOCK_YUV_FN_DMA_GET_REMAIN);
  assert_int_equal(mock_yuv_dest_start(), (uint32_t)(uintptr_t)g_out2);
  assert_int_equal(mock_yuv_dest_end(),
                   (uint32_t)(uintptr_t)g_out2 + CAPACITY);
}

static void encode_dma_capacity_reject(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  output.capacity = CHUNK - 1;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EINVAL);
  output.capacity = CAPACITY;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
}

static void encode_dma_maxlen_reject(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_transfer_len_max(0);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -E2BIG);
  mock_yuv_set_transfer_len_max(CHUNK);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
}

static void encode_dma_init_fail(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_INIT, BK_ERR_DMA_TRANS_LEN);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EINVAL);
  assert_true(mock_yuv_dma_config_valid());
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_INIT, BK_OK);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_DEINIT), 1);
}

static void encode_dma_setup_step_fail(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int fns[5] =
  {
    MOCK_YUV_FN_DMA_SET_TRANSFER_LEN,
    MOCK_YUV_FN_DMA_SET_SRC_BURST,
    MOCK_YUV_FN_DMA_SET_DEST_BURST,
    MOCK_YUV_FN_DMA_SET_SRC_SEC,
    MOCK_YUV_FN_DMA_SET_DEST_SEC,
  };
  int i;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  for (i = 0; i < 5; i++)
    {
      struct bk7258_yuv_h264_config_s cfg = default_config();

      mock_yuv_sdk_reset();
      assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
      mock_yuv_set_ret(fns[i], BK_ERR_PARAM);
      assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                       -EINVAL);
      mock_yuv_set_ret(fns[i], BK_OK);
      assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                       -EIO);
      assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
      g_priv = NULL;
    }
}

static void encode_dma_start_fail(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_START, BK_ERR_DMA_NOT_INIT);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -ENODEV);
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_START, BK_OK);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FLUSH_SRC), 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_STOP), 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_DISABLE_FINISH), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 1);
}

static void encode_rencode_initial_fail(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_RENCODE_START, BK_ERR_BUSY);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EBUSY);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_STOP), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_DEINIT), 0);
}

static void encode_rencode_mid_fail(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_tickwait_hook(hook_mid_rencode_fail);
  g_hook_guard = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EBUSY);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 1);
}

/* ---------------------------------------------------------------------- */
/* encode: event flows                                                    */
/* ---------------------------------------------------------------------- */

static void encode_timeout_faults(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_tickwait_hook(hook_noop);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -ETIMEDOUT);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_DEINIT), 0);
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
}

static void encode_error_event_faults(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_tickwait_hook(hook_error_event);
  g_hook_guard = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
  assert_int_equal(output.length, 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 1);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
}

static void encode_dma_full_maps_to_enospc(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_tickwait_hook(hook_fire_full_then_silence);
  g_hook_guard = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -ENOSPC);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_STOP), 1);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_H264_SOFT_RESET), 1);
}

static void encode_dma_full_encoded_overflow(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(6144u);
  mock_yuv_set_tickwait_hook(hook_fire_full_then_silence);
  g_hook_guard = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -ENOSPC);
}

static void encode_final_one_chunk_lag_allowed(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_finish_and_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, 0);
  assert_int_equal(output.length, 9216u);
}

static void encode_final_remain_too_large(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(CHUNK + 1);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -EIO);
  assert_int_equal(output.length, 0);
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
}

static void encode_final_encoded_zero(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(0);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -EIO);
  assert_int_equal(output.length, 0);
}

static void encode_final_encoded_overflow_capacity(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(6144u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -ENOSPC);
  assert_int_equal(output.length, 0);
}

static void encode_final_written_overflow(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_finishes_and_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -EIO);
  assert_int_equal(output.length, 0);
}

static void encode_final_mismatch_short(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(2048u);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -EIO);
  assert_int_equal(output.length, 0);
}

static void encode_final_stop_frame_fail_faults(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_ret(MOCK_YUV_FN_YUV_SOFT_RESET, BK_ERR_TIMEOUT);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -ETIMEDOUT);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_YUV_SOFT_RESET), 1);
}

static void encode_final_flush_fail_faults(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_ret(MOCK_YUV_FN_DMA_FLUSH_SRC, BK_ERR_NOT_SUPPORT);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  ret = bk7258_yuv_h264_encode(g_priv, &input, &output);
  assert_int_equal(ret, -ENOTSUP);
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output),
                   -EIO);
}

static void encode_default_timeout_ok(void **state)
{
  struct bk7258_yuv_h264_config_s cfg = default_config();
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  cfg.timeout_ms = 0;
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
  assert_int_equal(bk7258_yuv_h264_initialize(&g_priv, &cfg), 0);
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output), 0);
  assert_int_equal(output.length, 9216u);
}

static void encode_stale_events_ignored(void **state)
{
  struct bk7258_yuv_h264_input_s input;
  struct bk7258_yuv_h264_output_s output;

  (void)state;
  mock_yuv_fire_h264_line();
  mock_yuv_fire_h264_final();
  mock_yuv_fire_h264_line();
  mock_yuv_set_encode_words(2304u);
  mock_yuv_set_remain_len(1024u);
  mock_yuv_set_tickwait_hook(hook_fire_final);
  g_hook_guard = 0;
  input.data = g_input;
  input.length = FRAME_BYTES;
  output.data = g_out;
  output.capacity = CAPACITY;
  output.length = 0;
  assert_int_equal(bk7258_yuv_h264_encode(g_priv, &input, &output), 0);
  assert_int_equal(output.length, 9216u);
}

/* ---------------------------------------------------------------------- */
/* misc                                                                   */
/* ---------------------------------------------------------------------- */

static void encode_happy_then_uninit(void **state)
{
  struct bk7258_yuv_h264_output_s output;
  int ret;

  (void)state;
  ret = run_happy_encode(&output);
  assert_int_equal(ret, 0);
  assert_int_equal(output.length, 9216u);
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), 0);
  assert_int_equal(mock_yuv_calls_of(MOCK_YUV_FN_DMA_FLUSH_SRC), 1);
  assert_int_equal(bk7258_yuv_h264_uninitialize(g_priv), -ENODEV);
}

int main(void)
{
  const struct CMUnitTest tests[] =
  {
    /* initialize */
    cmocka_unit_test_setup_teardown(init_geometry_reject, group_setup,
                                    teardown_reset),
    cmocka_unit_test_setup_teardown(init_param_checks, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_cache_reject, group_setup,
                                    teardown_reset),
    cmocka_unit_test_setup_teardown(init_stream_call_order, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_root_fail_rolls_back, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_dma_alloc_fail, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_register_isr_fail, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_stream_fail_yuv_init, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_stream_fail_h264_init, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_stream_fail_set_em_base,
                                    group_setup, teardown_reset),
    cmocka_unit_test_setup_teardown(init_stream_fail_route, group_setup,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(init_stream_fail_isr_register,
                                    group_setup, teardown_reset),
    cmocka_unit_test_setup_teardown(init_stream_fail_start, group_setup,
                                    teardown_ok),
    /* uninitialize */
    cmocka_unit_test_setup_teardown(uninit_flow, setup_ok,
                                    teardown_reset),
    cmocka_unit_test_setup_teardown(uninit_rejects_foreign_priv, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_not_initialized, setup_ok,
                                    teardown_ok),
    /* encode: argument validation */
    cmocka_unit_test_setup_teardown(encode_arg_validation, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_timeout_overflow, group_setup,
                                    teardown_ok),
    /* encode: happy path and DMA setup */
    cmocka_unit_test_setup_teardown(encode_happy_path, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_reuse_updates_dest,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_capacity_reject, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_maxlen_reject, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_init_fail, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_setup_step_fail,
                                    group_setup, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_start_fail, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_rencode_initial_fail, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_rencode_mid_fail, setup_ok,
                                    teardown_ok),
    /* encode: event flows */
    cmocka_unit_test_setup_teardown(encode_timeout_faults, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_error_event_faults, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_full_maps_to_enospc,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_dma_full_encoded_overflow,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_one_chunk_lag_allowed,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_remain_too_large,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_encoded_zero, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_encoded_overflow_capacity,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_written_overflow,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_mismatch_short, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_stop_frame_fail_faults,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_final_flush_fail_faults,
                                    setup_ok, teardown_ok),
    cmocka_unit_test_setup_teardown(encode_default_timeout_ok, setup_ok,
                                    teardown_ok),
    cmocka_unit_test_setup_teardown(encode_stale_events_ignored, setup_ok,
                                    teardown_ok),
    /* misc */
    cmocka_unit_test_setup_teardown(encode_happy_then_uninit, setup_ok,
                                    teardown_ok),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
