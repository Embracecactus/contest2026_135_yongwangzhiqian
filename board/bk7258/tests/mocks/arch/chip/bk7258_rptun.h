/****************************************************************************
 * tests/mocks/arch/chip/bk7258_rptun.h
 *
 * Host shim for the RPTUN mailbox constants and the control struct.  Only the
 * symbols bk7258_rptun_mbox.c references are modelled; the real layout
 * static_asserts and shared-memory math are intentionally omitted.
 ****************************************************************************/

#ifndef __MOCK_ARCH_CHIP_BK7258_RPTUN_H
#define __MOCK_ARCH_CHIP_BK7258_RPTUN_H

#include <stdint.h>

#define BK7258_RPTUN_MBOX_COMMAND        0xb9u
#define BK7258_RPTUN_MBOX_DATA_LENGTH    16u
#define BK7258_RPTUN_MBOX_LOGICAL_INDEX  14u
#define BK7258_RPTUN_MBOX_PROBE_REPLY    (1u << 31)
#define BK7258_RPTUN_MBOX_PROBE_SEQUENCE \
  (~BK7258_RPTUN_MBOX_PROBE_REPLY)
#define BK7258_RPTUN_PM_WAKE_PREPARE     1u
#define BK7258_RPTUN_PM_WAKE_RELEASE     2u

enum bk7258_rptun_mbox_type_e
{
  BK7258_RPTUN_MBOX_INVALID = 0,
  BK7258_RPTUN_MBOX_LIFECYCLE,
  BK7258_RPTUN_MBOX_NOTIFY,
  BK7258_RPTUN_MBOX_HEARTBEAT,
  BK7258_RPTUN_MBOX_PROBE,
  BK7258_RPTUN_MBOX_PM_WAKE
};

/* AP mailbox progress bits, passed to the (no-op) mark() macro. */
#define BK7258_RPTUN_FLAG_AP_MBOX_SEMS     (1u << 8)
#define BK7258_RPTUN_FLAG_AP_MBOX_THREAD   (1u << 9)
#define BK7258_RPTUN_FLAG_AP_MBOX_PINNED   (1u << 10)
#define BK7258_RPTUN_FLAG_AP_MBOX_INIT     (1u << 11)
#define BK7258_RPTUN_FLAG_AP_MBOX_OPEN     (1u << 12)
#define BK7258_RPTUN_FLAG_AP_MBOX_CBS      (1u << 13)

struct bk7258_rptun_control_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;
  uint32_t state;
  uint32_t flags;
  uint32_t error;
  uint32_t resource_crc32;
  uint32_t cp_to_ap_pending;
  uint32_t ap_to_cp_pending;
  uint32_t cp_heartbeat;
  uint32_t ap_heartbeat;
  uint32_t cp_epoch;
  uint32_t ap_epoch;
  uint32_t cp_rx_sequence;
  uint32_t ap_rx_sequence;
};

volatile struct bk7258_rptun_control_s *bk7258_rptun_control(void);

#endif /* __MOCK_ARCH_CHIP_BK7258_RPTUN_H */
