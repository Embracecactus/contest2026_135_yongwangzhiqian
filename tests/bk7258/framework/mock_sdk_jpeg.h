/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/framework/mock_sdk_jpeg.h
 *
 * Host-controlled stand-ins for the immutable v3.1.1.9 libbk_jpeg_decoder
 * symbols and the sys_driver interrupt-route ABI that
 * bk7258_jpeg_decoder.c bridges to.  Every SDK entry records its call
 * arguments so tests can assert call order and payloads; return values are
 * programmable per entry point.
 ****************************************************************************/

#ifndef __MOCK_SDK_JPEG_H
#define __MOCK_SDK_JPEG_H

#include <stdint.h>

#include <components/avdk_utils/avdk_error.h>
#include <components/media_types.h>

#define MOCK_SDK_LOG_MAX 16
#define MOCK_INTR_CORES_MAX 16

/* Opaque handle type shared with the driver translation unit. */
struct bk7258_sdk_jpeg_decode_hw_s;

struct mock_call_log_s
{
  size_t count;
  struct bk7258_sdk_jpeg_decode_hw_s *handles[MOCK_SDK_LOG_MAX];
};

/* Test control. */
void mock_jpeg_sdk_reset(void);
void mock_jpeg_sdk_set_new(avdk_err_t result, void *handle);
void mock_jpeg_sdk_set_open(avdk_err_t result);
void mock_jpeg_sdk_set_close(avdk_err_t result);
void mock_jpeg_sdk_set_delete(avdk_err_t result);
void mock_jpeg_sdk_set_decode(avdk_err_t result);
void mock_jpeg_sdk_set_decode_image(uint16_t width, uint16_t height);
void mock_jpeg_sdk_set_sys_enable(int32_t result);

/* SDK entry-point definitions (linked into the test binary). */
avdk_err_t bk_hardware_jpeg_decode_new(
  struct bk7258_sdk_jpeg_decode_hw_s **handle, void *config);
avdk_err_t bk_jpeg_decode_hw_open(
  struct bk7258_sdk_jpeg_decode_hw_s *handle);
avdk_err_t bk_jpeg_decode_hw_close(
  struct bk7258_sdk_jpeg_decode_hw_s *handle);
avdk_err_t bk_jpeg_decode_hw_decode(
  struct bk7258_sdk_jpeg_decode_hw_s *handle, frame_buffer_t *in_frame,
  frame_buffer_t *out_frame);
avdk_err_t bk_jpeg_decode_hw_delete(
  struct bk7258_sdk_jpeg_decode_hw_s *handle);

int32_t sys_drv_core_intr_group1_enable(uint32_t core_id, uint32_t param);
int32_t sys_drv_core_intr_group1_disable(uint32_t core_id, uint32_t param);

/* Observability: call counts and the last/recorded arguments. */
extern struct mock_call_log_s g_mock_new_calls;
extern struct mock_call_log_s g_mock_open_calls;
extern struct mock_call_log_s g_mock_close_calls;
extern struct mock_call_log_s g_mock_delete_calls;
extern size_t g_mock_decode_calls;
extern frame_buffer_t *g_mock_decode_in;
extern frame_buffer_t *g_mock_decode_out;
extern size_t g_mock_sys_enable_calls;
extern size_t g_mock_sys_disable_calls;
extern uint32_t g_mock_sys_enable_cores[MOCK_INTR_CORES_MAX];
extern uint32_t g_mock_sys_enable_params[MOCK_INTR_CORES_MAX];
extern uint32_t g_mock_sys_disable_cores[MOCK_INTR_CORES_MAX];
extern uint32_t g_mock_sys_disable_params[MOCK_INTR_CORES_MAX];

#endif /* __MOCK_SDK_JPEG_H */
