/****************************************************************************
 * tests/bk7258/framework/mock_sdk_yuv_h264.c
 *
 * SDK stand-in implementation (see mock_sdk_yuv_h264.h).
 ****************************************************************************/

#include "mock_sdk_yuv_h264.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <common/bk_err.h>
#include <nuttx/semaphore.h>

static int g_mock_yuv_ret[MOCK_YUV_FN_MAX];
static struct mock_yuv_call_s g_mock_yuv_calls[MOCK_YUV_LOG_MAX];
static unsigned int g_mock_yuv_call_count;

static dma_id_t g_mock_yuv_alloc = DMA_ID_3;
static uint32_t g_mock_yuv_max_len = 0x10000u;
static uint32_t g_mock_yuv_remain = 1024u;
static uint32_t g_mock_yuv_fifo = 0x00c00000u;
static uint32_t g_mock_yuv_encode_words;
static int32_t g_mock_yuv_sys_enable;
static int g_mock_yuv_root_ret;
static long g_mock_yuv_clock;

static dma_config_t g_mock_yuv_dma_config;
static bool g_mock_yuv_dma_config_valid;
static uint32_t g_mock_yuv_dest_start;
static uint32_t g_mock_yuv_dest_end;
static uint32_t g_mock_yuv_transfer_len;
static uint32_t g_mock_yuv_src_burst;
static uint32_t g_mock_yuv_dest_burst;
static uint32_t g_mock_yuv_src_sec;
static uint32_t g_mock_yuv_dest_sec;
static uint32_t g_mock_yuv_em_base;
static yuv_buf_config_t g_mock_yuv_yuv_config;
static bool g_mock_yuv_yuv_config_valid;

static h264_isr_t g_mock_yuv_h264_isr[H264_ISR_MAX];
static void *g_mock_yuv_h264_isr_param[H264_ISR_MAX];
static yuv_buf_isr_t g_mock_yuv_yuv_err_isr;
static void *g_mock_yuv_yuv_err_param;
static dma_isr_t g_mock_yuv_dma_finish_isr;

void (*g_mock_yuv_tickwait_hook)(void);

/* ---------------------------------------------------------------------- */
/* Logging                                                                */
/* ---------------------------------------------------------------------- */

static void mock_yuv_log(int fn, uint32_t a0, uint32_t a1, uint32_t a2)
{
  if (g_mock_yuv_call_count < MOCK_YUV_LOG_MAX)
    {
      g_mock_yuv_calls[g_mock_yuv_call_count].fn = fn;
      g_mock_yuv_calls[g_mock_yuv_call_count].a0 = a0;
      g_mock_yuv_calls[g_mock_yuv_call_count].a1 = a1;
      g_mock_yuv_calls[g_mock_yuv_call_count].a2 = a2;
    }

  g_mock_yuv_call_count++;
}

void mock_yuv_sdk_reset(void)
{
  int i;

  memset(g_mock_yuv_ret, 0, sizeof(g_mock_yuv_ret));
  g_mock_yuv_call_count = 0;
  g_mock_yuv_alloc = DMA_ID_3;
  g_mock_yuv_max_len = 0x10000u;
  g_mock_yuv_remain = 1024u;
  g_mock_yuv_fifo = 0x00c00000u;
  g_mock_yuv_encode_words = 0;
  g_mock_yuv_sys_enable = 0;
  g_mock_yuv_root_ret = 0;
  g_mock_yuv_clock = 0;
  g_mock_yuv_dma_config_valid = false;
  g_mock_yuv_dest_start = 0;
  g_mock_yuv_dest_end = 0;
  g_mock_yuv_transfer_len = 0;
  g_mock_yuv_src_burst = 0;
  g_mock_yuv_dest_burst = 0;
  g_mock_yuv_src_sec = 0;
  g_mock_yuv_dest_sec = 0;
  g_mock_yuv_em_base = 0;
  g_mock_yuv_yuv_config_valid = false;
  g_mock_yuv_tickwait_hook = NULL;
  for (i = 0; i < H264_ISR_MAX; i++)
    {
      g_mock_yuv_h264_isr[i] = NULL;
      g_mock_yuv_h264_isr_param[i] = NULL;
    }

  g_mock_yuv_yuv_err_isr = NULL;
  g_mock_yuv_yuv_err_param = NULL;
  g_mock_yuv_dma_finish_isr = NULL;
}

void mock_yuv_set_ret(int fn, int ret)
{
  g_mock_yuv_ret[fn] = ret;
}

int mock_yuv_get_ret(int fn)
{
  return g_mock_yuv_ret[fn];
}

void mock_yuv_set_alloc(dma_id_t id)
{
  g_mock_yuv_alloc = id;
}

void mock_yuv_set_transfer_len_max(uint32_t max_len)
{
  g_mock_yuv_max_len = max_len;
}

void mock_yuv_set_remain_len(uint32_t remain)
{
  g_mock_yuv_remain = remain;
}

void mock_yuv_set_fifo_addr(uint32_t addr)
{
  g_mock_yuv_fifo = addr;
}

void mock_yuv_set_encode_words(uint32_t words)
{
  g_mock_yuv_encode_words = words;
}

void mock_yuv_set_sys_enable(int32_t ret)
{
  g_mock_yuv_sys_enable = ret;
}

void mock_yuv_set_root_ret(int ret)
{
  g_mock_yuv_root_ret = ret;
}

void mock_yuv_clock_set(long ticks)
{
  g_mock_yuv_clock = ticks;
}

void mock_yuv_set_tickwait_hook(void (*hook)(void))
{
  g_mock_yuv_tickwait_hook = hook;
}

unsigned int mock_yuv_call_count(void)
{
  return g_mock_yuv_call_count;
}

int mock_yuv_calls_of(int fn)
{
  int count = 0;
  unsigned int i;

  for (i = 0; i < g_mock_yuv_call_count; i++)
    {
      if (g_mock_yuv_calls[i].fn == fn)
        {
          count++;
        }
    }

  return count;
}

const struct mock_yuv_call_s *mock_yuv_call(unsigned int index)
{
  return &g_mock_yuv_calls[index];
}

const dma_config_t *mock_yuv_dma_config(void)
{
  return &g_mock_yuv_dma_config;
}

bool mock_yuv_dma_config_valid(void)
{
  return g_mock_yuv_dma_config_valid;
}

uint32_t mock_yuv_dest_start(void)
{
  return g_mock_yuv_dest_start;
}

uint32_t mock_yuv_dest_end(void)
{
  return g_mock_yuv_dest_end;
}

uint32_t mock_yuv_transfer_len(void)
{
  return g_mock_yuv_transfer_len;
}

uint32_t mock_yuv_src_burst(void)
{
  return g_mock_yuv_src_burst;
}

uint32_t mock_yuv_dest_burst(void)
{
  return g_mock_yuv_dest_burst;
}

uint32_t mock_yuv_src_sec(void)
{
  return g_mock_yuv_src_sec;
}

uint32_t mock_yuv_dest_sec(void)
{
  return g_mock_yuv_dest_sec;
}

uint32_t mock_yuv_em_base(void)
{
  return g_mock_yuv_em_base;
}

uint32_t mock_yuv_fifo_addr(void)
{
  return g_mock_yuv_fifo;
}

uint32_t mock_yuv_encode_words(void)
{
  return g_mock_yuv_encode_words;
}

const yuv_buf_config_t *mock_yuv_yuv_config(void)
{
  return &g_mock_yuv_yuv_config;
}

bool mock_yuv_yuv_config_valid(void)
{
  return g_mock_yuv_yuv_config_valid;
}

void mock_yuv_fire_h264_line(void)
{
  if (g_mock_yuv_h264_isr[H264_LINE_DONE] != NULL)
    {
      g_mock_yuv_h264_isr[H264_LINE_DONE](0, g_mock_yuv_h264_isr_param[H264_LINE_DONE]);
    }
}

void mock_yuv_fire_h264_final(void)
{
  if (g_mock_yuv_h264_isr[H264_FINAL_OUT] != NULL)
    {
      g_mock_yuv_h264_isr[H264_FINAL_OUT](0, g_mock_yuv_h264_isr_param[H264_FINAL_OUT]);
    }
}

void mock_yuv_fire_yuv_error(void)
{
  if (g_mock_yuv_yuv_err_isr != NULL)
    {
      g_mock_yuv_yuv_err_isr(0, g_mock_yuv_yuv_err_param);
    }
}

void mock_yuv_fire_dma_finish(void)
{
  if (g_mock_yuv_dma_finish_isr != NULL)
    {
      g_mock_yuv_dma_finish_isr(DMA_ID_3);
    }
}

/* ---------------------------------------------------------------------- */
/* Clock and NuttX semaphore shims (with deterministic tickwait hook)     */
/* ---------------------------------------------------------------------- */

long clock_systime_ticks(void)
{
  return g_mock_yuv_clock;
}

int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{
  (void)pshared;
  sem->count = (int)value;
  return 0;
}

int nxsem_destroy(sem_t *sem)
{
  (void)sem;
  return 0;
}

int nxsem_post(sem_t *sem)
{
  if (sem->count < INT_MAX)
    {
      sem->count++;
    }

  return 0;
}

int nxsem_trywait(sem_t *sem)
{
  if (sem->count > 0)
    {
      sem->count--;
      return 0;
    }

  return -EAGAIN;
}

int nxsem_tickwait_uninterruptible(sem_t *sem, int ticks)
{
  if (sem->count > 0)
    {
      sem->count--;
      return 0;
    }

  if (g_mock_yuv_tickwait_hook != NULL)
    {
      g_mock_yuv_tickwait_hook();
    }

  if (sem->count > 0)
    {
      sem->count--;
      return 0;
    }

  /* Nothing posted within this slice: model one elapsed tick and report a
   * spurious wakeup so the driver re-checks its own deadline rather than
   * treating this as a hard timeout. */

  g_mock_yuv_clock++;
  return 0;
}

/* ---------------------------------------------------------------------- */
/* Board media-root helper                                                */
/* ---------------------------------------------------------------------- */

int bk7258_media_root_initialize(uint32_t roots)
{
  mock_yuv_log(MOCK_YUV_FN_MEDIA_ROOT_INIT, roots, 0, 0);
  return g_mock_yuv_root_ret;
}

/* ---------------------------------------------------------------------- */
/* CPU1 interrupt-route registers (group 2, H.264 bit 14)                 */
/* ---------------------------------------------------------------------- */

int32_t sys_drv_core_intr_group2_disable(uint32_t core_id, uint32_t param)
{
  mock_yuv_log(MOCK_YUV_FN_SYS_GROUP2_DISABLE, core_id, param, 0);
  return 0;
}

int32_t sys_drv_core_intr_group2_enable(uint32_t core_id, uint32_t param)
{
  mock_yuv_log(MOCK_YUV_FN_SYS_GROUP2_ENABLE, core_id, param, 0);
  return g_mock_yuv_sys_enable;
}

/* ---------------------------------------------------------------------- */
/* DMA driver                                                             */
/* ---------------------------------------------------------------------- */

bk_err_t bk_dma_driver_init(void)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_DRIVER_INIT, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_DRIVER_INIT];
}

dma_id_t bk_dma_alloc(uint16_t user_id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_ALLOC, user_id, 0, 0);
  return g_mock_yuv_alloc;
}

bk_err_t bk_dma_free(uint16_t user_id, dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_FREE, user_id, id, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_FREE];
}

bk_err_t bk_dma_init(dma_id_t id, const dma_config_t *config)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_INIT, id, 0, 0);
  if (config != NULL)
    {
      g_mock_yuv_dma_config = *config;
      g_mock_yuv_dma_config_valid = true;
      g_mock_yuv_dest_start = config->dst.start_addr;
      g_mock_yuv_dest_end = config->dst.end_addr;
    }

  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_INIT];
}

bk_err_t bk_dma_deinit(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_DEINIT, id, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_DEINIT];
}

bk_err_t bk_dma_start(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_START, id, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_START];
}

bk_err_t bk_dma_stop(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_STOP, id, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_STOP];
}

bk_err_t bk_dma_flush_src_buffer(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_FLUSH_SRC, id, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_FLUSH_SRC];
}

bk_err_t bk_dma_register_isr(dma_id_t id, dma_isr_t half_finish_isr,
                             dma_isr_t finish_isr)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_REGISTER_ISR, id, 0, 0);
  g_mock_yuv_dma_finish_isr = finish_isr;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_REGISTER_ISR];
}

bk_err_t bk_dma_set_dest_addr(dma_id_t id, uint32_t start_addr,
                              uint32_t end_addr)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_SET_DEST_ADDR, start_addr, end_addr, 0);
  g_mock_yuv_dest_start = start_addr;
  g_mock_yuv_dest_end = end_addr;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_SET_DEST_ADDR];
}

bk_err_t bk_dma_set_transfer_len(dma_id_t id, uint32_t tran_len)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_SET_TRANSFER_LEN, tran_len, 0, 0);
  g_mock_yuv_transfer_len = tran_len;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_SET_TRANSFER_LEN];
}

bk_err_t bk_dma_set_src_burst_len(dma_id_t id, dma_burst_len_t len)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_SET_SRC_BURST, len, 0, 0);
  g_mock_yuv_src_burst = len;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_SET_SRC_BURST];
}

bk_err_t bk_dma_set_dest_burst_len(dma_id_t id, dma_burst_len_t len)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_SET_DEST_BURST, len, 0, 0);
  g_mock_yuv_dest_burst = len;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_SET_DEST_BURST];
}

bk_err_t bk_dma_set_src_sec_attr(dma_id_t id, dma_sec_attr_t attr)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_SET_SRC_SEC, attr, 0, 0);
  g_mock_yuv_src_sec = attr;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_SET_SRC_SEC];
}

bk_err_t bk_dma_set_dest_sec_attr(dma_id_t id, dma_sec_attr_t attr)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_SET_DEST_SEC, attr, 0, 0);
  g_mock_yuv_dest_sec = attr;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_SET_DEST_SEC];
}

bk_err_t bk_dma_enable_finish_interrupt(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_ENABLE_FINISH, id, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_ENABLE_FINISH];
}

bk_err_t bk_dma_disable_finish_interrupt(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_DISABLE_FINISH, id, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_DMA_DISABLE_FINISH];
}

uint32_t bk_dma_get_transfer_len_max(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_GET_MAX_LEN, id, 0, 0);
  return g_mock_yuv_max_len;
}

uint32_t bk_dma_get_remain_len(dma_id_t id)
{
  mock_yuv_log(MOCK_YUV_FN_DMA_GET_REMAIN, id, 0, 0);
  return g_mock_yuv_remain;
}

/* ---------------------------------------------------------------------- */
/* H.264 driver                                                           */
/* ---------------------------------------------------------------------- */

bk_err_t bk_h264_driver_init(void)
{
  mock_yuv_log(MOCK_YUV_FN_H264_DRIVER_INIT, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_DRIVER_INIT];
}

bk_err_t bk_h264_init(uint16_t width, uint16_t height)
{
  mock_yuv_log(MOCK_YUV_FN_H264_INIT, width, height, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_INIT];
}

bk_err_t bk_h264_deinit(void)
{
  mock_yuv_log(MOCK_YUV_FN_H264_DEINIT, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_DEINIT];
}

bk_err_t bk_h264_encode_enable(void)
{
  mock_yuv_log(MOCK_YUV_FN_H264_ENCODE_ENABLE, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_ENCODE_ENABLE];
}

bk_err_t bk_h264_encode_disable(void)
{
  mock_yuv_log(MOCK_YUV_FN_H264_ENCODE_DISABLE, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_ENCODE_DISABLE];
}

bk_err_t bk_h264_soft_reset(void)
{
  mock_yuv_log(MOCK_YUV_FN_H264_SOFT_RESET, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_SOFT_RESET];
}

bk_err_t bk_h264_get_fifo_addr(uint32_t *fifo_addr)
{
  mock_yuv_log(MOCK_YUV_FN_H264_GET_FIFO, 0, 0, 0);
  if (fifo_addr != NULL)
    {
      *fifo_addr = g_mock_yuv_fifo;
    }

  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_GET_FIFO];
}

bk_err_t bk_h264_register_isr(h264_isr_type_t type_id, h264_isr_t isr,
                              void *param)
{
  mock_yuv_log(MOCK_YUV_FN_H264_REGISTER_ISR, type_id, 0, 0);
  if (type_id < H264_ISR_MAX)
    {
      g_mock_yuv_h264_isr[type_id] = isr;
      g_mock_yuv_h264_isr_param[type_id] = param;
    }

  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_REGISTER_ISR];
}

bk_err_t bk_h264_unregister_isr(h264_isr_type_t type_id)
{
  mock_yuv_log(MOCK_YUV_FN_H264_UNREGISTER_ISR, type_id, 0, 0);
  if (type_id < H264_ISR_MAX)
    {
      g_mock_yuv_h264_isr[type_id] = NULL;
      g_mock_yuv_h264_isr_param[type_id] = NULL;
    }

  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_H264_UNREGISTER_ISR];
}

uint32_t bk_h264_get_encode_count(void)
{
  mock_yuv_log(MOCK_YUV_FN_H264_GET_COUNT, 0, 0, 0);
  return g_mock_yuv_encode_words;
}

/* ---------------------------------------------------------------------- */
/* YUV-buffer driver                                                      */
/* ---------------------------------------------------------------------- */

bk_err_t bk_yuv_buf_driver_init(void)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_DRIVER_INIT, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_DRIVER_INIT];
}

bk_err_t bk_yuv_buf_init(const yuv_buf_config_t *config)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_INIT, 0, 0, 0);
  if (config != NULL)
    {
      g_mock_yuv_yuv_config = *config;
      g_mock_yuv_yuv_config_valid = true;
    }

  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_INIT];
}

bk_err_t bk_yuv_buf_deinit(void)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_DEINIT, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_DEINIT];
}

bk_err_t bk_yuv_buf_enable_nosensor_encode_mode(void)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_NOSENSOR, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_NOSENSOR];
}

bk_err_t bk_yuv_buf_set_em_base_addr(uint32_t em_base_addr)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_SET_EM_BASE, em_base_addr, 0, 0);
  g_mock_yuv_em_base = em_base_addr;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_SET_EM_BASE];
}

bk_err_t bk_yuv_buf_start(yuv_mode_t work_mode)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_START, work_mode, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_START];
}

bk_err_t bk_yuv_buf_stop(yuv_mode_t work_mode)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_STOP, work_mode, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_STOP];
}

bk_err_t bk_yuv_buf_rencode_start(void)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_RENCODE_START, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_RENCODE_START];
}

bk_err_t bk_yuv_buf_register_isr(yuv_buf_isr_type_t type_id,
                                 yuv_buf_isr_t isr, void *param)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_REGISTER_ISR, type_id, 0, 0);
  g_mock_yuv_yuv_err_isr = isr;
  g_mock_yuv_yuv_err_param = param;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_REGISTER_ISR];
}

bk_err_t bk_yuv_buf_unregister_isr(yuv_buf_isr_type_t type_id)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_UNREGISTER_ISR, type_id, 0, 0);
  g_mock_yuv_yuv_err_isr = NULL;
  g_mock_yuv_yuv_err_param = NULL;
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_UNREGISTER_ISR];
}

bk_err_t bk_yuv_buf_soft_reset(void)
{
  mock_yuv_log(MOCK_YUV_FN_YUV_SOFT_RESET, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_YUV_SOFT_RESET];
}

/* ---------------------------------------------------------------------- */
/* JPEG encoder driver-init root (used only by the media-root mock)      */
/* ---------------------------------------------------------------------- */

bk_err_t bk_jpeg_enc_driver_init(void)
{
  mock_yuv_log(MOCK_YUV_FN_JPEG_DRIVER_INIT, 0, 0, 0);
  return (bk_err_t)g_mock_yuv_ret[MOCK_YUV_FN_JPEG_DRIVER_INIT];
}