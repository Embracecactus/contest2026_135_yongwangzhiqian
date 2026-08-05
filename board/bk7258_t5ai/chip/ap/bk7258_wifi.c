/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/ap/
 * bk7258_wifi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-side adapter between the official Beken v3.1.1.9 Wi-Fi vnet proxy and
 * the native NuttX network stack.  The vendor lwIP runtime is deliberately
 * not linked into the AP image.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arpa/inet.h>

#include <nuttx/kmalloc.h>
#include <nuttx/net/ethernet.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/netdev.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#ifdef CONFIG_NET_PKT
#  include <nuttx/net/pkt.h>
#endif

#include <arch/chip/bk7258_wifi.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WIFI_VIF_STA              0
#define BK7258_WIFI_PBUF_RAW              0
/* Immutable v3.1.1.9 AP archive ABI: sdkconfig reserves 108 bytes for the
 * MSDU head and 600 bytes for the controller descriptor before payload.
 */

#define BK7258_WIFI_MSDU_HEAD_LENGTH      108u
#define BK7258_WIFI_MSDU_DESC_LENGTH      600u
#define BK7258_WIFI_PBUF_RAW_TX           \
  (BK7258_WIFI_MSDU_HEAD_LENGTH + BK7258_WIFI_MSDU_DESC_LENGTH)
#define BK7258_WIFI_PBUF_RAM              0x280
#define BK7258_WIFI_PBUF_RAM_RX           0x380
#define BK7258_WIFI_PBUF_TYPE_CONTIGUOUS  0x80
#define BK7258_WIFI_PBUF_ALIGN            4u
#define BK7258_WIFI_PKTBUF_SIZE           \
  (MAX_NETDEV_PKTSIZE + CONFIG_NET_GUARDSIZE)

#define BK7258_WIFI_WDRV_MAC_LENGTH        6u
#define BK7258_WIFI_MAC_READY_TIMEOUT_MS    5000u
#define BK7258_WIFI_LINK_CONNECTED             3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* These two structures are the immutable 32-bit ABI used by the official
 * AP Wi-Fi archives.  Keep them local: the AP NuttX network stack must not
 * acquire a source dependency on the vendor lwIP headers.
 */

struct pbuf
{
  FAR struct pbuf *next;
  FAR void *payload;
  uint16_t tot_len;
  uint16_t len;
  uint8_t type_internal;
  uint8_t flags;
  uint8_t ref;
  uint8_t if_idx;
};

struct bk7258_wifi_common_header_s
{
  uint16_t length;
  uint8_t type : 4;
  uint8_t dst_index : 4;
  uint8_t need_free : 1;
  uint8_t is_buf_bank : 1;
  uint8_t vif_idx : 2;
  uint8_t special_type : 3;
  uint8_t reserved : 1;
};

struct bk7258_wifi_cpdu_s
{
  FAR struct bk7258_wifi_cpdu_s *next;
  struct bk7258_wifi_common_header_s header;
};

/* Prefix of the immutable v3.1.1.9 wdrv_host_env object.  bk_wifi_init()
 * already performs the official GET_MAC handshake and stores its result
 * here.  Keep this narrow local declaration so the AP NuttX network stack
 * does not acquire the vendor lwIP/private driver header closure.
 */

struct bk7258_wifi_wdrv_connect_ind_s
{
  uint8_t ssid[33];
  int8_t rssi;
  uint32_t ipaddr;
  uint32_t netmask;
  uint32_t router;
  uint32_t dns;
};

struct bk7258_wifi_wdrv_host_prefix_s
{
  int8_t wlan_mode;
  uint8_t wlan_link_sta_status;
  bool mac_ready;
  uint8_t reserved;
  FAR void *confirm_lock;
  FAR void *confirm_first;
  FAR void *confirm_last;
  uint8_t mac[BK7258_WIFI_WDRV_MAC_LENGTH];
  uint8_t get_wlan_cfm[99];
  struct bk7258_wifi_wdrv_connect_ind_s connect_ind;
};

struct bk7258_wifi_driver_s
{
  bool registered;
  bool ifup;
  struct work_s pollwork;
  struct net_driver_s dev;
};

_Static_assert(sizeof(struct pbuf) == 16,
               "Beken Wi-Fi pbuf ABI must remain 16 bytes");
_Static_assert(sizeof(struct bk7258_wifi_cpdu_s) == 8,
               "Beken Wi-Fi CPDU ABI must remain 8 bytes");
_Static_assert(offsetof(struct bk7258_wifi_wdrv_host_prefix_s, mac) == 16,
               "Beken Wi-Fi cached MAC ABI must remain at offset 16");
_Static_assert(offsetof(struct bk7258_wifi_wdrv_host_prefix_s,
                        wlan_link_sta_status) == 1,
               "Beken Wi-Fi link state ABI must remain at offset 1");
_Static_assert(offsetof(struct bk7258_wifi_wdrv_host_prefix_s,
                        connect_ind) == 124,
               "Beken Wi-Fi IPv4 indication ABI must remain at offset 124");
_Static_assert(offsetof(struct bk7258_wifi_wdrv_connect_ind_s, ipaddr) == 36,
               "Beken Wi-Fi IPv4 address ABI must remain at offset 36");
_Static_assert(sizeof(struct bk7258_wifi_wdrv_connect_ind_s) == 52,
               "Beken Wi-Fi IPv4 indication ABI must remain 52 bytes");

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_wifi_transmit(FAR struct net_driver_s *dev);
static int bk7258_wifi_txpoll(FAR struct net_driver_s *dev);
static int bk7258_wifi_ifup(FAR struct net_driver_s *dev);
static int bk7258_wifi_ifdown(FAR struct net_driver_s *dev);
static int bk7258_wifi_txavail(FAR struct net_driver_s *dev);
static void bk7258_wifi_txavail_work(FAR void *arg);
static int bk7258_wifi_cached_mac(FAR uint8_t *mac);
static int bk7258_wifi_wait_cached_mac(FAR uint8_t *mac);

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

/* The public SDK bundle carries the CP form of bk_wifi_init().  The official
 * AP archive actually exports this no-argument ABI, so declare it narrowly
 * here instead of including the incompatible CP-facing declaration.
 */

extern int bk_wifi_init(void);
extern int wdrv_txdata_sender(FAR struct pbuf *p, uint32_t vif_idx);
extern struct bk7258_wifi_wdrv_host_prefix_s wdrv_host_env;

FAR struct pbuf *pbuf_alloc(int layer, uint16_t length, int type);
uint8_t pbuf_header(FAR struct pbuf *p, int16_t increment);
void pbuf_ref(FAR struct pbuf *p);
uint8_t pbuf_free(FAR struct pbuf *p);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static spinlock_t g_bk7258_wifi_pbuf_lock;
static uint16_t g_bk7258_wifi_pktbuf[(BK7258_WIFI_PKTBUF_SIZE + 1) / 2];
static struct bk7258_wifi_driver_s g_bk7258_wifi;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static size_t bk7258_wifi_align(size_t value)
{
  return (value + BK7258_WIFI_PBUF_ALIGN - 1u) &
         ~(BK7258_WIFI_PBUF_ALIGN - 1u);
}

static int bk7258_wifi_cached_mac(FAR uint8_t *mac)
{
  memcpy(mac, wdrv_host_env.mac, BK7258_WIFI_WDRV_MAC_LENGTH);

  /* The SDK's wdrv_get_mac_addr() discards the synchronous command result.
   * Validate the value it cached so a missing confirmation still fails
   * closed without issuing a second command into the bounded mailbox FIFO.
   */

  if ((mac[0] & 1u) != 0u ||
      memcmp(mac, "\0\0\0\0\0\0", BK7258_WIFI_WDRV_MAC_LENGTH) == 0 ||
      memcmp(mac, "\xff\xff\xff\xff\xff\xff",
             BK7258_WIFI_WDRV_MAC_LENGTH) == 0)
    {
      return -EPROTO;
    }

  return OK;
}

static int bk7258_wifi_wait_cached_mac(FAR uint8_t *mac)
{
  uint32_t elapsed;
  int ret;

  /* On the first power-on, CP RF calibration can delay the official
   * GET_MAC confirmation beyond the AP archive's fixed two-second command
   * wait.  The archive deliberately accepts that timeout and its RX worker
   * still caches a late confirmation.  Keep the wrapper readiness gate
   * tied to that real result instead of issuing a duplicate mailbox command
   * or adding an unconditional boot delay.
   */

  for (elapsed = 0; elapsed < BK7258_WIFI_MAC_READY_TIMEOUT_MS; elapsed++)
    {
      ret = bk7258_wifi_cached_mac(mac);
      if (ret == OK)
        {
          return OK;
        }

      (void)nxsig_usleep(1000);
    }

  return bk7258_wifi_cached_mac(mac);
}

static void bk7258_wifi_sync_carrier_locked(
  FAR struct bk7258_wifi_driver_s *priv)
{
  bool connected;

  connected = __atomic_load_n(&wdrv_host_env.wlan_link_sta_status,
                              __ATOMIC_ACQUIRE) ==
              BK7258_WIFI_LINK_CONNECTED;

  if (priv->registered && priv->ifup && connected)
    {
      netdev_carrier_on(&priv->dev);
    }
  else if (priv->registered)
    {
      netdev_carrier_off(&priv->dev);
    }
}

static int bk7258_wifi_transmit(FAR struct net_driver_s *dev)
{
  FAR struct pbuf *p;
  FAR struct bk7258_wifi_cpdu_s *cpdu;
  int ret;

  if (dev->d_len == 0 || dev->d_len > BK7258_WIFI_PKTBUF_SIZE)
    {
      return -EINVAL;
    }

  p = pbuf_alloc(BK7258_WIFI_PBUF_RAW_TX, dev->d_len,
                 BK7258_WIFI_PBUF_RAM);
  if (p == NULL)
    {
      NETDEV_TXERRORS(dev);
      return -ENOMEM;
    }

  cpdu = (FAR struct bk7258_wifi_cpdu_s *)(p + 1);
  memset(cpdu, 0, sizeof(*cpdu));
  memcpy(p->payload, dev->d_buf, dev->d_len);

  ret = wdrv_txdata_sender(p, BK7258_WIFI_VIF_STA);

  /* wdrv_txdata_sender() takes its own reference before queueing.  Release
   * the driver's reference in both paths; on a queue failure the SDK has
   * already released the reference that it briefly acquired.
   */

  pbuf_free(p);
  if (ret != 0)
    {
      NETDEV_TXERRORS(dev);
      return -EAGAIN;
    }

  NETDEV_TXPACKETS(dev);
  NETDEV_TXDONE(dev);
  return OK;
}

static int bk7258_wifi_txpoll(FAR struct net_driver_s *dev)
{
  return bk7258_wifi_transmit(dev) < 0 ? 1 : 0;
}

static int bk7258_wifi_ifup(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_wifi_driver_s *priv = dev->d_private;

  priv->ifup = true;
  bk7258_wifi_sync_carrier_locked(priv);
  return OK;
}

static int bk7258_wifi_ifdown(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_wifi_driver_s *priv = dev->d_private;

  priv->ifup = false;
  bk7258_wifi_sync_carrier_locked(priv);
  return OK;
}

static void bk7258_wifi_txavail_work(FAR void *arg)
{
  FAR struct bk7258_wifi_driver_s *priv = arg;

  net_lock();
  if (priv->registered && priv->ifup)
    {
      devif_poll(&priv->dev, bk7258_wifi_txpoll);
    }

  net_unlock();
}

static int bk7258_wifi_txavail(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_wifi_driver_s *priv = dev->d_private;
  int ret = OK;

  if (work_available(&priv->pollwork))
    {
      ret = work_queue(LPWORK, &priv->pollwork,
                       bk7258_wifi_txavail_work, priv, 0);
    }

  return ret;
}

/****************************************************************************
 * Public Functions Required by the Official AP Wi-Fi Archives
 ****************************************************************************/

FAR struct pbuf *pbuf_alloc(int layer, uint16_t length, int type)
{
  FAR struct pbuf *p;
  size_t offset;
  size_t allocsize;

  if ((type != BK7258_WIFI_PBUF_RAM &&
       type != BK7258_WIFI_PBUF_RAM_RX) || layer < 0)
    {
      return NULL;
    }

  offset = bk7258_wifi_align((size_t)layer);
  allocsize = sizeof(*p) + offset + bk7258_wifi_align(length);
  if (allocsize < length || allocsize > UINT16_MAX + sizeof(*p))
    {
      return NULL;
    }

  p = kmm_malloc(allocsize);
  if (p == NULL)
    {
      return NULL;
    }

  p->next          = NULL;
  p->payload       = (FAR uint8_t *)(p + 1) + offset;
  p->tot_len       = length;
  p->len           = length;
  p->type_internal = BK7258_WIFI_PBUF_TYPE_CONTIGUOUS;
  p->flags         = 0;
  p->ref           = 1;
  p->if_idx        = UINT8_MAX;
  return p;
}

uint8_t pbuf_header(FAR struct pbuf *p, int16_t increment)
{
  FAR uint8_t *payload;

  if (p == NULL)
    {
      return 1;
    }

  payload = p->payload;
  if (increment > 0)
    {
      if ((size_t)(payload - (FAR uint8_t *)(p + 1)) <
          (size_t)increment ||
          (uint32_t)p->len + (uint32_t)increment > UINT16_MAX ||
          (uint32_t)p->tot_len + (uint32_t)increment > UINT16_MAX)
        {
          return 1;
        }
    }
  else if ((uint16_t)(-increment) > p->len)
    {
      return 1;
    }

  p->payload = payload - increment;
  p->len = (uint16_t)(p->len + increment);
  p->tot_len = (uint16_t)(p->tot_len + increment);
  return 0;
}

void pbuf_ref(FAR struct pbuf *p)
{
  irqstate_t flags;

  if (p == NULL)
    {
      return;
    }

  flags = spin_lock_irqsave(&g_bk7258_wifi_pbuf_lock);
  if (p->ref != UINT8_MAX)
    {
      p->ref++;
    }

  spin_unlock_irqrestore(&g_bk7258_wifi_pbuf_lock, flags);
}

uint8_t pbuf_free(FAR struct pbuf *p)
{
  FAR struct pbuf *next;
  irqstate_t flags;
  uint8_t count = 0;

  while (p != NULL)
    {
      flags = spin_lock_irqsave(&g_bk7258_wifi_pbuf_lock);
      if (p->ref == 0)
        {
          spin_unlock_irqrestore(&g_bk7258_wifi_pbuf_lock, flags);
          break;
        }

      p->ref--;
      if (p->ref != 0)
        {
          spin_unlock_irqrestore(&g_bk7258_wifi_pbuf_lock, flags);
          break;
        }

      next = p->next;
      spin_unlock_irqrestore(&g_bk7258_wifi_pbuf_lock, flags);
      kmm_free(p);
      p = next;
      count++;
    }

  return count;
}

int host_wlan_add_netif(FAR uint8_t *mac)
{
  FAR struct bk7258_wifi_driver_s *priv = &g_bk7258_wifi;
  int ret;

  if (mac == NULL)
    {
      return -EINVAL;
    }

  /* bk_wifi_init() adds STA followed by SoftAP.  N16 is deliberately
   * STA-only, so the first call owns wlan0 and the second call is accepted
   * without registering a vendor/lwIP SoftAP interface.
   */

  if (priv->registered)
    {
      return OK;
    }

  memset(priv, 0, sizeof(*priv));
  priv->dev.d_buf     = (FAR uint8_t *)g_bk7258_wifi_pktbuf;
  priv->dev.d_ifup    = bk7258_wifi_ifup;
  priv->dev.d_ifdown  = bk7258_wifi_ifdown;
  priv->dev.d_txavail = bk7258_wifi_txavail;
  priv->dev.d_private = priv;
  strlcpy(priv->dev.d_ifname, "wlan%d", IFNAMSIZ);
  memcpy(priv->dev.d_mac.ether.ether_addr_octet, mac, 6);

  ret = netdev_register(&priv->dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;
  return OK;
}

int host_wlan_remove_netif(void)
{
  FAR struct bk7258_wifi_driver_s *priv = &g_bk7258_wifi;
  int ret;

  if (!priv->registered)
    {
      return OK;
    }

  priv->ifup = false;
  net_lock();
  bk7258_wifi_sync_carrier_locked(priv);
  net_unlock();
  work_cancel_sync(LPWORK, &priv->pollwork);
  ret = netdev_unregister(&priv->dev);
  if (ret >= 0)
    {
      priv->registered = false;
    }

  return ret;
}

int host_wlan_remove_sap_netif(void)
{
  return OK;
}

/* The v3.1.1.9 AP archive retains its lwIP-oriented IP-control calls during
 * early initialization.  Address configuration belongs exclusively to
 * native NuttX in N16, so these hooks must not enter the NuttX network stack.
 * Carrier state is synchronized by the project control wrapper after the CP
 * lease has been applied to wlan0.
 */

void sta_ip_mode_set(int dhcp)
{
  (void)dhcp;
}

void sta_ip_down(void)
{
}

void sta_ip_start(void)
{
}

uint32_t uap_ip_is_start(void)
{
  return 0;
}

void uap_ip_down(void)
{
}

void uap_ip_start(void)
{
}

int bk_netif_set_ip4_config(int iface, FAR const void *config)
{
  (void)iface;
  (void)config;
  return OK;
}

void ethernetif_input(int iface, FAR struct pbuf *p, uint8_t dst_idx)
{
  FAR struct bk7258_wifi_driver_s *priv = &g_bk7258_wifi;
  FAR struct eth_hdr_s *eth;

  (void)dst_idx;

  if (p == NULL)
    {
      return;
    }

  net_lock();
  if (iface != BK7258_WIFI_VIF_STA || !priv->registered || !priv->ifup ||
      p->payload == NULL || p->len < ETH_HDRLEN ||
      p->len > BK7258_WIFI_PKTBUF_SIZE)
    {
      if (priv->registered)
        {
          NETDEV_RXDROPPED(&priv->dev);
        }

      net_unlock();
      pbuf_free(p);
      return;
    }

  memcpy(priv->dev.d_buf, p->payload, p->len);
  priv->dev.d_len = p->len;
  NETDEV_RXPACKETS(&priv->dev);

#ifdef CONFIG_NET_PKT
  pkt_input(&priv->dev);
#endif

  eth = (FAR struct eth_hdr_s *)priv->dev.d_buf;

#ifdef CONFIG_NET_IPv4
  if (eth->type == HTONS(ETHTYPE_IP))
    {
      NETDEV_RXIPV4(&priv->dev);
      ipv4_input(&priv->dev);
    }
  else
#endif
#ifdef CONFIG_NET_IPv6
  if (eth->type == HTONS(ETHTYPE_IP6))
    {
      NETDEV_RXIPV6(&priv->dev);
      ipv6_input(&priv->dev);
    }
  else
#endif
#ifdef CONFIG_NET_ARP
  if (eth->type == HTONS(ETHTYPE_ARP))
    {
      arp_input(&priv->dev);
      NETDEV_RXARP(&priv->dev);
    }
  else
#endif
    {
      priv->dev.d_len = 0;
      NETDEV_RXDROPPED(&priv->dev);
    }

  if (priv->dev.d_len > 0)
    {
      (void)bk7258_wifi_transmit(&priv->dev);
    }

  net_unlock();
  pbuf_free(p);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_wifi_initialize(void)
{
  uint8_t mac[BK7258_WIFI_WDRV_MAC_LENGTH];
  int ret;

  ret = bk_wifi_init();
  if (ret != 0)
    {
      return ret < 0 ? ret : -EIO;
    }

  if (!g_bk7258_wifi.registered)
    {
      return -ENODEV;
    }

  ret = bk7258_wifi_wait_cached_mac(mac);
  if (ret < 0)
    {
      return ret;
    }

  net_lock();
  memcpy(g_bk7258_wifi.dev.d_mac.ether.ether_addr_octet, mac,
         sizeof(mac));
  net_unlock();
  return OK;
}

int bk7258_wifi_refresh_carrier(void)
{
  FAR struct bk7258_wifi_driver_s *priv = &g_bk7258_wifi;

  if (!priv->registered)
    {
      return -ENODEV;
    }

  net_lock();
  bk7258_wifi_sync_carrier_locked(priv);
  net_unlock();
  return OK;
}

int bk7258_wifi_read_link(struct bk7258_wifi_result_s *result)
{
  struct bk7258_wifi_wdrv_connect_ind_s first;
  struct bk7258_wifi_wdrv_connect_ind_s second;
  uint8_t before;
  uint8_t after;

  if (result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  before = __atomic_load_n(&wdrv_host_env.wlan_link_sta_status,
                           __ATOMIC_ACQUIRE);
  __asm volatile ("dmb sy" ::: "memory");
  memcpy(&first, &wdrv_host_env.connect_ind, sizeof(first));
  __asm volatile ("dmb sy" ::: "memory");
  memcpy(&second, &wdrv_host_env.connect_ind, sizeof(second));
  __asm volatile ("dmb sy" ::: "memory");
  after = __atomic_load_n(&wdrv_host_env.wlan_link_sta_status,
                          __ATOMIC_ACQUIRE);

  if (before != after || memcmp(&first, &second, sizeof(first)) != 0)
    {
      explicit_bzero(&first, sizeof(first));
      explicit_bzero(&second, sizeof(second));
      return -EAGAIN;
    }

  result->link_state = after;
  if (after == BK7258_WIFI_LINK_CONNECTED)
    {
      /* v3.1.1.9 publishes CONNECTED for VNET together with the CP DHCP
       * result.  A zero address therefore means the indication is not yet
       * safe for the AP network stack to consume.
       */

      if (first.ipaddr == 0)
        {
          explicit_bzero(&first, sizeof(first));
          explicit_bzero(&second, sizeof(second));
          return -EAGAIN;
        }

      result->rssi = first.rssi;
      result->ipaddr = first.ipaddr;
      result->netmask = first.netmask;
      result->router = first.router;
    }

  explicit_bzero(&first, sizeof(first));
  explicit_bzero(&second, sizeof(second));
  return OK;
}
