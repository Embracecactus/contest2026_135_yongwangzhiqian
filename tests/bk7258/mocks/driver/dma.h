/****************************************************************************
 * tests/mocks/driver/dma.h
 *
 * Host mirror of the v3.1.1.9 SDK <driver/dma.h> ABI (types from
 * dma_types.h / hal_dma_types.h).  Only the surface used by the board
 * helpers is provided; the enumerants keep their real ordinal values.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_DMA_H
#define __MOCK_DRIVER_DMA_H

#include <stdint.h>

#include <common/bk_err.h>

#define BK_ERR_DMA_BASE          (-0x2100)
#define BK_ERR_DMA_ID            (BK_ERR_DMA_BASE - 1)
#define BK_ERR_DMA_NOT_INIT      (BK_ERR_DMA_BASE - 2)
#define BK_ERR_DMA_ID_NOT_INIT   (BK_ERR_DMA_BASE - 3)
#define BK_ERR_DMA_ID_NOT_START  (BK_ERR_DMA_BASE - 4)
#define BK_ERR_DMA_INVALID_ADDR  (BK_ERR_DMA_BASE - 5)
#define BK_ERR_DMA_ID_REINIT     (BK_ERR_DMA_BASE - 6)
#define BK_ERR_DMA_TRANS_LEN     (BK_ERR_DMA_BASE - 7)

typedef enum
{
  DMA_ID_0 = 0,
  DMA_ID_1,
  DMA_ID_2,
  DMA_ID_3,
  DMA_ID_4,
  DMA_ID_5,
  DMA_ID_6,
  DMA_ID_7,
  DMA_ID_8,
  DMA_ID_9,
  DMA_ID_10,
  DMA_ID_11,
  DMA_ID_12,
  DMA_ID_13,
  DMA_ID_14,
  DMA_ID_15,
  DMA_ID_MAX,
} dma_id_t;

typedef enum
{
  DMA_DEV_DTCM = 0,
  DMA_DEV_LA,
  DMA_DEV_HSSPI,
  DMA_DEV_AUDIO,
  DMA_DEV_AUDIO_RX,
  DMA_DEV_SDIO,
  DMA_DEV_SDIO_RX,
  DMA_DEV_UART1,
  DMA_DEV_UART1_RX,
  DMA_DEV_UART2,
  DMA_DEV_UART2_RX,
  DMA_DEV_UART3,
  DMA_DEV_UART3_RX,
  DMA_DEV_I2S,
  DMA_DEV_I2S_CH1,
  DMA_DEV_I2S_CH2,
  DMA_DEV_I2S_RX,
  DMA_DEV_I2S_RX_CH1,
  DMA_DEV_I2S_RX_CH2,
  DMA_DEV_I2S1,
  DMA_DEV_I2S1_RX,
  DMA_DEV_I2S2,
  DMA_DEV_I2S2_RX,
  DMA_DEV_GSPI0,
  DMA_DEV_GSPI0_RX,
  DMA_DEV_GSPI1,
  DMA_DEV_GSPI1_RX,
  DMA_DEV_GSPI2,
  DMA_DEV_GSPI2_RX,
  DMA_DEV_JPEG,
  DMA_DEV_PSRAM_VIDEO,
  DMA_DEV_PSRAM_AUDIO,
  DMA_DEV_USB,
  DMA_DEV_LCD_CMD,
  DMA_DEV_LCD_DATA,
  DMA_DEV_DISP_RX,
  DMA_DEV_SDMADC_RX,
  DMA_DEV_AHB_MEM,
  DMA_DEV_SPI0,
  DMA_DEV_SPI1,
  DMA_DEV_H264,
  DMA_DEV_AUD_DMIC,
  DMA_DEV_MAX,
} dma_dev_t;

typedef enum
{
  DMA_DATA_WIDTH_8BITS = 0,
  DMA_DATA_WIDTH_16BITS,
  DMA_DATA_WIDTH_32BITS,
} dma_data_width_t;

typedef enum
{
  DMA_WORK_MODE_SINGLE = 0,
  DMA_WORK_MODE_REPEAT,
} dma_work_mode_t;

typedef enum
{
  DMA_ADDR_INC_DISABLE = 0,
  DMA_ADDR_INC_ENABLE,
} dma_addr_inc_t;

typedef enum
{
  DMA_ADDR_LOOP_DISABLE = 0,
  DMA_ADDR_LOOP_ENABLE,
} dma_addr_loop_t;

typedef enum
{
  DMA_ATTR_NON_SEC = 0,
  DMA_ATTR_SEC,
} dma_sec_attr_t;

typedef enum
{
  BURST_LEN_SINGLE = 0,
  BURST_LEN_INC4,
  BURST_LEN_INC8,
  BURST_LEN_INC16,
} dma_burst_len_t;

typedef uint32_t dma_pixel_trans_type_t;
typedef uint8_t dma_chan_priority_t;

typedef struct
{
  dma_dev_t dev;
  dma_data_width_t width;
  dma_addr_inc_t addr_inc_en;
  dma_addr_loop_t addr_loop_en;
  uint32_t start_addr;
  uint32_t end_addr;
} dma_port_config_t;

typedef struct
{
  dma_work_mode_t mode;
  dma_chan_priority_t chan_prio;
  dma_port_config_t src;
  dma_port_config_t dst;
  dma_pixel_trans_type_t trans_type;
  uint32_t dest_wr_intlv;
  uint32_t src_rd_intlv;
} dma_config_t;

typedef void (*dma_isr_t)(dma_id_t dma_id);

bk_err_t bk_dma_driver_init(void);
bk_err_t bk_dma_driver_deinit(void);
dma_id_t bk_dma_alloc(uint16_t user_id);
bk_err_t bk_dma_free(uint16_t user_id, dma_id_t id);
bk_err_t bk_dma_init(dma_id_t id, const dma_config_t *config);
bk_err_t bk_dma_deinit(dma_id_t id);
bk_err_t bk_dma_start(dma_id_t id);
bk_err_t bk_dma_stop(dma_id_t id);
uint32_t bk_dma_get_transfer_len_max(dma_id_t id);
bk_err_t bk_dma_set_transfer_len(dma_id_t id, uint32_t tran_len);
bk_err_t bk_dma_set_dest_addr(dma_id_t id, uint32_t start_addr,
                              uint32_t end_addr);
bk_err_t bk_dma_set_src_burst_len(dma_id_t id, dma_burst_len_t len);
bk_err_t bk_dma_set_dest_burst_len(dma_id_t id, dma_burst_len_t len);
bk_err_t bk_dma_set_src_sec_attr(dma_id_t id, dma_sec_attr_t attr);
bk_err_t bk_dma_set_dest_sec_attr(dma_id_t id, dma_sec_attr_t attr);
bk_err_t bk_dma_enable_finish_interrupt(dma_id_t id);
bk_err_t bk_dma_disable_finish_interrupt(dma_id_t id);
bk_err_t bk_dma_register_isr(dma_id_t id, dma_isr_t half_finish_isr,
                              dma_isr_t finish_isr);
uint32_t bk_dma_get_remain_len(dma_id_t id);
bk_err_t bk_dma_flush_src_buffer(dma_id_t id);

#endif /* __MOCK_DRIVER_DMA_H */