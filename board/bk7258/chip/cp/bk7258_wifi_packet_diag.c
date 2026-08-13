/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/
 * bk7258_wifi_packet_diag.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <modules/wifi.h>
#include <os/os.h>

#include "bk7258_wifi_packet_diag.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_ETH_HEADER_LEN           14u
#define BK7258_ETH_TYPE_IPV4            UINT16_C(0x0800)
#define BK7258_IP_PROTOCOL_UDP           17u
#define BK7258_UDP_HEADER_LEN             8u
#define BK7258_DHCP_FIXED_LEN           240u
#define BK7258_DHCP_CLIENT_PORT          68u
#define BK7258_DHCP_SERVER_PORT          67u
#define BK7258_DHCP_OPTION_PAD            0u
#define BK7258_DHCP_OPTION_MSG_TYPE      53u
#define BK7258_DHCP_OPTION_END          255u
#define BK7258_DHCP_DISCOVER              1u
#define BK7258_DHCP_OFFER                 2u
#define BK7258_DHCP_REQUEST               3u
#define BK7258_DHCP_ACK                   5u
#define BK7258_DHCP_NAK                   6u

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Immutable Beken lwIP 2.1.2 32-bit pbuf prefix.  Only payload and len are
 * observed.  The AP adapter already enforces the same 16-byte archive ABI.
 */

struct pbuf
{
  struct pbuf *next;
  void *payload;
  uint16_t tot_len;
  uint16_t len;
  uint8_t type_internal;
  uint8_t flags;
  uint8_t ref;
  uint8_t if_idx;
};

struct udp_pcb;
struct netif;

typedef int8_t bk7258_lwip_err_t;

_Static_assert(sizeof(struct pbuf) == 16,
               "Beken CP lwIP pbuf ABI must remain 16 bytes");
_Static_assert(offsetof(struct pbuf, payload) == 4,
               "Beken CP lwIP pbuf payload offset must remain 4");
_Static_assert(offsetof(struct pbuf, len) == 10,
               "Beken CP lwIP pbuf len offset must remain 10");

/****************************************************************************
 * Public Data
 ****************************************************************************/

volatile struct bk7258_wifi_packet_diag_s g_bk7258_wifi_packet_diag =
{
  .magic = BK7258_WIFI_PACKET_DIAG_MAGIC,
  .version = BK7258_WIFI_PACKET_DIAG_VERSION,
  .tx_min_free_heap = UINT32_MAX,
  .tx_min_heap_margin = INT32_MAX,
};

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

void __real_ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx);

bk7258_lwip_err_t __real_udp_sendto_if_src(struct udp_pcb *pcb,
                                           struct pbuf *p,
                                           const void *dst_ip,
                                           uint16_t dst_port,
                                           struct netif *netif,
                                           const void *src_ip);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint16_t bk7258_wifi_diag_be16(const uint8_t *data)
{
  return ((uint16_t)data[0] << 8) | data[1];
}

static uint32_t bk7258_wifi_diag_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) |
         ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) |
         data[3];
}

static bool bk7258_wifi_diag_cookie_valid(const uint8_t *dhcp,
                                          size_t length)
{
  return length >= BK7258_DHCP_FIXED_LEN &&
         dhcp[236] == 99 && dhcp[237] == 130 &&
         dhcp[238] == 83 && dhcp[239] == 99;
}

static uint8_t bk7258_wifi_diag_message_type(const uint8_t *dhcp,
                                             size_t length)
{
  size_t offset = BK7258_DHCP_FIXED_LEN;

  while (offset < length)
    {
      uint8_t option = dhcp[offset++];
      uint8_t option_len;

      if (option == BK7258_DHCP_OPTION_PAD)
        {
          continue;
        }

      if (option == BK7258_DHCP_OPTION_END || offset >= length)
        {
          break;
        }

      option_len = dhcp[offset++];
      if (option_len > length - offset)
        {
          break;
        }

      if (option == BK7258_DHCP_OPTION_MSG_TYPE && option_len == 1)
        {
          return dhcp[offset];
        }

      offset += option_len;
    }

  return 0;
}

static void bk7258_wifi_diag_record_tx(struct pbuf *p, uint16_t dst_port)
{
  volatile struct bk7258_wifi_packet_diag_s *diag =
    &g_bk7258_wifi_packet_diag;
  const uint8_t *dhcp;
  uint8_t type;

  diag->tx_calls++;
  diag->tx_last_dst_port = dst_port;

  if (dst_port != BK7258_DHCP_SERVER_PORT || p == NULL ||
      p->payload == NULL || p->len < BK7258_DHCP_FIXED_LEN)
    {
      if (dst_port == BK7258_DHCP_SERVER_PORT)
        {
          diag->tx_truncated++;
        }

      return;
    }

  dhcp = p->payload;
  if (!bk7258_wifi_diag_cookie_valid(dhcp, p->len))
    {
      diag->tx_truncated++;
      return;
    }

  type = bk7258_wifi_diag_message_type(dhcp, p->len);
  diag->tx_dhcp++;
  diag->tx_last_xid = bk7258_wifi_diag_be32(dhcp + 4);
  diag->tx_last_src_port = BK7258_DHCP_CLIENT_PORT;
  diag->tx_last_type = type;
  memcpy((void *)diag->tx_last_chaddr, dhcp + 28,
         sizeof(diag->tx_last_chaddr));

  if (type == BK7258_DHCP_DISCOVER)
    {
      diag->tx_discover++;
    }
  else if (type == BK7258_DHCP_REQUEST)
    {
      diag->tx_request++;
    }
}

static void bk7258_wifi_diag_record_tx_heap(uint16_t dst_port)
{
  volatile struct bk7258_wifi_packet_diag_s *diag =
    &g_bk7258_wifi_packet_diag;
  uint32_t free_heap;
  uint16_t min_reserve = 0;
  int32_t margin;

  if (dst_port != BK7258_DHCP_SERVER_PORT)
    {
      return;
    }

  free_heap = (uint32_t)rtos_get_free_heap_size();
  (void)bk_wifi_get_min_rsv_mem(&min_reserve);
  margin = (int32_t)free_heap - (int32_t)min_reserve;

  diag->tx_heap_samples++;
  diag->tx_last_free_heap = free_heap;
  diag->tx_last_min_reserve = min_reserve;
  diag->tx_last_heap_margin = margin;
  if (free_heap < diag->tx_min_free_heap)
    {
      diag->tx_min_free_heap = free_heap;
    }

  if (margin < diag->tx_min_heap_margin)
    {
      diag->tx_min_heap_margin = margin;
    }
}

static void bk7258_wifi_diag_record_rx(int iface, struct pbuf *p,
                                       uint8_t dst_idx)
{
  volatile struct bk7258_wifi_packet_diag_s *diag =
    &g_bk7258_wifi_packet_diag;
  const uint8_t *frame;
  const uint8_t *ip;
  const uint8_t *udp;
  const uint8_t *dhcp;
  size_t ip_len;
  size_t dhcp_len;
  uint16_t src_port;
  uint16_t dst_port;
  uint8_t type;

  diag->rx_frames++;
  if (p == NULL || p->payload == NULL || p->len < BK7258_ETH_HEADER_LEN)
    {
      diag->rx_truncated++;
      return;
    }

  frame = p->payload;
  memcpy((void *)diag->rx_last_dest, frame,
         sizeof(diag->rx_last_dest));
  diag->rx_last_iface = (uint8_t)iface;
  diag->rx_last_dst_idx = dst_idx;

  if (bk7258_wifi_diag_be16(frame + 12) != BK7258_ETH_TYPE_IPV4)
    {
      return;
    }

  diag->rx_ipv4++;
  if (p->len < BK7258_ETH_HEADER_LEN + 20u)
    {
      diag->rx_truncated++;
      return;
    }

  ip = frame + BK7258_ETH_HEADER_LEN;
  ip_len = (size_t)(ip[0] & 0x0f) * 4u;
  if ((ip[0] >> 4) != 4 || ip_len < 20u ||
      p->len < BK7258_ETH_HEADER_LEN + ip_len + BK7258_UDP_HEADER_LEN)
    {
      diag->rx_truncated++;
      return;
    }

  diag->rx_last_src_ip = bk7258_wifi_diag_be32(ip + 12);
  diag->rx_last_dst_ip = bk7258_wifi_diag_be32(ip + 16);
  if (ip[9] != BK7258_IP_PROTOCOL_UDP)
    {
      return;
    }

  diag->rx_udp++;
  udp = ip + ip_len;
  src_port = bk7258_wifi_diag_be16(udp);
  dst_port = bk7258_wifi_diag_be16(udp + 2);
  diag->rx_last_src_port = src_port;
  diag->rx_last_dst_port = dst_port;

  if (src_port != BK7258_DHCP_SERVER_PORT ||
      dst_port != BK7258_DHCP_CLIENT_PORT)
    {
      return;
    }

  diag->rx_dhcp++;
  dhcp = udp + BK7258_UDP_HEADER_LEN;
  dhcp_len = p->len - (size_t)(dhcp - frame);
  if (!bk7258_wifi_diag_cookie_valid(dhcp, dhcp_len))
    {
      diag->rx_bad_cookie++;
      return;
    }

  type = bk7258_wifi_diag_message_type(dhcp, dhcp_len);
  diag->rx_last_xid = bk7258_wifi_diag_be32(dhcp + 4);
  diag->rx_last_type = type;
  memcpy((void *)diag->rx_last_chaddr, dhcp + 28,
         sizeof(diag->rx_last_chaddr));

  if (type == BK7258_DHCP_OFFER)
    {
      diag->rx_offer++;
    }
  else if (type == BK7258_DHCP_ACK)
    {
      diag->rx_ack++;
    }
  else if (type == BK7258_DHCP_NAK)
    {
      diag->rx_nak++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_wifi_packet_diag_reset(void)
{
  uint32_t generation = g_bk7258_wifi_packet_diag.generation + 1u;

  memset((void *)&g_bk7258_wifi_packet_diag, 0,
         sizeof(g_bk7258_wifi_packet_diag));
  g_bk7258_wifi_packet_diag.magic = BK7258_WIFI_PACKET_DIAG_MAGIC;
  g_bk7258_wifi_packet_diag.version = BK7258_WIFI_PACKET_DIAG_VERSION;
  g_bk7258_wifi_packet_diag.generation = generation;
  g_bk7258_wifi_packet_diag.tx_min_free_heap = UINT32_MAX;
  g_bk7258_wifi_packet_diag.tx_min_heap_margin = INT32_MAX;
}

bk7258_lwip_err_t __wrap_udp_sendto_if_src(struct udp_pcb *pcb,
                                           struct pbuf *p,
                                           const void *dst_ip,
                                           uint16_t dst_port,
                                           struct netif *netif,
                                           const void *src_ip)
{
  bk7258_lwip_err_t ret;

  bk7258_wifi_diag_record_tx_heap(dst_port);
  bk7258_wifi_diag_record_tx(p, dst_port);
  ret = __real_udp_sendto_if_src(pcb, p, dst_ip, dst_port, netif, src_ip);
  g_bk7258_wifi_packet_diag.tx_last_result = ret;
  return ret;
}

void __wrap_ethernetif_input(int iface, struct pbuf *p, uint8_t dst_idx)
{
  bk7258_wifi_diag_record_rx(iface, p, dst_idx);
  __real_ethernetif_input(iface, p, dst_idx);
}
