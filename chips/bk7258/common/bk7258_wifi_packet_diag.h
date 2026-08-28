/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/
 * bk7258_wifi_packet_diag.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __CONTEST2026_135_BOARD_BK7258_CHIP_COMMON_WIFI_PACKET_DIAG_H
#define __CONTEST2026_135_BOARD_BK7258_CHIP_COMMON_WIFI_PACKET_DIAG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WIFI_PACKET_DIAG_MAGIC   UINT32_C(0x57464447) /* WFDG */
#define BK7258_WIFI_PACKET_DIAG_VERSION UINT32_C(2)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_wifi_packet_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t generation;

  uint32_t tx_calls;
  uint32_t tx_dhcp;
  uint32_t tx_discover;
  uint32_t tx_request;
  uint32_t tx_truncated;
  int32_t tx_last_result;
  uint32_t tx_last_xid;
  uint16_t tx_last_src_port;
  uint16_t tx_last_dst_port;
  uint8_t tx_last_chaddr[6];
  uint8_t tx_last_type;
  uint8_t tx_reserved;

  uint32_t rx_frames;
  uint32_t rx_ipv4;
  uint32_t rx_udp;
  uint32_t rx_dhcp;
  uint32_t rx_offer;
  uint32_t rx_ack;
  uint32_t rx_nak;
  uint32_t rx_truncated;
  uint32_t rx_bad_cookie;
  uint32_t rx_last_xid;
  uint32_t rx_last_src_ip;
  uint32_t rx_last_dst_ip;
  uint16_t rx_last_src_port;
  uint16_t rx_last_dst_port;
  uint8_t rx_last_dest[6];
  uint8_t rx_last_chaddr[6];
  uint8_t rx_last_type;
  uint8_t rx_last_iface;
  uint8_t rx_last_dst_idx;
  uint8_t rx_reserved;

  uint32_t tx_heap_samples;
  uint32_t tx_last_free_heap;
  uint32_t tx_min_free_heap;
  uint16_t tx_last_min_reserve;
  uint16_t tx_heap_reserved;
  int32_t tx_last_heap_margin;
  int32_t tx_min_heap_margin;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern volatile struct bk7258_wifi_packet_diag_s
  g_bk7258_wifi_packet_diag;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bk7258_wifi_packet_diag_reset(void);

#endif /* __CONTEST2026_135_BOARD_BK7258_CHIP_COMMON_WIFI_PACKET_DIAG_H */
