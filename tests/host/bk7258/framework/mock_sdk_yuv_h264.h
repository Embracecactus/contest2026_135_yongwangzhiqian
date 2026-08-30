/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/framework/mock_sdk_yuv_h264.h
 *
 * Host stand-in for the immutable v3.1.1.9 DMA / H.264 / YUV-buffer driver
 * bundle used by bk7258_yuv_h264.c, plus the board media-root helper and
 * the CPU1 interrupt-route register (sys_drv_core_intr_group2_*).
 *
 * Every SDK entry point is logged in order with its arguments, and every
 * bk_err_t return value is programmable per function, so the driver's
 * rollback/retry paths can be exercised deterministically.  ISR callbacks
 * the driver registers are captured and re-fired on demand, letting tests
 * drive the encode line/final/error state machine single-threaded.
 ****************************************************************************/

#ifndef __MOCK_SDK_YUV_H264_H
#define __MOCK_SDK_YUV_H264_H

#include <stdbool.h>
#include <stdint.h>

#include <driver/dma.h>
#include <driver/h264.h>
#include <driver/yuv_buf.h>

/* Function ids shared by the call log and the per-function result table. */
enum
{
  MOCK_YUV_FN_DMA_DRIVER_INIT = 0,
  MOCK_YUV_FN_DMA_ALLOC,
  MOCK_YUV_FN_DMA_FREE,
  MOCK_YUV_FN_DMA_INIT,
  MOCK_YUV_FN_DMA_DEINIT,
  MOCK_YUV_FN_DMA_START,
  MOCK_YUV_FN_DMA_STOP,
  MOCK_YUV_FN_DMA_FLUSH_SRC,
  MOCK_YUV_FN_DMA_REGISTER_ISR,
  MOCK_YUV_FN_DMA_SET_DEST_ADDR,
  MOCK_YUV_FN_DMA_SET_TRANSFER_LEN,
  MOCK_YUV_FN_DMA_SET_SRC_BURST,
  MOCK_YUV_FN_DMA_SET_DEST_BURST,
  MOCK_YUV_FN_DMA_SET_SRC_SEC,
  MOCK_YUV_FN_DMA_SET_DEST_SEC,
  MOCK_YUV_FN_DMA_ENABLE_FINISH,
  MOCK_YUV_FN_DMA_DISABLE_FINISH,
  MOCK_YUV_FN_DMA_GET_MAX_LEN,
  MOCK_YUV_FN_DMA_GET_REMAIN,
  MOCK_YUV_FN_H264_DRIVER_INIT,
  MOCK_YUV_FN_H264_INIT,
  MOCK_YUV_FN_H264_DEINIT,
  MOCK_YUV_FN_H264_ENCODE_ENABLE,
  MOCK_YUV_FN_H264_ENCODE_DISABLE,
  MOCK_YUV_FN_H264_SOFT_RESET,
  MOCK_YUV_FN_H264_GET_FIFO,
  MOCK_YUV_FN_H264_GET_COUNT,
  MOCK_YUV_FN_H264_REGISTER_ISR,
  MOCK_YUV_FN_H264_UNREGISTER_ISR,
  MOCK_YUV_FN_YUV_DRIVER_INIT,
  MOCK_YUV_FN_YUV_INIT,
  MOCK_YUV_FN_YUV_DEINIT,
  MOCK_YUV_FN_YUV_NOSENSOR,
  MOCK_YUV_FN_YUV_SET_EM_BASE,
  MOCK_YUV_FN_YUV_START,
  MOCK_YUV_FN_YUV_STOP,
  MOCK_YUV_FN_YUV_RENCODE_START,
  MOCK_YUV_FN_YUV_REGISTER_ISR,
  MOCK_YUV_FN_YUV_UNREGISTER_ISR,
  MOCK_YUV_FN_YUV_SOFT_RESET,
  MOCK_YUV_FN_JPEG_DRIVER_INIT,
  MOCK_YUV_FN_SYS_GROUP2_ENABLE,
  MOCK_YUV_FN_SYS_GROUP2_DISABLE,
  MOCK_YUV_FN_MEDIA_ROOT_INIT,
  MOCK_YUV_FN_MAX,
};

#define MOCK_YUV_LOG_MAX 128

struct mock_yuv_call_s
{
  int fn;
  uint32_t a0;
  uint32_t a1;
  uint32_t a2;
};

/* Programmable results -------------------------------------------------- */

void mock_yuv_sdk_reset(void);
void mock_yuv_set_ret(int fn, int ret);
int mock_yuv_get_ret(int fn);
void mock_yuv_set_alloc(dma_id_t id);
void mock_yuv_set_transfer_len_max(uint32_t max_len);
void mock_yuv_set_remain_len(uint32_t remain);
void mock_yuv_set_fifo_addr(uint32_t addr);
void mock_yuv_set_encode_words(uint32_t words);
void mock_yuv_set_sys_enable(int32_t ret);
void mock_yuv_set_root_ret(int ret);
void mock_yuv_clock_set(long ticks);

/* Event-loop drive: run before the driver's nxsem_tickwait on an empty
 * semaphore; may fire ISRs to post events deterministically. */
void mock_yuv_set_tickwait_hook(void (*hook)(void));

/* Call log -------------------------------------------------------------- */

unsigned int mock_yuv_call_count(void);
int mock_yuv_calls_of(int fn);
const struct mock_yuv_call_s *mock_yuv_call(unsigned int index);

/* Captured state -------------------------------------------------------- */

const dma_config_t *mock_yuv_dma_config(void);
bool mock_yuv_dma_config_valid(void);
uint32_t mock_yuv_dest_start(void);
uint32_t mock_yuv_dest_end(void);
uint32_t mock_yuv_transfer_len(void);
uint32_t mock_yuv_src_burst(void);
uint32_t mock_yuv_dest_burst(void);
uint32_t mock_yuv_src_sec(void);
uint32_t mock_yuv_dest_sec(void);
uint32_t mock_yuv_em_base(void);
uint32_t mock_yuv_fifo_addr(void);
uint32_t mock_yuv_encode_words(void);
const yuv_buf_config_t *mock_yuv_yuv_config(void);
bool mock_yuv_yuv_config_valid(void);

/* ISR capture and injection --------------------------------------------- */

void mock_yuv_fire_h264_line(void);
void mock_yuv_fire_h264_final(void);
void mock_yuv_fire_yuv_error(void);
void mock_yuv_fire_dma_finish(void);

#endif /* __MOCK_SDK_YUV_H264_H */
