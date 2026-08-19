/****************************************************************************
 * tests/bk7258/mocks/driver/hal/hal_can_types.h
 *
 * Host mirror of the immutable v3.1.1.9
 * ap/include/driver/hal/hal_can_types.h, restricted to the types the
 * bk7258_can.c lower-half actually consumes.  Values/members match the SDK
 * header verbatim so driver behavior is preserved.
 ****************************************************************************/

#ifndef __MOCK_DRIVER_HAL_CAN_TYPES_H
#define __MOCK_DRIVER_HAL_CAN_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>

#define CAN_20_MAX_PAYLOAD  8
#define CAN_FD_MAX_PAYLOAD  64

#define FDF_CAN_20  0
#define FDF_CAN_FD  1

typedef enum
{
  CAN_CHAN_0 = 0,
  CAN_CHAN_MAX,
} can_channel_t;

typedef enum
{
  CAN_BR_125K,
  CAN_BR_250K,
  CAN_BR_500K,
  CAN_BR_800K,
  CAN_BR_1M,
  CAN_BR_2M,
  CAN_BR_4M,
  CAN_BR_5M,
} can_bit_rate_e;

typedef enum
{
  CMD_CAN_MODUILE_INIT,
  CMD_CAN_SET_RX_CALLBACK,
  CMD_CAN_SET_TX_CALLBACK,
  CMD_CAN_RESET_REQ,
  CMD_CAN_ACC_FILTER_SET,
  CMD_CAN_SET_TX_FIFO,
  CMD_CAN_GET_TX_SIZE,
  CMD_CAN_SET_RX_FIFO,
  CMD_CAN_FD_BOSCH_MODE,
  CMD_CAN_GET_RX_FRAME_TAG,
  CMD_CAN_SET_TX_FRAME_TAG,
  CMD_CAN_PTB_INBUF,
  CMD_CAN_TRANS_SWITCH,
  CMD_CAN_STB_INBUF,
  CMD_CAN_SET_ERR_CALLBACK,
  CMD_CAN_BUSOFF_CLR,
  CMD_CAN_GET_KOER,
} can_ctrl_cmd_e;

typedef struct
{
  uint32_t id;
  uint8_t ide;
  uint8_t rtr;
  uint8_t fdf;
  uint8_t brs;
  uint8_t esi;
  uint8_t ttsen;
} can_frame_tag_t;

typedef struct
{
  can_frame_tag_t tag;
  uint32_t size;
  uint8_t *data;
} can_frame_s;

typedef void (*can_callback)(void *param);

typedef struct
{
  can_callback cb;
  void *param;
} can_callback_des_t;

typedef struct
{
  can_callback_des_t tx_cb;
  can_callback_des_t rx_cb;
  can_callback_des_t err_cb;
} can_hal_t;

#endif /* __MOCK_DRIVER_HAL_CAN_TYPES_H */
