/****************************************************************************
 * tests/framework/mock_sdk_jpeg.c
 *
 * Host stand-ins for libbk_jpeg_decoder.a and the sys_driver intr-route
 * ABI.  Every SDK entry records its call arguments and honors the
 * programmable results installed by mock_jpeg_sdk_set_*().
 ****************************************************************************/

#include <string.h>

#include <mock_sdk_jpeg.h>

struct mock_call_log_s g_mock_new_calls;
struct mock_call_log_s g_mock_open_calls;
struct mock_call_log_s g_mock_close_calls;
struct mock_call_log_s g_mock_delete_calls;
size_t g_mock_decode_calls;
frame_buffer_t *g_mock_decode_in;
frame_buffer_t *g_mock_decode_out;
size_t g_mock_sys_enable_calls;
size_t g_mock_sys_disable_calls;
uint32_t g_mock_sys_enable_cores[MOCK_INTR_CORES_MAX];
uint32_t g_mock_sys_enable_params[MOCK_INTR_CORES_MAX];
uint32_t g_mock_sys_disable_cores[MOCK_INTR_CORES_MAX];
uint32_t g_mock_sys_disable_params[MOCK_INTR_CORES_MAX];

/* The concrete handle storage; the driver TU only uses the opaque tag. */
struct bk7258_sdk_jpeg_decode_hw_s
{
  int dummy;
};

static struct bk7258_sdk_jpeg_decode_hw_s g_mock_handle;
static avdk_err_t g_mock_new_ret;
static struct bk7258_sdk_jpeg_decode_hw_s *g_mock_new_handle;
static avdk_err_t g_mock_open_ret;
static avdk_err_t g_mock_close_ret;
static avdk_err_t g_mock_delete_ret;
static avdk_err_t g_mock_decode_ret;
static uint16_t g_mock_decode_width;
static uint16_t g_mock_decode_height;
static int32_t g_mock_sys_enable_ret;

void mock_jpeg_sdk_reset(void)
{
  memset(&g_mock_new_calls, 0, sizeof(g_mock_new_calls));
  memset(&g_mock_open_calls, 0, sizeof(g_mock_open_calls));
  memset(&g_mock_close_calls, 0, sizeof(g_mock_close_calls));
  memset(&g_mock_delete_calls, 0, sizeof(g_mock_delete_calls));
  g_mock_decode_calls = 0;
  g_mock_decode_in = NULL;
  g_mock_decode_out = NULL;
  g_mock_sys_enable_calls = 0;
  g_mock_sys_disable_calls = 0;

  g_mock_new_ret = AVDK_ERR_OK;
  g_mock_new_handle = &g_mock_handle;
  g_mock_open_ret = AVDK_ERR_OK;
  g_mock_close_ret = AVDK_ERR_OK;
  g_mock_delete_ret = AVDK_ERR_OK;
  g_mock_decode_ret = AVDK_ERR_OK;
  g_mock_decode_width = 0;
  g_mock_decode_height = 0;
  g_mock_sys_enable_ret = 0;
}

void mock_jpeg_sdk_set_new(avdk_err_t result, void *handle)
{
  g_mock_new_ret = result;
  g_mock_new_handle = (struct bk7258_sdk_jpeg_decode_hw_s *)handle;
}

void mock_jpeg_sdk_set_open(avdk_err_t result)
{
  g_mock_open_ret = result;
}

void mock_jpeg_sdk_set_close(avdk_err_t result)
{
  g_mock_close_ret = result;
}

void mock_jpeg_sdk_set_delete(avdk_err_t result)
{
  g_mock_delete_ret = result;
}

void mock_jpeg_sdk_set_decode(avdk_err_t result)
{
  g_mock_decode_ret = result;
}

void mock_jpeg_sdk_set_decode_image(uint16_t width, uint16_t height)
{
  g_mock_decode_width = width;
  g_mock_decode_height = height;
}

void mock_jpeg_sdk_set_sys_enable(int32_t result)
{
  g_mock_sys_enable_ret = result;
}

static void mock_log_record(struct mock_call_log_s *log, void *handle)
{
  struct bk7258_sdk_jpeg_decode_hw_s *h =
    (struct bk7258_sdk_jpeg_decode_hw_s *)handle;

  if (log->count < MOCK_SDK_LOG_MAX)
    {
      log->handles[log->count] = h;
    }

  log->count++;
}

avdk_err_t bk_hardware_jpeg_decode_new(
  struct bk7258_sdk_jpeg_decode_hw_s **handle, void *config)
{
  (void)config;
  mock_log_record(&g_mock_new_calls, g_mock_new_handle);
  if (handle != NULL)
    {
      *handle = g_mock_new_handle;
    }

  return g_mock_new_ret;
}

avdk_err_t bk_jpeg_decode_hw_open(struct bk7258_sdk_jpeg_decode_hw_s *handle)
{
  mock_log_record(&g_mock_open_calls, handle);
  return g_mock_open_ret;
}

avdk_err_t bk_jpeg_decode_hw_close(struct bk7258_sdk_jpeg_decode_hw_s *handle)
{
  mock_log_record(&g_mock_close_calls, handle);
  return g_mock_close_ret;
}

avdk_err_t bk_jpeg_decode_hw_delete(struct bk7258_sdk_jpeg_decode_hw_s *handle)
{
  mock_log_record(&g_mock_delete_calls, handle);
  return g_mock_delete_ret;
}

avdk_err_t bk_jpeg_decode_hw_decode(
  struct bk7258_sdk_jpeg_decode_hw_s *handle, frame_buffer_t *in_frame,
  frame_buffer_t *out_frame)
{
  (void)handle;
  g_mock_decode_calls++;
  g_mock_decode_in = in_frame;
  g_mock_decode_out = out_frame;
  if (out_frame != NULL)
    {
      out_frame->width = g_mock_decode_width;
      out_frame->height = g_mock_decode_height;
    }

  return g_mock_decode_ret;
}

int32_t sys_drv_core_intr_group1_enable(uint32_t core_id, uint32_t param)
{
  if (g_mock_sys_enable_calls < MOCK_INTR_CORES_MAX)
    {
      g_mock_sys_enable_cores[g_mock_sys_enable_calls] = core_id;
      g_mock_sys_enable_params[g_mock_sys_enable_calls] = param;
    }

  g_mock_sys_enable_calls++;
  return g_mock_sys_enable_ret;
}

int32_t sys_drv_core_intr_group1_disable(uint32_t core_id, uint32_t param)
{
  if (g_mock_sys_disable_calls < MOCK_INTR_CORES_MAX)
    {
      g_mock_sys_disable_cores[g_mock_sys_disable_calls] = core_id;
      g_mock_sys_disable_params[g_mock_sys_disable_calls] = param;
    }

  g_mock_sys_disable_calls++;
  return 0;
}