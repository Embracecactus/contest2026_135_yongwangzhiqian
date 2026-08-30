/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/nuttx/can/can.h
 *
 * Host shim for the NuttX CAN upper-half ABI consumed by bk7258_can.c.
 *
 * The upper-half itself is not linked into the test binary: can_receive()
 * and can_txdone() are recorded by the suite mock instead.  This header
 * only mirrors the device/ops/hdr/msg structures and ioctl argument
 * structs the lower-half touches, with the same CONFIG_CAN_* gating as the
 * real nuttx/include/nuttx/can/can.h (only the ERROR/TIMESTAMP gates are
 * enabled by mocks/nuttx/config.h; EXTID and FD stay undefined).
 ****************************************************************************/

#ifndef __MOCK_NUTTX_CAN_H
#define __MOCK_NUTTX_CAN_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include <nuttx/config.h>

#ifndef FAR
#define FAR
#endif

#define CAN_MAX_DLEN   8
#define CAN_SFF_MASK   0x07ffu

/* Error frames (CONFIG_CAN_ERRORS) -------------------------------------- */

#define CAN_ERROR_CONTROLLER (0x10000000u)
#define CAN_ERROR_DLC        8
#define CAN_ERR_DLC          CAN_ERROR_DLC
#define CAN_ERROR1_UNSPEC    (1u << 0)

/* ioctl commands (plain distinct ids; only the lower-half switch uses
 * them) */

#define CANIOC_GET_CONNMODES    1
#define CANIOC_SET_CONNMODES    2
#define CANIOC_GET_BITTIMING    3
#define CANIOC_SET_BITTIMING    4
#define CANIOC_BUSOFF_RECOVERY  5
#define CANIOC_ADD_EXTFILTER    6
#define CANIOC_DEL_EXTFILTER    7
#define CANIOC_ADD_STDFILTER    8
#define CANIOC_DEL_STDFILTER    9

struct can_timestamp_s
{
  uint32_t tv_sec;
  uint32_t tv_usec;
};

struct can_hdr_s
{
  uint32_t ch_id;
  uint8_t ch_dlc;
  uint8_t ch_rtr;
#ifdef CONFIG_CAN_ERRORS
  uint8_t ch_error;
#endif
#ifdef CONFIG_CAN_EXTID
  uint8_t ch_extid;
#endif
#ifdef CONFIG_CAN_FD
  uint8_t ch_edl;
  uint8_t ch_brs;
  uint8_t ch_esi;
#endif
#ifdef CONFIG_CAN_TIMESTAMP
  struct can_timestamp_s ch_ts;
#endif
};

struct can_msg_s
{
  struct can_hdr_s cm_hdr;
  uint8_t cm_data[CAN_MAX_DLEN];
};

struct can_dev_s;

struct canioc_connmodes_s
{
  uint8_t bm_loopback;
  uint8_t bm_silent;
  uint8_t bm_extdata;
};

struct canioc_bittiming_s
{
  uint32_t bt_baud;
  uint8_t bt_tseg1;
  uint8_t bt_tseg2;
  uint8_t bt_sjw;
};

struct can_ops_s
{
  void (*co_reset)(FAR struct can_dev_s *dev);
  int (*co_setup)(FAR struct can_dev_s *dev);
  void (*co_shutdown)(FAR struct can_dev_s *dev);
  void (*co_rxint)(FAR struct can_dev_s *dev, bool enable);
  void (*co_txint)(FAR struct can_dev_s *dev, bool enable);
  int (*co_ioctl)(FAR struct can_dev_s *dev, int cmd, unsigned long arg);
  int (*co_remoterequest)(FAR struct can_dev_s *dev, uint16_t id);
  int (*co_send)(FAR struct can_dev_s *dev, FAR struct can_msg_s *msg);
  bool (*co_txready)(FAR struct can_dev_s *dev);
  bool (*co_txempty)(FAR struct can_dev_s *dev);
  bool (*co_cancel)(FAR struct can_dev_s *dev, FAR struct can_msg_s *msg);
  void (*co_errhandle)(FAR struct can_dev_s *dev);
};

struct can_dev_s
{
  FAR const struct can_ops_s *cd_ops;
  FAR void *cd_priv;
  int cd_crefs;
};

/* Upper-half entry points (implemented by the suite mock). */

int can_receive(FAR struct can_dev_s *dev, FAR struct can_hdr_s *hdr,
                FAR uint8_t *data);
void can_txdone(FAR struct can_dev_s *dev);

#endif /* __MOCK_NUTTX_CAN_H */
