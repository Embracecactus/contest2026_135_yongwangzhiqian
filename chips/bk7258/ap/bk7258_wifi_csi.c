/****************************************************************************
 * chips/bk7258/ap/bk7258_wifi_csi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded raw CSI capture. SDK callbacks only copy into a single-producer
 * ring; the existing logical-CPU0 Wi-Fi worker owns SDK calls and UDP I/O.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <nuttx/clock.h>
#include <nuttx/net/net.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_radio_mode.h>
#include <arch/chip/bk7258_wifi.h>
#include <arch/chip/bk7258_wifi_csi.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Private v3.1.1.9 ABI, isolated from vendor config/lwIP headers. The union
 * is aligned like the SDK's double IQ storage, but only len raw words leave
 * this adapter. Never export the unused union tail or structure padding.
 */

struct bk7258_csi_sdk_rx_s
{
  int8_t rssi;
  uint8_t rate;
  uint8_t format;
  uint8_t mcs;
  uint8_t bandwidth;
  uint8_t aggregation;
  uint8_t stbc;
  uint8_t fec;
  uint8_t sgi;
  uint8_t channel;
  uint8_t secondary_channel;
  uint8_t sig_len;
  uint32_t timestamp;
};

struct bk7258_csi_sdk_info_s
{
  struct bk7258_csi_sdk_rx_s rx;
  uint8_t mac[6];
  uint16_t len;
  uint16_t data_type;
  union
  {
    uint32_t words[BK7258_CSI_MAX_SAMPLES];
    double iq[2 * BK7258_CSI_MAX_SAMPLES];
  } data;
};

static_assert(offsetof(struct bk7258_csi_sdk_info_s, len) == 22,
               "CSI SDK length ABI changed");
static_assert(offsetof(struct bk7258_csi_sdk_info_s, data) == 32,
               "CSI SDK data ABI changed");
static_assert(sizeof(struct bk7258_csi_sdk_info_s) == 864,
               "CSI SDK structure ABI changed");
static_assert(offsetof(struct bk7258_wifi_csi_packet_s, words) ==
               BK7258_CSI_HEADER_SIZE, "BCSI header ABI changed");

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

extern void bk_wifi_csi_info_cb_register(
  void (*callback)(struct bk7258_csi_sdk_info_s *info));
extern int bk_wifi_csi_start_req(uint8_t type, uint8_t mode,
                                uint8_t identity, uint8_t format,
                                uint32_t interval, uint32_t delay);
extern int bk_wifi_csi_stop_req(void);
extern int bk_wifi_sta_pm_disable(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct
{
  struct bk7258_wifi_csi_packet_s queue[BK7258_CSI_QUEUE_SIZE];
  struct bk7258_wifi_csi_result_s result;
  struct socket socket;
  struct sockaddr_in peer;
  uint32_t head;
  uint32_t tail;
  uint32_t accepting;
  uint32_t in_callback;
  clock_t started;
  clock_t retry_at;
  uint32_t duration_ms;
} g_csi;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_csi_callback(struct bk7258_csi_sdk_info_s *info)
{
  struct bk7258_wifi_csi_packet_s *packet;
  uint32_t sequence;
  uint32_t head;
  uint32_t tail;

  __atomic_add_fetch(&g_csi.in_callback, 1u, __ATOMIC_ACQ_REL);
  if (__atomic_load_n(&g_csi.accepting, __ATOMIC_ACQUIRE) == 0)
    {
      goto out;
    }

  sequence = __atomic_add_fetch(&g_csi.result.callbacks, 1u,
                                __ATOMIC_RELAXED);
  if (info == NULL)
    {
      __atomic_add_fetch(&g_csi.result.invalid, 1u, __ATOMIC_RELAXED);
      goto out;
    }

  __atomic_store_n(&g_csi.result.last_len, info->len, __ATOMIC_RELAXED);
  __atomic_store_n(&g_csi.result.last_type, info->data_type,
                    __ATOMIC_RELAXED);
  if (info->len == 0 || info->len > BK7258_CSI_MAX_SAMPLES ||
      info->data_type != 0)
    {
      __atomic_add_fetch(&g_csi.result.invalid, 1u, __ATOMIC_RELAXED);
      goto out;
    }

  head = __atomic_load_n(&g_csi.head, __ATOMIC_RELAXED);
  tail = __atomic_load_n(&g_csi.tail, __ATOMIC_ACQUIRE);
  if (head - tail >= BK7258_CSI_QUEUE_SIZE)
    {
      __atomic_add_fetch(&g_csi.result.queue_drops, 1u, __ATOMIC_RELAXED);
      goto out;
    }

  packet = &g_csi.queue[head % BK7258_CSI_QUEUE_SIZE];
  memset(packet, 0, BK7258_CSI_HEADER_SIZE);
  packet->sequence = sequence;
  packet->timestamp_us = info->rx.timestamp;
  packet->count = info->len;
  packet->data_type = info->data_type;
  packet->rssi = info->rx.rssi;
  packet->channel = info->rx.channel;
  packet->format = info->rx.format;
  packet->bandwidth = info->rx.bandwidth;
  memcpy(packet->mac, info->mac, sizeof(packet->mac));
  packet->mcs = info->rx.mcs;
  packet->rate = info->rx.rate;
  memcpy(packet->words, info->data.words,
         info->len * sizeof(packet->words[0]));
  __atomic_store_n(&g_csi.head, head + 1u, __ATOMIC_RELEASE);
out:
  __atomic_sub_fetch(&g_csi.in_callback, 1u, __ATOMIC_RELEASE);
}

static int bk7258_csi_send(struct bk7258_wifi_csi_packet_s *packet,
                          uint16_t kind)
{
  size_t length = BK7258_CSI_HEADER_SIZE +
                  packet->count * sizeof(packet->words[0]);
  ssize_t ret;

  packet->magic = BK7258_CSI_MAGIC;
  packet->version = BK7258_CSI_VERSION;
  packet->kind = kind;
  packet->session = g_csi.result.session;
  packet->callbacks = __atomic_load_n(&g_csi.result.callbacks,
                                      __ATOMIC_ACQUIRE);
  packet->invalid = __atomic_load_n(&g_csi.result.invalid,
                                    __ATOMIC_ACQUIRE);
  packet->queue_drops = __atomic_load_n(&g_csi.result.queue_drops,
                                        __ATOMIC_ACQUIRE);
  packet->sent = g_csi.result.sent + (kind == 1 ? 1u : 0u);
  packet->send_errors = g_csi.result.send_errors;
  packet->active = __atomic_load_n(&g_csi.accepting, __ATOMIC_ACQUIRE);
  packet->uptime_ms = TICK2MSEC(clock_systime_ticks() - g_csi.started);
  ret = psock_sendto(&g_csi.socket, packet, length, MSG_DONTWAIT,
                     (const struct sockaddr *)&g_csi.peer,
                     sizeof(g_csi.peer));
  if (ret != (ssize_t)length)
    {
      g_csi.result.send_errors++;
      g_csi.result.last_error = ret < 0 ? (int)ret : -EIO;
      return ret < 0 ? (int)ret : -EIO;
    }

  if (kind == 1)
    {
      g_csi.result.sent++;
    }

  return OK;
}

static void bk7258_csi_status_packet(void)
{
  struct bk7258_wifi_csi_packet_s packet;

  memset(&packet, 0, sizeof(packet));
  packet.sequence = __atomic_load_n(&g_csi.result.callbacks,
                                     __ATOMIC_ACQUIRE);
  (void)bk7258_csi_send(&packet, 2);
}

static void bk7258_csi_drain(void)
{
  uint32_t tail;
  unsigned int count;

  for (count = 0; count < BK7258_CSI_QUEUE_SIZE; count++)
    {
      tail = __atomic_load_n(&g_csi.tail, __ATOMIC_RELAXED);
      if (tail == __atomic_load_n(&g_csi.head, __ATOMIC_ACQUIRE))
        {
          break;
        }

      (void)bk7258_csi_send(
        &g_csi.queue[tail % BK7258_CSI_QUEUE_SIZE], 1);
      __atomic_store_n(&g_csi.tail, tail + 1u, __ATOMIC_RELEASE);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint32_t bk7258_wifi_csi_active(void)
{
  return g_csi.result.active;
}

void bk7258_wifi_csi_snapshot(struct bk7258_wifi_csi_result_s *result)
{
  memset(result, 0, sizeof(*result));
  result->active = g_csi.result.active;
  result->session = g_csi.result.session;
  result->sent = g_csi.result.sent;
  result->send_errors = g_csi.result.send_errors;
  result->last_error = g_csi.result.last_error;
  result->callbacks = __atomic_load_n(&g_csi.result.callbacks,
                                      __ATOMIC_ACQUIRE);
  result->invalid = __atomic_load_n(&g_csi.result.invalid, __ATOMIC_ACQUIRE);
  result->queue_drops = __atomic_load_n(&g_csi.result.queue_drops,
                                        __ATOMIC_ACQUIRE);
  result->last_len = __atomic_load_n(&g_csi.result.last_len,
                                     __ATOMIC_ACQUIRE);
  result->last_type = __atomic_load_n(&g_csi.result.last_type,
                                      __ATOMIC_ACQUIRE);
  result->queued = __atomic_load_n(&g_csi.head, __ATOMIC_ACQUIRE) -
                   __atomic_load_n(&g_csi.tail, __ATOMIC_ACQUIRE);
}

int bk7258_wifi_csi_stop(void)
{
  int ret;
  unsigned int retry;

  if (g_csi.result.active == 0)
    {
      return OK;
    }

  __atomic_store_n(&g_csi.accepting, 0u, __ATOMIC_RELEASE);
  for (retry = 0; retry < 100; retry++)
    {
      if (__atomic_load_n(&g_csi.in_callback, __ATOMIC_ACQUIRE) == 0)
        {
          break;
        }

      nxsig_usleep(1000);
    }

  if (__atomic_load_n(&g_csi.in_callback, __ATOMIC_ACQUIRE) != 0)
    {
      g_csi.result.last_error = -EBUSY;
      return -EBUSY;
    }

  bk7258_csi_drain();
  /* Finish the data stream while the same working controller/TX path is
   * still available. The wire active flag describes the acceptance gate;
   * the control result stays active until hardware cleanup succeeds.
   */

  for (retry = 0; retry < 3; retry++)
    {
      bk7258_csi_status_packet();
      nxsig_usleep(20000);
    }

  ret = bk_wifi_csi_stop_req();
  if (ret != OK)
    {
      g_csi.result.last_error = ret < 0 ? ret : -EIO;
      return g_csi.result.last_error;
    }

  bk_wifi_csi_info_cb_register(NULL);
  g_csi.result.active = 0;
  (void)psock_close(&g_csi.socket);
  ret = bk7258_radio_mode_release(BK7258_RADIO_MODE_WIFI_CSI);
  g_csi.result.last_error = ret;
  return ret;
}

int bk7258_wifi_csi_start(const struct bk7258_wifi_csi_config_s *config)
{
  struct bk7258_wifi_result_s link;
  struct linger linger;
  uint32_t session;
  int ret;

  if (config == NULL || config->address == 0 ||
      config->address == UINT32_MAX || config->port == 0 ||
      config->interval_ms < 10 || config->interval_ms > 1000 ||
      config->duration_ms < 1000 ||
      config->duration_ms > BK7258_CSI_MAX_DURATION ||
      config->format < 1 || config->format > 3)
    {
      return -EINVAL;
    }

  if (g_csi.result.active != 0 ||
      __atomic_load_n(&g_csi.in_callback, __ATOMIC_ACQUIRE) != 0)
    {
      return -EBUSY;
    }

  ret = bk7258_wifi_read_link(&link);
  if (ret < 0 || link.link_state != BK7258_WIFI_LINK_CONNECTED)
    {
      return ret < 0 ? ret : -ENETDOWN;
    }

  ret = bk7258_radio_mode_acquire(BK7258_RADIO_MODE_WIFI_CSI);
  if (ret < 0)
    {
      return ret;
    }

  ret = psock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &g_csi.socket);
  if (ret < 0)
    {
      (void)bk7258_radio_mode_release(BK7258_RADIO_MODE_WIFI_CSI);
      return ret;
    }

  /* Buffered UDP close otherwise waits indefinitely for a lost TX path.
   * Keep controller shutdown bounded even if the peer or radio disappears.
   */

  linger.l_onoff = 1;
  linger.l_linger = 1;
  ret = psock_setsockopt(&g_csi.socket, SOL_SOCKET, SO_LINGER,
                         &linger, sizeof(linger));
  if (ret < 0)
    {
      (void)psock_close(&g_csi.socket);
      (void)bk7258_radio_mode_release(BK7258_RADIO_MODE_WIFI_CSI);
      return ret;
    }

  memset(&g_csi.peer, 0, sizeof(g_csi.peer));
  g_csi.peer.sin_family = AF_INET;
  g_csi.peer.sin_port = htons(config->port);
  g_csi.peer.sin_addr.s_addr = config->address;
  session = g_csi.result.session + 1u;
  memset(&g_csi.result, 0, sizeof(g_csi.result));
  g_csi.result.session = session == 0 ? 1 : session;
  g_csi.result.active = 1;
  g_csi.duration_ms = config->duration_ms;
  g_csi.started = clock_systime_ticks();
  g_csi.retry_at = g_csi.started;
  __atomic_store_n(&g_csi.head, 0u, __ATOMIC_RELEASE);
  __atomic_store_n(&g_csi.tail, 0u, __ATOMIC_RELEASE);

  /* CSI observes individual frames and must not inherit STA doze intervals.
   * This opt-in diagnostic owns the association until it disconnects or
   * reboots; the normal product never enables this capture configuration.
   */

  ret = bk_wifi_sta_pm_disable();
  if (ret != OK)
    {
      g_csi.result.active = 0;
      g_csi.result.last_error = ret < 0 ? ret : -EIO;
      (void)psock_close(&g_csi.socket);
      (void)bk7258_radio_mode_release(BK7258_RADIO_MODE_WIFI_CSI);
      return g_csi.result.last_error;
    }

  /* Resolve the peer before switching the controller's CSI receive path.
   * A failed status send stays visible in the layered transport counters.
   */

  __atomic_store_n(&g_csi.accepting, 1u, __ATOMIC_RELEASE);
  bk7258_csi_status_packet();
  bk_wifi_csi_info_cb_register(bk7258_csi_callback);

  /* Mode bit 0 delivers raw callbacks; bit 1 prints every CSI word. Keep
   * printing and the vendor motion algorithm disabled in this diagnostic.
   */

  ret = bk_wifi_csi_start_req(0, 1, 1, config->format,
                              config->interval_ms, 0);
  if (ret != OK)
    {
      int stop_ret = bk7258_wifi_csi_stop();

      g_csi.result.last_error = ret < 0 ? ret : -EIO;
      return stop_ret < 0 ? stop_ret : g_csi.result.last_error;
    }

  bk7258_csi_status_packet();
  return OK;
}

void bk7258_wifi_csi_poll(void)
{
  clock_t now;

  if (g_csi.result.active == 0)
    {
      return;
    }

  bk7258_csi_drain();
  now = clock_systime_ticks();
  if (TICK2MSEC(now - g_csi.started) >= g_csi.duration_ms &&
      TICK2MSEC(now - g_csi.retry_at) >= 1000)
    {
      (void)bk7258_wifi_csi_stop();
      g_csi.retry_at = now;
    }
}
