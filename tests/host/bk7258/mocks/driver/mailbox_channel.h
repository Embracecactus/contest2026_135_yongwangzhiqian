/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/driver/mailbox_channel.h
 *
 * Host shim mirroring the Beken SDK logical-mailbox ABI consumed by
 * bk7258_rptun_mbox.c.  The layout of mb_chnl_cmd_t is reproduced exactly
 * (4-byte hdr + param1..3 = 16 bytes) so the file's static_assert holds.
 *
 * The transport is implemented in mock_sdk.c and is *controllable* from the
 * test: it can be forced to return BK_ERR_BUSY (the -EAGAIN retry path) and
 * can be told whether to fire the TX-complete callback (simulating a lost
 * mailbox completion interrupt).
 ****************************************************************************/

#ifndef __MOCK_DRIVER_MAILBOX_CHANNEL_H
#define __MOCK_DRIVER_MAILBOX_CHANNEL_H

#include <stdint.h>
#include <common/bk_err.h>

/* The 4-byte command header (matches the SDK bitfield union). */
typedef union
{
  struct
  {
    uint32_t cmd : 8;
    uint32_t state : 4;
    uint32_t reserved : 20;
  };
  uint32_t data;
} mb_chnl_hdr_t;

/* Exactly 16 bytes: this is what the implementation's static_assert checks. */
typedef struct
{
  mb_chnl_hdr_t hdr;
  uint32_t param1;
  uint32_t param2;
  uint32_t param3;
} mb_chnl_cmd_t;

typedef struct
{
  mb_chnl_hdr_t hdr;
  uint32_t ack_data1;
  uint32_t ack_data2;
  uint32_t ack_state;
} mb_chnl_ack_t;

typedef void (*chnl_rx_isr_t)(void *param, mb_chnl_cmd_t *cmd_buf);
typedef void (*chnl_tx_cmpl_isr_t)(void *param, mb_chnl_ack_t *ack_buf);

enum mb_chnl_ctrl_cmd_e
{
  MB_CHNL_GET_STATUS = 0,
  MB_CHNL_SET_RX_ISR,
  MB_CHNL_SET_TX_ISR,
  MB_CHNL_SET_TX_CMPL_ISR
};

#define ACK_STATE_PENDING   0x01
#define ACK_STATE_COMPLETE  0x02
#define ACK_STATE_FAIL      0x03

/* The production wrapper uses the SDK PWC channel for coordinated-PM wake
 * and the LOG channel for RPTUN traffic.  Preserve their logical indices. */

#define MB_CHNL_PWC         2
#define MB_CHNL_LOG         14
#define GET_LOG_CHNL_ID(log_chnl)  ((log_chnl) & 0xF)

bk_err_t mb_chnl_init(void);
bk_err_t mb_chnl_open(uint8_t log_chnl, void *param);
bk_err_t mb_chnl_close(uint8_t log_chnl);
bk_err_t mb_chnl_ctrl(uint8_t log_chnl, int cmd, void *param);
bk_err_t mb_chnl_write(uint8_t log_chnl, mb_chnl_cmd_t *cmd_buf);

#endif /* __MOCK_DRIVER_MAILBOX_CHANNEL_H */
