/****************************************************************************
 * tests/mocks/driver/h264.h
 *
 * Host mirror of the v3.1.1.9 SDK <driver/h264.h> ABI (types from
 * h264_types.h).  Only the surface used by the board helpers is provided.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_H264_H
#define __MOCK_DRIVER_H264_H

#include <stdint.h>

#include <common/bk_err.h>

#define BK_ERR_H264_BASE                (-0x4300)
#define BK_ERR_H264_DRIVER_NOT_INIT     (BK_ERR_H264_BASE - 1)
#define BK_ERR_H264_ISR_INVALID_ID      (BK_ERR_H264_BASE - 2)
#define BK_ERR_H264_INVALID_RESOLUTION_TYPE (BK_ERR_H264_BASE - 3)
#define BK_ERR_H264_INVALID_PFRAME_NUMBER   (BK_ERR_H264_BASE - 4)
#define BK_ERR_H264_INVALID_QP          (BK_ERR_H264_BASE - 5)
#define BK_ERR_H264_INVALID_IMB_BITS    (BK_ERR_H264_BASE - 6)
#define BK_ERR_H264_INVALID_PMB_BITS    (BK_ERR_H264_BASE - 7)
#define BK_ERR_H264_INVALID_CONFIG_PARAM (BK_ERR_H264_BASE - 8)
#define BK_ERR_H264_INVALID_PIXEL_HEIGHT (BK_ERR_H264_BASE - 9)

typedef uint8_t h264_unit_t;
typedef void (*h264_isr_t)(h264_unit_t id, void *param);

typedef enum
{
  H264_SKIP_FRAME = 0,
  H264_FINAL_OUT,
  H264_LINE_DONE,
  H264_ISR_MAX,
} h264_isr_type_t;

bk_err_t bk_h264_driver_init(void);
bk_err_t bk_h264_driver_deinit(void);
bk_err_t bk_h264_init(uint16_t width, uint16_t height);
bk_err_t bk_h264_deinit(void);
bk_err_t bk_h264_encode_enable(void);
bk_err_t bk_h264_encode_disable(void);
bk_err_t bk_h264_soft_reset(void);
bk_err_t bk_h264_get_fifo_addr(uint32_t *fifo_addr);
bk_err_t bk_h264_register_isr(h264_isr_type_t type_id, h264_isr_t isr,
                              void *param);
bk_err_t bk_h264_unregister_isr(h264_isr_type_t type_id);
uint32_t bk_h264_get_encode_count(void);

#endif /* __MOCK_DRIVER_H264_H */