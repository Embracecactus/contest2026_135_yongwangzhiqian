/****************************************************************************
 * chips/bk7258/ap/bk7258_eth.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 Ethernet MAC NuttX netdev driver.
 *
 * The v3.1.1.9 AP SDK exports a complete STM32H7-compatible HAL Ethernet
 * driver (HAL_ETH_*, LAN8742 PHY) built with CONFIG_ETH=y.  This wrapper
 * adapts it to the native NuttX netdev interface:
 *
 *   - d_ifup:   HAL_ETH_Init (MAC + DMA descriptors) + LAN8742 PHY init
 *   - d_ifdown: HAL_ETH_Stop + HAL_ETH_DeInit
 *   - RX:       INT_SRC_ETH -> HAL_ETH_IRQHandler -> HAL_ETH_ReadData
 *               -> devif_input
 *   - TX:       devif_poll -> HAL_ETH_Transmit
 *
 * The SDK HAL relies on a few STM32-style helpers that have no bundle
 * provider; this file supplies them board-owned:
 *   - HAL_RCC_GetHCLKFreq()        (clock tick used for MDIO/1us timers)
 *   - HAL_ETH_MspInit()/DeInit()   (ETH GPIO mux + INT_SRC_ETH hook)
 *   - HAL_ETH_RxCpltCallback()     (RX complete -> ReadData loop)
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_ETH

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/net/ethernet.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_eth.h>
#include <arch/chip/eth_mac.h>
#include <arch/chip/lan8742.h>

#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <driver/int.h>
#include <driver/int_types.h>

#include "arm_internal.h"
#include "bk7258_clockdiag.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_ETH_RX_BUF_SIZE   1536u
#define BK7258_ETH_TX_TIMEOUT_MS 1000u
#define BK7258_ETH_PHY_TIMEOUT_MS 3000u
#define BK7258_ETH_LINK_CHECK_MS  1000u

/* ETH GPIO device mux tokens (SDK hal_gpio_types.h). */

#define BK7258_ETH_GPIO_MDC       GPIO_DEV_ENET_MDC
#define BK7258_ETH_GPIO_MDIO      GPIO_DEV_ENET_MDIO
#define BK7258_ETH_GPIO_RXD0      GPIO_DEV_ENET_RXD0
#define BK7258_ETH_GPIO_RXD1      GPIO_DEV_ENET_RXD1
#define BK7258_ETH_GPIO_RXDV      GPIO_DEV_ENET_RXDV
#define BK7258_ETH_GPIO_TXD0      GPIO_DEV_ENET_TXD0
#define BK7258_ETH_GPIO_TXD1      GPIO_DEV_ENET_TXD1
#define BK7258_ETH_GPIO_TXEN      GPIO_DEV_ENET_TXEN
#define BK7258_ETH_GPIO_REF_CLK   GPIO_DEV_ENET_REF_CLK
#define BK7258_ETH_GPIO_PHY_INT   GPIO_DEV_ENET_PHY_INT

/* SDK GPIO device-mux API is not exported in the bundle headers. */

extern bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
extern bk_err_t gpio_dev_map(gpio_id_t gpio_id, uint32_t gpio_dev);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_eth_driver_s
{
  struct net_driver_s dev;
  FAR const struct bk7258_eth_board_s *board;
  ETH_HandleTypeDef heth;
  lan8742_Object_t phy;
  struct work_s pollwork;
  struct work_s linkwork;
  bool ifup;
  bool registered;
  uint8_t rx_pkt[1536];
  uint32_t rx_pkt_len;
  uint8_t tx_buf[1536];
  uint32_t rx_pool_idx;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* DMA descriptor rings and receive buffers (must be 4-byte aligned). */

static ETH_DMADescTypeDef g_eth_tx_desc[ETH_TX_DESC_CNT]
  __attribute__((aligned(4)));
static ETH_DMADescTypeDef g_eth_rx_desc[ETH_RX_DESC_CNT]
  __attribute__((aligned(4)));
static uint8_t g_eth_rx_buf[ETH_RX_DESC_CNT][BK7258_ETH_RX_BUF_SIZE]
  __attribute__((aligned(4)));

static struct bk7258_eth_driver_s g_eth;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void bk7258_eth_isr(void *arg);

/****************************************************************************
 * STM32-compatible helpers required by the SDK HAL
 ****************************************************************************/

uint32_t HAL_RCC_GetHCLKFreq(void)
{
  /* HCLK means the shared bus clock, not physical AP CPU1/CPU2.  In the
   * pinned SDK HAL, the dynamic MDIO selection that calls this function is
   * compiled out and DIV62 is unconditional.  CONFIG_ETH_LPI, which is off
   * in the pinned AP profile, samples this value once for MAC1USTCR during
   * MAC initialization.  Return the official current OPP bus mapping here;
   * a future LPI + runtime-DVFS profile must also reprogram MAC1USTCR from a
   * PM notifier after each OPP transition.
   */

  return bk7258_clockdiag_current_bus_hz();
}

void HAL_ETH_MspInit(ETH_HandleTypeDef *heth)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;
  FAR const struct bk7258_eth_board_s *board = priv->board;
  irqstate_t flags;

  (void)heth;

  if (board == NULL)
    {
      return;
    }

  flags = enter_critical_section();

  if (board->pin_group == BK7258_ETH_PIN_GROUP0)
    {
      /* RMII group 0: pins 27(PHY_INT), 29(MDC), 32(MDIO),
       * 33(RXD0), 34(RXD1), 35(RXDV), 36(TXD0), 37(TXD1), 38(TXEN),
       * 39(REF_CLK).
       */

      (void)gpio_dev_unmap(GPIO_27);
      (void)gpio_dev_map(GPIO_27, BK7258_ETH_GPIO_PHY_INT);
      (void)gpio_dev_unmap(GPIO_29);
      (void)gpio_dev_map(GPIO_29, BK7258_ETH_GPIO_MDC);
      (void)gpio_dev_unmap(GPIO_32);
      (void)gpio_dev_map(GPIO_32, BK7258_ETH_GPIO_MDIO);
      (void)gpio_dev_unmap(GPIO_33);
      (void)gpio_dev_map(GPIO_33, BK7258_ETH_GPIO_RXD0);
      (void)gpio_dev_unmap(GPIO_34);
      (void)gpio_dev_map(GPIO_34, BK7258_ETH_GPIO_RXD1);
      (void)gpio_dev_unmap(GPIO_35);
      (void)gpio_dev_map(GPIO_35, BK7258_ETH_GPIO_RXDV);
      (void)gpio_dev_unmap(GPIO_36);
      (void)gpio_dev_map(GPIO_36, BK7258_ETH_GPIO_TXD0);
      (void)gpio_dev_unmap(GPIO_37);
      (void)gpio_dev_map(GPIO_37, BK7258_ETH_GPIO_TXD1);
      (void)gpio_dev_unmap(GPIO_38);
      (void)gpio_dev_map(GPIO_38, BK7258_ETH_GPIO_TXEN);
      (void)gpio_dev_unmap(GPIO_39);
      (void)gpio_dev_map(GPIO_39, BK7258_ETH_GPIO_REF_CLK);
    }
  else
    {
      /* RMII group 1: pins 46..55 (conflicts with LCD). */

      (void)gpio_dev_unmap(GPIO_46);
      (void)gpio_dev_map(GPIO_46, BK7258_ETH_GPIO_REF_CLK);
      (void)gpio_dev_unmap(GPIO_47);
      (void)gpio_dev_map(GPIO_47, BK7258_ETH_GPIO_MDC);
      (void)gpio_dev_unmap(GPIO_48);
      (void)gpio_dev_map(GPIO_48, BK7258_ETH_GPIO_MDIO);
      (void)gpio_dev_unmap(GPIO_49);
      (void)gpio_dev_map(GPIO_49, BK7258_ETH_GPIO_RXD0);
      (void)gpio_dev_unmap(GPIO_50);
      (void)gpio_dev_map(GPIO_50, BK7258_ETH_GPIO_RXD1);
      (void)gpio_dev_unmap(GPIO_51);
      (void)gpio_dev_map(GPIO_51, BK7258_ETH_GPIO_RXDV);
      (void)gpio_dev_unmap(GPIO_52);
      (void)gpio_dev_map(GPIO_52, BK7258_ETH_GPIO_TXD0);
      (void)gpio_dev_unmap(GPIO_53);
      (void)gpio_dev_map(GPIO_53, BK7258_ETH_GPIO_TXD1);
      (void)gpio_dev_unmap(GPIO_54);
      (void)gpio_dev_map(GPIO_54, BK7258_ETH_GPIO_TXEN);
      (void)gpio_dev_unmap(GPIO_55);
      (void)gpio_dev_map(GPIO_55, BK7258_ETH_GPIO_PHY_INT);
    }

  leave_critical_section(flags);

  (void)bk_int_isr_register(INT_SRC_ETH, (int_group_isr_t)bk7258_eth_isr,
                            NULL);
}

void HAL_ETH_MspDeInit(ETH_HandleTypeDef *heth)
{
  (void)heth;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_eth_isr(void *arg)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;

  (void)arg;
  HAL_ETH_IRQHandler(&priv->heth);
}

/* RX DMA buffer allocation: the HAL re-arms descriptors after each
 * HAL_ETH_ReadData() call.  A single in-order consumer (the RX ISR) makes
 * round-robin over the static pool safe.
 */

static void bk7258_eth_rx_allocate(uint8_t **buffer)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;
  uint8_t *b;

  b = g_eth_rx_buf[priv->rx_pool_idx];
  priv->rx_pool_idx = (priv->rx_pool_idx + 1u) % ETH_RX_DESC_CNT;
  *buffer = b;
}

/* HAL RX link callback: called once per descriptor segment of a frame.
 * Concatenate the segments into priv->rx_pkt.
 */

static void bk7258_eth_rx_link(FAR void **pstart, FAR void **pend,
                               FAR uint8_t *buff, uint16_t length)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;

  if (*pstart == NULL)
    {
      priv->rx_pkt_len = 0;
    }

  if (priv->rx_pkt_len + length <= sizeof(priv->rx_pkt))
    {
      memcpy(&priv->rx_pkt[priv->rx_pkt_len], buff, length);
      priv->rx_pkt_len += length;
    }

  *pstart = priv->rx_pkt;
  *pend   = (FAR void *)((uintptr_t)priv->rx_pkt + priv->rx_pkt_len);
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;
  FAR struct eth_hdr_s *eth;
  FAR void *app;
  uint32_t len;

  (void)heth;

  while (HAL_ETH_ReadData(&priv->heth, &app) == HAL_OK)
    {
      len = priv->rx_pkt_len;
      if (len < ETH_HDRLEN || len > 1514)
        {
          continue;
        }

      net_lock();
      if (priv->registered && priv->ifup)
        {
          memcpy(priv->dev.d_buf, priv->rx_pkt, len);
          priv->dev.d_len = len;
          NETDEV_RXPACKETS(&priv->dev);

#ifdef CONFIG_NET_PKT
          pkt_input(&priv->dev);
#endif

          eth = (FAR struct eth_hdr_s *)priv->dev.d_buf;

#ifdef CONFIG_NET_IPv4
          if (eth->type == HTONS(ETHTYPE_IP))
            {
              NETDEV_RXIPV4(&priv->dev);
              (void)ipv4_input(&priv->dev);
            }
          else
#endif
#ifdef CONFIG_NET_IPv6
          if (eth->type == HTONS(ETHTYPE_IP6))
            {
              NETDEV_RXIPV6(&priv->dev);
              (void)ipv6_input(&priv->dev);
            }
          else
#endif
#ifdef CONFIG_NET_ARP
          if (eth->type == HTONS(ETHTYPE_ARP))
            {
              NETDEV_RXARP(&priv->dev);
              arp_input(&priv->dev);
            }
          else
#endif
            {
              NETDEV_RXDROPPED(&priv->dev);
            }
        }

      net_unlock();
    }
}

static int32_t bk7258_eth_phy_read(uint32_t addr, uint32_t reg,
                                   uint32_t *pval)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;

  if (HAL_ETH_ReadPHYRegister(&priv->heth, addr, reg, pval) != HAL_OK)
    {
      return -1;
    }

  return 0;
}

static int32_t bk7258_eth_phy_write(uint32_t addr, uint32_t reg, uint32_t val)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;

  if (HAL_ETH_WritePHYRegister(&priv->heth, addr, reg, val) != HAL_OK)
    {
      return -1;
    }

  return 0;
}

static int bk7258_eth_phy_setup(FAR struct bk7258_eth_driver_s *priv)
{
  lan8742_IOCtx_t io;
  int32_t ret;

  memset(&io, 0, sizeof(io));
  io.ReadReg = bk7258_eth_phy_read;
  io.WriteReg = bk7258_eth_phy_write;
  io.GetTick = (int32_t (*)(void))clock_systime_ticks;

  (void)LAN8742_RegisterBusIO(&priv->phy, &io);

  ret = LAN8742_Init(&priv->phy);
  if (ret < 0)
    {
      return -EIO;
    }

  ret = LAN8742_StartAutoNego(&priv->phy);
  if (ret < 0)
    {
      return -EIO;
    }

  return OK;
}

static int bk7258_eth_transmit(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_eth_driver_s *priv = dev->d_private;
  ETH_TxPacketConfig txconfig;
  ETH_BufferTypeDef txbuffer;
  HAL_StatusTypeDef hret;

  if (!priv->ifup)
    {
      return -ENETDOWN;
    }

  memset(&txconfig, 0, sizeof(txconfig));
  memset(&txbuffer, 0, sizeof(txbuffer));

  txbuffer.buffer = dev->d_buf;
  txbuffer.len    = dev->d_len;
  txbuffer.next   = NULL;

  txconfig.Length       = dev->d_len;
  txconfig.TxBuffer    = &txbuffer;
  txconfig.SrcAddrCtrl = ETH_TX_PACKETS_FEATURES_SAIC;
  txconfig.CRCPadCtrl  = ETH_CRC_PAD_INSERT;
  txconfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT;

  hret = HAL_ETH_Transmit(&priv->heth, &txconfig, BK7258_ETH_TX_TIMEOUT_MS);
  if (hret != HAL_OK)
    {
      NETDEV_TXERRORS(dev);
      return -EIO;
    }

  NETDEV_TXPACKETS(dev);
  NETDEV_TXDONE(dev);
  return OK;
}

static int bk7258_eth_txpoll(FAR struct net_driver_s *dev)
{
  return bk7258_eth_transmit(dev) < 0 ? 1 : 0;
}

static void bk7258_eth_txavail_work(FAR void *arg)
{
  FAR struct bk7258_eth_driver_s *priv = arg;

  net_lock();
  if (priv->registered && priv->ifup)
    {
      devif_poll(&priv->dev, bk7258_eth_txpoll);
    }

  net_unlock();
}

static int bk7258_eth_txavail(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_eth_driver_s *priv = dev->d_private;

  if (work_available(&priv->pollwork))
    {
      (void)work_queue(LPWORK, &priv->pollwork, bk7258_eth_txavail_work,
                       priv, 0);
    }

  return OK;
}

static void bk7258_eth_link_work(FAR void *arg)
{
  FAR struct bk7258_eth_driver_s *priv = arg;
  int32_t link;

  link = LAN8742_GetLinkState(&priv->phy);
  if (link != LAN8742_STATUS_LINK_DOWN && !priv->ifup)
    {
      net_lock();
      priv->ifup = true;
      (void)HAL_ETH_Start_IT(&priv->heth);
      net_unlock();
    }

  if (work_available(&priv->linkwork))
    {
      (void)work_queue(LPWORK, &priv->linkwork, bk7258_eth_link_work, priv,
                       BK7258_ETH_LINK_CHECK_MS);
    }
}

static int bk7258_eth_ifup(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_eth_driver_s *priv = dev->d_private;
  uint8_t mac[6];
  HAL_StatusTypeDef hret;
  int ret;

  memset(&priv->heth, 0, sizeof(priv->heth));
  memcpy(mac, priv->board->mac_addr, sizeof(mac));
  priv->heth.Instance         = (FAR ETH_TypeDef *)ETH_BASE;
  priv->heth.Init.MACAddr     = mac;
  priv->heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  priv->heth.Init.TxDesc      = g_eth_tx_desc;
  priv->heth.Init.RxDesc      = g_eth_rx_desc;
  priv->heth.Init.RxBuffLen   = BK7258_ETH_RX_BUF_SIZE;

  (void)HAL_ETH_RegisterRxAllocateCallback(&priv->heth,
                                           bk7258_eth_rx_allocate);
  (void)HAL_ETH_RegisterRxLinkCallback(&priv->heth, bk7258_eth_rx_link);

  hret = HAL_ETH_Init(&priv->heth);
  if (hret != HAL_OK)
    {
      return -EIO;
    }

  ret = bk7258_eth_phy_setup(priv);
  if (ret < 0)
    {
      (void)HAL_ETH_DeInit(&priv->heth);
      return ret;
    }

  (void)work_queue(LPWORK, &priv->linkwork, bk7258_eth_link_work, priv,
                   BK7258_ETH_LINK_CHECK_MS);

  return OK;
}

static int bk7258_eth_ifdown(FAR struct net_driver_s *dev)
{
  FAR struct bk7258_eth_driver_s *priv = dev->d_private;

  work_cancel_sync(LPWORK, &priv->linkwork);

  net_lock();
  priv->ifup = false;
  (void)HAL_ETH_Stop_IT(&priv->heth);
  (void)HAL_ETH_DeInit(&priv->heth);
  net_unlock();

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_eth_initialize(FAR const struct bk7258_eth_board_s *board)
{
  FAR struct bk7258_eth_driver_s *priv = &g_eth;
  int ret;

  if (priv->registered)
    {
      return -EBUSY;
    }

  if (board == NULL || board->name == NULL)
    {
      return -EINVAL;
    }

  priv->board = board;
  memset(&priv->dev, 0, sizeof(priv->dev));
  priv->dev.d_buf      = priv->tx_buf;
  priv->dev.d_ifup     = bk7258_eth_ifup;
  priv->dev.d_ifdown   = bk7258_eth_ifdown;
  priv->dev.d_txavail  = bk7258_eth_txavail;
  priv->dev.d_private  = priv;
  strlcpy(priv->dev.d_ifname, "eth%d", IFNAMSIZ);
  memcpy(priv->dev.d_mac.ether.ether_addr_octet, board->mac_addr, 6);

  ret = netdev_register(&priv->dev, NET_LL_ETHERNET);
  if (ret < 0)
    {
      return ret;
    }

  priv->registered = true;

  syslog(LOG_INFO, "BK7258 ETH: ready %s (group%d)\n", priv->dev.d_ifname,
         board->pin_group);
  return OK;
}

/****************************************************************************
 * NuttX architecture network hook.  The kernel calls arm_netinitialize()
 * from up_initialize() when CONFIG_NET is enabled; it must exist for the
 * link to succeed even though this board has no built-in ethernet PHY.
 ****************************************************************************/

void arm_netinitialize(void)
{
  /* Physical PHY wiring is unavailable at this architecture hook.  The
   * selected board registers eth0 later with bk7258_eth_initialize().
   */
}

#endif /* CONFIG_BK7258_ETH */
