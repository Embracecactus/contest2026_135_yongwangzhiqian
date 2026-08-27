/****************************************************************************
 * tests/modules/ap/test_bk7258_jpeg_decoder.c
 *
 * Host unit tests for chips/bk7258/ap/bk7258_jpeg_decoder.c.
 *
 * The real implementation is compiled unmodified; the immutable SDK
 * (libbk_jpeg_decoder.a) and the sys_driver interrupt-route ABI are
 * replaced by framework/mock_sdk_jpeg.c, and the NuttX cache/kmalloc/mutex
 * surface by the mocks/nuttx shims + mocks/mock_nuttx_ap.c.
 *
 * NOTE: this target is built with -no-pie.  The driver rejects any address
 * range whose final byte exceeds 32 bits (the SDK speaks to 32-bit JPEG
 * registers), so data/static buffers must live below 4 GiB on the host.
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <cmocka.h>

#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <nuttx/mutex.h>

#include <common/bk_err.h>

#include <bk7258_jpeg_decoder.h>
#include <mock_sdk_jpeg.h>

/* JPEG bitstream builder ------------------------------------------------ */

#define JB_MAX 2048

struct jb
{
  uint8_t b[JB_MAX];
  size_t n;
};

static void jb_u8(struct jb *j, uint8_t v)
{
  assert_true(j->n < JB_MAX);
  j->b[j->n++] = v;
}

static void jb_be16(struct jb *j, uint16_t v)
{
  jb_u8(j, (uint8_t)(v >> 8));
  jb_u8(j, (uint8_t)(v & 0xffu));
}

static void jb_bytes(struct jb *j, const uint8_t *p, size_t n)
{
  assert_true(j->n + n <= JB_MAX);
  memcpy(&j->b[j->n], p, n);
  j->n += n;
}

static void jb_seg(struct jb *j, uint8_t marker, const uint8_t *payload,
                   uint16_t len)
{
  /* JPEG segment lengths include the two length bytes themselves. */
  jb_u8(j, 0xff);
  jb_u8(j, marker);
  jb_be16(j, len + 2);
  jb_bytes(j, payload, len);
}

static void jb_soi(struct jb *j)
{
  jb_u8(j, 0xff);
  jb_u8(j, 0xd8);
}

static void jb_eoi(struct jb *j)
{
  jb_u8(j, 0xff);
  jb_u8(j, 0xd9);
}

/* One DQT record: 8-bit precision, table `table`, values 1..64. */
static void jb_dqt_record(struct jb *j, uint8_t table)
{
  uint8_t rec[65];
  unsigned int i;

  rec[0] = table & 0x0fu;
  for (i = 1; i < 65; i++)
    {
      rec[i] = (uint8_t)i;
    }

  jb_bytes(j, rec, sizeof(rec));
}

static void jb_dqt(struct jb *j)
{
  struct jb seg;

  seg.n = 0;
  jb_dqt_record(&seg, 0);
  jb_dqt_record(&seg, 1);
  jb_seg(j, 0xdb, seg.b, (uint16_t)seg.n);
}

/* One minimal valid Huffman record: a single symbol of length 1. */
static void jb_dht_record(struct jb *j, uint8_t header)
{
  uint8_t rec[18];

  memset(rec, 0, sizeof(rec));
  rec[0] = header;
  if (header & 0x10)
    {
      rec[2] = 1;               /* one 2-bit symbol */
    }
  else
    {
      rec[1] = 1;               /* one 1-bit symbol */
    }

  rec[17] = 0x00;               /* symbol 0x00 */
  jb_bytes(j, rec, sizeof(rec));
}

static void jb_dht(struct jb *j)
{
  struct jb seg;

  seg.n = 0;
  jb_dht_record(&seg, 0x00);    /* DC0 */
  jb_dht_record(&seg, 0x01);    /* DC1 */
  jb_dht_record(&seg, 0x10);    /* AC0 */
  jb_dht_record(&seg, 0x11);    /* AC1 */
  jb_seg(j, 0xc4, seg.b, (uint16_t)seg.n);
}

static void jb_sof0(struct jb *j, uint16_t width, uint16_t height,
                    uint8_t y_sample)
{
  uint8_t sof[15];   /* standard SOF0 payload; segment length = 17 */

  sof[0] = 8;                   /* precision */
  sof[1] = (uint8_t)(height >> 8);
  sof[2] = (uint8_t)(height & 0xffu);
  sof[3] = (uint8_t)(width >> 8);
  sof[4] = (uint8_t)(width & 0xffu);
  sof[5] = 3;                   /* components */
  sof[6] = 1;                   /* Y id */
  sof[7] = y_sample;            /* Y sampling */
  sof[8] = 0;                   /* Y -> DQT0 */
  sof[9] = 2;                   /* Cb id */
  sof[10] = 0x11;               /* Cb sampling */
  sof[11] = 1;                  /* Cb -> DQT1 */
  sof[12] = 3;                  /* Cr id */
  sof[13] = 0x11;               /* Cr sampling */
  sof[14] = 1;                  /* Cr -> DQT1 */
  jb_seg(j, 0xc0, sof, sizeof(sof));
}

static void jb_sos(struct jb *j)
{
  uint8_t scan[10];

  scan[0] = 3;
  scan[1] = 1;
  scan[2] = 0;
  scan[3] = 2;
  scan[4] = 0x11;
  scan[5] = 3;
  scan[6] = 0x11;
  scan[7] = 0;
  scan[8] = 63;
  scan[9] = 0;
  jb_seg(j, 0xda, scan, sizeof(scan));
}

static void jb_entropy_eoi(struct jb *j)
{
  jb_eoi(j);
}

/* A complete, parser-accepted baseline stream. */
static void jb_happy(struct jb *j, uint16_t width, uint16_t height,
                     uint8_t y_sample)
{
  j->n = 0;
  jb_soi(j);
  jb_dqt(j);
  jb_dht(j);
  jb_sof0(j, width, height, y_sample);
  jb_sos(j);
  jb_entropy_eoi(j);
}

/* A happy stream with `app_segs` APP15 segments in the header (after DHT).
 * Header segments are parse-valid anywhere before SOS, so the stream length
 * can be grown without touching the entropy tail.
 */
static void jb_happy_app(struct jb *j, uint16_t width, uint16_t height,
                         uint8_t y_sample, size_t app_segs)
{
  uint8_t fill[64];
  size_t i;

  memset(fill, 0x11, sizeof(fill));
  j->n = 0;
  jb_soi(j);
  jb_dqt(j);
  jb_dht(j);
  for (i = 0; i < app_segs; i++)
    {
      jb_seg(j, 0xef, fill, sizeof(fill));
    }

  jb_sof0(j, width, height, y_sample);
  jb_sos(j);
  jb_entropy_eoi(j);
}

/* Test scaffolding ------------------------------------------------------ */

#define SAMPLE_444 0x11u
#define SAMPLE_422 0x21u
#define SAMPLE_420 0x22u
#define INTR_BIT   (1u << 26)
#define CORE_CPU1  1u
#define CORE_CPU2  2u

static struct jb g_jb;
static uint8_t g_out[4096] __attribute__((aligned(64)));
static uint8_t g_small[16];
static struct bk7258_jpeg_decoder_s *s_owner;

static int t_setup(void **state)
{
  mock_jpeg_sdk_reset();
  mock_cache_set_linesize(0);
  mock_mutex_fail_next(0);
  memset(&g_mock_cache_clean, 0, sizeof(g_mock_cache_clean));
  memset(&g_mock_cache_flush, 0, sizeof(g_mock_cache_flush));
  memset(&g_mock_cache_invalidate, 0, sizeof(g_mock_cache_invalidate));
  s_owner = NULL;
  return 0;
}

static int t_teardown(void **state)
{
  if (s_owner != NULL)
    {
      mock_jpeg_sdk_reset();
      assert_int_equal(bk7258_jpeg_decoder_uninitialize(s_owner), 0);
      s_owner = NULL;
    }

  return 0;
}

static void assert_route_cpu1(void)
{
  /* open() then disable(CPU2)+enable(CPU1). */
  assert_int_equal(g_mock_sys_disable_calls, 1);
  assert_int_equal(g_mock_sys_disable_cores[0], CORE_CPU2);
  assert_int_equal(g_mock_sys_disable_params[0], INTR_BIT);
  assert_int_equal(g_mock_sys_enable_calls, 1);
  assert_int_equal(g_mock_sys_enable_cores[0], CORE_CPU1);
  assert_int_equal(g_mock_sys_enable_params[0], INTR_BIT);
}

/* Initialize ------------------------------------------------------------ */

static void test_init_null_out(void **state)
{
  assert_int_equal(bk7258_jpeg_decoder_initialize(NULL), -EINVAL);
}

static void test_init_ok(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  assert_non_null(out);
  s_owner = out;
  assert_int_equal(g_mock_new_calls.count, 1);
  assert_int_equal(g_mock_open_calls.count, 1);
  assert_route_cpu1();
}

static void test_init_second_busy(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  out = (struct bk7258_jpeg_decoder_s *)(uintptr_t)1;
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EBUSY);
  /* The driver always clears *out before consulting the owner. */
  assert_null(out);
  assert_int_equal(g_mock_new_calls.count, 1);
}

static void test_init_new_inval(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_new(AVDK_ERR_INVAL, (void *)(uintptr_t)0x1000);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EINVAL);
  assert_null(out);
  assert_int_equal(g_mock_delete_calls.count, 1);
}

static void test_init_new_ok_null_handle(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_new(AVDK_ERR_OK, NULL);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EIO);
  assert_null(out);
  /* handle is NULL: nothing to delete, no orphan. */
  assert_int_equal(g_mock_delete_calls.count, 0);
}

static void test_init_new_fail_delete_fail_orphan(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_s *first;

  mock_jpeg_sdk_set_new(AVDK_ERR_HWERROR, (void *)(uintptr_t)0x1000);
  mock_jpeg_sdk_set_delete(AVDK_ERR_SHUTDOWN);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -ESHUTDOWN);
  assert_non_null(out);
  s_owner = out;
  first = out;
  assert_int_equal(g_mock_delete_calls.count, 1);

  /* The original caller lost the handle: a later initialize must hand it
   * back together with -EIO instead of constructing a second instance. */
  out = NULL;
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EIO);
  assert_ptr_equal(out, first);
  assert_int_equal(g_mock_new_calls.count, 1);
}

static void test_init_open_timeout(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_open(AVDK_ERR_TIMEOUT);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -ETIMEDOUT);
  assert_null(out);
  assert_int_equal(g_mock_open_calls.count, 1);
  assert_int_equal(g_mock_delete_calls.count, 1);
  assert_int_equal(g_mock_sys_enable_calls, 0);
}

static void test_init_open_fail_delete_fail_orphan(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_open(AVDK_ERR_BUSY);
  mock_jpeg_sdk_set_delete(AVDK_ERR_BUSY);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EBUSY);
  assert_non_null(out);
  s_owner = out;

  /* orphan is not opened: uninitialize skips close and retries delete. */
  assert_int_equal(g_mock_close_calls.count, 0);
  assert_int_equal(g_mock_delete_calls.count, 1);
}

static void test_init_route_fail(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_sys_enable(1);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EIO);
  assert_null(out);

  assert_int_equal(g_mock_sys_disable_calls, 2);
  assert_int_equal(g_mock_sys_disable_cores[0], CORE_CPU2);
  assert_int_equal(g_mock_sys_disable_cores[1], CORE_CPU1);
  assert_int_equal(g_mock_close_calls.count, 1);
  assert_int_equal(g_mock_delete_calls.count, 1);
}

static void test_init_route_fail_close_fail(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_sys_enable(1);
  mock_jpeg_sdk_set_close(AVDK_ERR_BUSY);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EBUSY);
  assert_non_null(out);
  s_owner = out;

  /* Retained as an open orphan: uninitialize retries close then delete. */
  assert_int_equal(g_mock_close_calls.count, 1);
  assert_int_equal(g_mock_delete_calls.count, 0);
}

static void test_init_route_fail_delete_fail(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_jpeg_sdk_set_sys_enable(1);
  mock_jpeg_sdk_set_delete(AVDK_ERR_BUSY);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EBUSY);
  assert_non_null(out);
  s_owner = out;

  assert_int_equal(g_mock_close_calls.count, 1);
  /* closed, then delete failed: orphan stays. */
  assert_int_equal(g_mock_delete_calls.count, 1);
}

static void test_init_lock_fail(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  mock_mutex_fail_next(1);
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EAGAIN);
  assert_int_equal(g_mock_new_calls.count, 0);
}

/* Uninitialize ---------------------------------------------------------- */

static void test_uninit_null(void **state)
{
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(NULL), -EINVAL);
}

static void test_uninit_not_owner(void **state)
{
  char junk[16];

  assert_int_equal(
    bk7258_jpeg_decoder_uninitialize(
      (FAR struct bk7258_jpeg_decoder_s *)(void *)junk), -EINVAL);
}

static void test_uninit_ok(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(out), 0);
  s_owner = NULL;

  assert_int_equal(g_mock_close_calls.count, 1);
  assert_int_equal(g_mock_sys_disable_calls, 2);   /* CPU2 then CPU1 */
  assert_int_equal(g_mock_sys_disable_cores[1], CORE_CPU1);
  assert_int_equal(g_mock_delete_calls.count, 1);

  /* Stale-handle use after teardown. */
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(out), -EINVAL);
}

static void test_uninit_close_fail_retry(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  mock_jpeg_sdk_set_close(AVDK_ERR_BUSY);
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(out), -EBUSY);
  assert_int_equal(g_mock_close_calls.count, 1);
  assert_int_equal(g_mock_delete_calls.count, 0);

  /* Ownership was kept: retry completes once the SDK recovers. */
  mock_jpeg_sdk_set_close(AVDK_ERR_OK);
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(out), 0);
  s_owner = NULL;
  assert_int_equal(g_mock_close_calls.count, 2);
  assert_int_equal(g_mock_delete_calls.count, 1);
}

static void test_uninit_delete_fail_blocks_reinit(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_s *saved;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  saved = out;

  mock_jpeg_sdk_set_delete(AVDK_ERR_BUSY);
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(saved), -EBUSY);

  /* A delete-failed owner blocks new instances until uninitialize retried.
   * initialize always clears *out on this path, so use the saved handle. */
  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), -EBUSY);
  assert_null(out);

  mock_jpeg_sdk_set_delete(AVDK_ERR_OK);
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(saved), 0);
  s_owner = NULL;
  assert_int_equal(g_mock_delete_calls.count, 2);
}

/* get_info: ownership gating ------------------------------------------- */

static void test_getinfo_null_info(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, NULL), -EINVAL);
}

static void test_getinfo_not_owner(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_get_info(NULL, &in, &info),
                   -EINVAL);

  /* Bogus non-owner pointer. */
  assert_int_equal(
    bk7258_jpeg_decoder_get_info(
      (FAR struct bk7258_jpeg_decoder_s *)(uintptr_t)0x1234, &in, &info),
    -ENODEV);
}

/* get_info: parser ------------------------------------------------------ */

static void test_parse_444(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;
  int ret;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  ret = bk7258_jpeg_decoder_get_info(out, &in, &info);
  assert_int_equal(ret, 0);
  assert_int_equal(info.width, 3);
  assert_int_equal(info.height, 3);
  assert_int_equal(info.format, BK7258_JPEG_DECODER_YUV444);
}

static void test_parse_422(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 16, 8, SAMPLE_422);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), 0);
  assert_int_equal(info.width, 16);
  assert_int_equal(info.height, 8);
  assert_int_equal(info.format, BK7258_JPEG_DECODER_YUV422);
}

static void test_parse_420(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 640, 480, SAMPLE_420);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), 0);
  assert_int_equal(info.width, 640);
  assert_int_equal(info.height, 480);
  assert_int_equal(info.format, BK7258_JPEG_DECODER_YUV420);
}

static void test_parse_no_soi(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  g_jb.n = 0;
  jb_u8(&g_jb, 0x00);
  jb_u8(&g_jb, 0x00);
  jb_eoi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_soi_only(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  g_jb.n = 0;
  jb_soi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_trailing_prefix(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  g_jb.n = 0;
  jb_soi(&g_jb);
  jb_u8(&g_jb, 0xff);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_sof1_rejected(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;
  uint8_t sof[17];

  memset(sof, 0, sizeof(sof));
  sof[0] = 8;
  sof[5] = 1;
  g_jb.n = 0;
  jb_soi(&g_jb);
  jb_seg(&g_jb, 0xc1, sof, sizeof(sof));
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sof0_bad_precision(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* SOF payload: SOI(2)+DQT seg(133)+DHT seg(77)+SOF len(3) -> 215. */
  g_jb.b[215 + 1] = 12;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sof0_bad_components(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[215 + 6] = 2;   /* SOF ncomp */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sof0_zero_dims(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[215 + 2] = 0;   /* SOF height hi */
  g_jb.b[215 + 3] = 0;   /* SOF height lo */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_sof0_dup_ids(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[215 + 10] = 1;   /* Cb id == Y id */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sof0_bad_ysample(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[215 + 8] = 0x12;   /* Y sampling */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sof0_bad_chroma_sample(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[215 + 12] = 0x22;   /* Cb v-factor */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sof0_twice(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  g_jb.n = 0;   /* two SOF0 segments before the single scan */
  jb_soi(&g_jb);
  jb_dqt(&g_jb);
  jb_dht(&g_jb);
  jb_sof0(&g_jb, 3, 3, SAMPLE_444);
  jb_sof0(&g_jb, 3, 3, SAMPLE_444);
  jb_sos(&g_jb);
  jb_entropy_eoi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dqt_pq_nonzero(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* First DQT record (payload starts at byte 6): pq=1. */
  g_jb.b[6] = 0x10;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dqt_table2(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[6] = 0x02;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dqt_dup_table(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[6 + 65] = 0x00;   /* second record repeats table 0 */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dqt_length_bad(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;
  uint8_t empty[2] = {0, 0};

  g_jb.n = 0;
  jb_soi(&g_jb);
  jb_seg(&g_jb, 0xdb, empty, sizeof(empty));
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_dqt_zero_value(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[6 + 1] = 0;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_dht_class2(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* First DHT record header (SOI 2 + DQT seg 133 + DHT len 2). */
  g_jb.b[140] = 0x20;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dht_overflow_counts(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[141] = 2;   /* DC0: two 1-bit symbols */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dht_zero_symbols(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[141] = 0;   /* DC0: no symbols */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dht_dc_symbol_too_big(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* DC0 symbol slot: header(1)+counts(16)=17 bytes into DHT payload. */
  g_jb.b[140 + 17] = 12;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dht_ac_size_11(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* AC0 symbol slot: DC0(18)+DC1(18)+header(1)+counts(16)=53. */
  g_jb.b[140 + 53] = 0x1b;   /* run=1, size=11 */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dht_ac_odd_zero(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[140 + 53] = 0x10;   /* run=1, size=0 */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_dht_dup_symbol(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[142] = 1;   /* DC0: two 1-bit symbols (one at len 1, one at len 2) */
  g_jb.b[140 + 17] = 0;  /* both 0x00 -> duplicate */
  g_jb.b[140 + 18] = 0;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_dht_dup_table(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[158] = 0x00;   /* DC1 record header -> repeats DC0 table */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_sos_before_sof(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  g_jb.n = 0;
  jb_soi(&g_jb);
  jb_dqt(&g_jb);
  jb_dht(&g_jb);
  jb_sos(&g_jb);
  jb_entropy_eoi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_missing_dqt(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  g_jb.n = 0;
  jb_soi(&g_jb);
  jb_dht(&g_jb);
  jb_sof0(&g_jb, 3, 3, SAMPLE_444);
  jb_sos(&g_jb);
  jb_entropy_eoi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_scan_mismatch_ids(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.b[235 + 1] = 99;   /* SOS first id */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_scan_mismatch_tables(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* ysel first scan byte: must be 0. */
  g_jb.b[235 + 2] = 1;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_no_eoi(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.n -= 2;   /* drop EOI */
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_trailing_after_eoi(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  jb_u8(&g_jb, 0x00);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EINVAL);
}

static void test_parse_marker_in_entropy(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;
  uint8_t ext[4];

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  /* Replace entropy EOI with a mid-scan DHT marker: reject. */
  ext[0] = 0xff;
  ext[1] = 0xc4;
  ext[2] = 0x00;
  ext[3] = 0x00;
  memcpy(&g_jb.b[g_jb.n - 2], ext, sizeof(ext));
  g_jb.b[g_jb.n + 2] = 0;
  g_jb.n += 2;
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info),
                   -ENOTSUP);
}

static void test_parse_stuff_restart_then_eoi(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  g_jb.n -= 2;   /* drop the base EOI, then re-emit entropy below */
  /* Entropy: 0xff 0x00 fill, 0xff 0xd0 restart, then EOI. */
  jb_u8(&g_jb, 0xff);
  jb_u8(&g_jb, 0x00);
  jb_u8(&g_jb, 0xff);
  jb_u8(&g_jb, 0xd0);
  jb_eoi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), 0);
}

static void test_parse_header_segments_app_com_dri(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_info_s info;
  struct bk7258_jpeg_decoder_s *out = NULL;
  uint8_t app[16];
  uint8_t com[4] = {0x68, 0x69, 0x21, 0x21};
  uint8_t dri[2] = {0x00, 0x02};

  memset(app, 0x55, sizeof(app));
  g_jb.n = 0;
  jb_soi(&g_jb);
  jb_seg(&g_jb, 0xe0, app, sizeof(app));
  jb_seg(&g_jb, 0xfe, com, sizeof(com));
  jb_dqt(&g_jb);
  jb_dht(&g_jb);
  jb_sof0(&g_jb, 3, 3, SAMPLE_444);
  jb_seg(&g_jb, 0xdd, dri, sizeof(dri));
  jb_sos(&g_jb);
  jb_entropy_eoi(&g_jb);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), 0);
  assert_int_equal(info.format, BK7258_JPEG_DECODER_YUV444);
}

/* Decode: descriptor validation ---------------------------------------- */

static void test_decode_validation_ladder(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;
  struct bk7258_jpeg_decoder_frame_s saved;
  int ret;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  saved = in;

  /* NULL input / empty descriptors. */
  ret = bk7258_jpeg_decoder_decode(out, NULL, &out_f);
  assert_int_equal(ret, -EINVAL);
  assert_int_equal(out_f.length, 0);

  in.data = NULL;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  in = saved;

  in.length = 0;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  in = saved;

  in.capacity = 0;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  in = saved;

  in.length = in.capacity + 1;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  in = saved;

  /* Output descriptor. */
  out_f.data = NULL;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  out_f.data = g_out;

  out_f.capacity = 0;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  out_f.capacity = sizeof(g_out);

  /* 32-bit address contract. */
  in.data = (FAR uint8_t *)((uintptr_t)0x1ull << 32);
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f),
                   -EOVERFLOW);
  in = saved;

  in.data = (FAR uint8_t *)(UINTPTR_MAX - 3);
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f),
                   -EOVERFLOW);
  in = saved;

  out_f.data = (FAR uint8_t *)((uintptr_t)0x1ull << 32);
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f),
                   -EOVERFLOW);
  assert_int_equal(out_f.length, 0);
  out_f.data = g_out;

  /* Overlap. */
  in.data = g_out;
  in.capacity = sizeof(g_out);
  in.length = in.capacity;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  in = saved;

  assert_int_equal(g_mock_decode_calls, 0);
}

static void test_decode_not_owner(void **state)
{
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(
    bk7258_jpeg_decoder_decode(
      (FAR struct bk7258_jpeg_decoder_s *)(uintptr_t)0x1234, &in, &out_f),
    -ENODEV);

  assert_int_equal(bk7258_jpeg_decoder_decode(NULL, &in, &out_f), -EINVAL);
  assert_int_equal(g_mock_decode_calls, 0);
}

static void test_decode_lock_fail(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  mock_mutex_fail_next(1);
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EAGAIN);
  assert_int_equal(g_mock_decode_calls, 0);
}

static void test_decode_ok_default_cache(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;
  frame_buffer_t sdk_in;
  frame_buffer_t sdk_out;
  uintptr_t guarded;
  uintptr_t rounded;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  mock_jpeg_sdk_set_decode_image(3, 3);

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), 0);

  /* Snapshot the SDK frame descriptors immediately: they live in the
   * driver's dead stack frame and later calls would reuse that memory. */
  sdk_in = *g_mock_decode_in;
  sdk_out = *g_mock_decode_out;

  assert_int_equal(out_f.length, 3 * 3 * 2);
  assert_int_equal(out_f.width, 3);
  assert_int_equal(out_f.height, 3);
  assert_int_equal(in.width, 3);
  assert_int_equal(in.height, 3);
  assert_int_equal(g_mock_decode_calls, 1);

  /* The SDK never sees the caller's input buffer: it receives the
   * guarded bounce. */
  assert_int_not_equal((uintptr_t)sdk_in.frame, (uintptr_t)g_jb.b);
  guarded = (uintptr_t)g_jb.n + 2048;
  rounded = (guarded + 7) & ~(uintptr_t)7;
  assert_int_equal(sdk_in.length, (uint32_t)g_jb.n);
  assert_int_equal(sdk_in.size, (uint32_t)rounded);
  assert_int_equal(sdk_in.fmt, PIXEL_FMT_JPEG);
  assert_int_equal(sdk_in.width, 3);
  assert_int_equal(sdk_in.height, 3);

  assert_int_equal((uintptr_t)sdk_out.frame, (uintptr_t)g_out);
  assert_int_equal(sdk_out.size, sizeof(g_out));
  assert_int_equal(sdk_out.fmt, PIXEL_FMT_YUYV);

  /* Cache maintenance around the transfer. */
  assert_int_equal(g_mock_cache_clean.count, 1);
  assert_int_equal(g_mock_cache_clean.end[0] - g_mock_cache_clean.start[0],
                   guarded);
  assert_int_equal(g_mock_cache_flush.count, 1);
  assert_int_equal(g_mock_cache_flush.start[0], (uintptr_t)g_out);
  assert_int_equal(g_mock_cache_flush.end[0] - g_mock_cache_flush.start[0],
                   24);   /* 18-byte payload on 8-byte lines */
  assert_int_equal(g_mock_cache_invalidate.count, 1);
  assert_int_equal(g_mock_cache_invalidate.end[0] -
                     g_mock_cache_invalidate.start[0], 24);
}

static void test_decode_ok_line64(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;
  uintptr_t guarded;

  mock_cache_set_linesize(64);

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  mock_jpeg_sdk_set_decode_image(3, 3);

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), 0);

  guarded = (uintptr_t)g_jb.n + 2048;
  /* The clean span is the unrounded guarded size; the output span is a
   * whole number of 64-byte lines. */
  assert_int_equal(g_mock_cache_clean.end[0] - g_mock_cache_clean.start[0],
                   guarded);
  assert_int_equal(g_mock_cache_flush.end[0] - g_mock_cache_flush.start[0],
                   64);
  assert_int_equal(g_mock_cache_invalidate.end[0] -
                     g_mock_cache_invalidate.start[0], 64);
}

static void test_decode_unaligned_line64(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  mock_cache_set_linesize(64);

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out + 8;
  out_f.capacity = sizeof(g_out) - 8;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EINVAL);
  assert_int_equal(g_mock_decode_calls, 0);
}

static void test_decode_non_power_of_two_line(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  mock_cache_set_linesize(3);

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EIO);
  assert_int_equal(g_mock_decode_calls, 0);
}

static void test_decode_enospc_small_buf(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_small;
  out_f.capacity = 4;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -ENOSPC);
  assert_int_equal(out_f.length, 0);
  assert_int_equal(g_mock_decode_calls, 0);

  /* Not faulted: a larger buffer decodes fine. */
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);
  mock_jpeg_sdk_set_decode_image(3, 3);
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), 0);
}

static void test_decode_enospc_rounded_span(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  mock_cache_set_linesize(64);

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = 63;   /* payload 18 fits but 64-byte span does not */

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -ENOSPC);
  assert_int_equal(g_mock_decode_calls, 0);
}

static void test_decode_sdk_error_faults(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;
  struct bk7258_jpeg_decoder_info_s info;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  mock_jpeg_sdk_set_decode_image(3, 3);
  mock_jpeg_sdk_set_decode(AVDK_ERR_HWERROR);

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EIO);
  assert_int_equal(out_f.length, 0);
  assert_int_equal(g_mock_decode_calls, 1);

  /* The handle is faulted: no further SDK traffic. */
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EIO);
  assert_int_equal(g_mock_decode_calls, 1);
  assert_int_equal(bk7258_jpeg_decoder_get_info(out, &in, &info), -EIO);

  /* A fresh generation works again. */
  mock_jpeg_sdk_reset();
  mock_jpeg_sdk_set_decode_image(3, 3);
  assert_int_equal(bk7258_jpeg_decoder_uninitialize(out), 0);
  s_owner = NULL;

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), 0);
}

static void test_decode_sdk_timeout_faults(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  mock_jpeg_sdk_set_decode(BK_ERR_TIMEOUT);

  /* The SDK timeout surfaces as its hardware-error status: -EIO. */
  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EIO);
  assert_int_equal(out_f.length, 0);
}

static void test_decode_dims_mismatch_faults(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);
  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  mock_jpeg_sdk_set_decode_image(4, 3);

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), -EIO);
  assert_int_equal(out_f.length, 0);
  assert_int_equal(in.width, 0);
  assert_int_equal(in.height, 0);
}

static void test_decode_bounce_grow(void **state)
{
  struct bk7258_jpeg_decoder_s *out = NULL;
  struct bk7258_jpeg_decoder_frame_s in;
  struct bk7258_jpeg_decoder_frame_s out_f;
  static struct jb longer;
  frame_buffer_t sdk_in;

  jb_happy(&g_jb, 3, 3, SAMPLE_444);

  memset(&in, 0, sizeof(in));
  in.data = g_jb.b;
  in.capacity = (uint32_t)g_jb.n;
  in.length = (uint32_t)g_jb.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_initialize(&out), 0);
  s_owner = out;
  mock_jpeg_sdk_set_decode_image(3, 3);

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), 0);
  sdk_in = *g_mock_decode_in;
  assert_int_equal(sdk_in.size,
                   (uint32_t)((((uintptr_t)g_jb.n + 2048) + 7) & ~(uintptr_t)7));

  /* A longer, still parse-valid stream forces the bounce to grow. */
  jb_happy_app(&longer, 3, 3, SAMPLE_444, 2);
  assert_true(longer.n > g_jb.n);
  memset(&in, 0, sizeof(in));
  in.data = longer.b;
  in.capacity = (uint32_t)longer.n;
  in.length = (uint32_t)longer.n;
  memset(&out_f, 0, sizeof(out_f));
  out_f.data = g_out;
  out_f.capacity = sizeof(g_out);

  assert_int_equal(bk7258_jpeg_decoder_decode(out, &in, &out_f), 0);
  sdk_in = *g_mock_decode_in;
  assert_int_equal(sdk_in.size,
                   (uint32_t)((((uintptr_t)longer.n + 2048) + 7) & ~(uintptr_t)7));
}

/* Suite ----------------------------------------------------------------- */

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(test_init_null_out, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_init_ok, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_init_second_busy, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_init_new_inval, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_init_new_ok_null_handle, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_init_new_fail_delete_fail_orphan,
                                    t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_init_open_timeout, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_init_open_fail_delete_fail_orphan,
                                    t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_init_route_fail, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_init_route_fail_close_fail, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_init_route_fail_delete_fail, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_init_lock_fail, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_uninit_null, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_uninit_not_owner, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_uninit_ok, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_uninit_close_fail_retry, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_uninit_delete_fail_blocks_reinit,
                                    t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_getinfo_null_info, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_getinfo_not_owner, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_444, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_422, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_420, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_no_soi, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_soi_only, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_trailing_prefix, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof1_rejected, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_bad_precision, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_bad_components, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_zero_dims, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_dup_ids, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_bad_ysample, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_bad_chroma_sample,
                                    t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sof0_twice, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dqt_pq_nonzero, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dqt_table2, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dqt_dup_table, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dqt_length_bad, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dqt_zero_value, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_class2, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_overflow_counts, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_zero_symbols, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_dc_symbol_too_big, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_ac_size_11, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_ac_odd_zero, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_dup_symbol, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_dht_dup_table, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_sos_before_sof, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_missing_dqt, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_scan_mismatch_ids, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_scan_mismatch_tables, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_no_eoi, t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_trailing_after_eoi, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_marker_in_entropy, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_stuff_restart_then_eoi, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_parse_header_segments_app_com_dri,
                                    t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_validation_ladder, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_not_owner, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_lock_fail, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_ok_default_cache, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_ok_line64, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_unaligned_line64, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_non_power_of_two_line,
                                    t_setup, t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_enospc_small_buf, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_enospc_rounded_span, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_sdk_error_faults, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_sdk_timeout_faults, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_dims_mismatch_faults, t_setup,
                                    t_teardown),
    cmocka_unit_test_setup_teardown(test_decode_bounce_grow, t_setup,
                                    t_teardown),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
