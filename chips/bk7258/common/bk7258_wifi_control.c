/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/
 * bk7258_wifi_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal CP-to-AP Wi-Fi STA control plane.  CP supplies ephemeral runtime
 * credentials; AP logical CPU0 calls the official v3.1.1.9 proxy and applies
 * the CP VNET lease to native NuttX wlan0.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_WIFI_VNET

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#ifdef CONFIG_BK7258_AP_CORE
#  include <sys/ioctl.h>
#  include <sys/poll.h>
#  include <sys/socket.h>
#  include <sys/time.h>

#  include <arpa/inet.h>
#  include <netinet/in.h>

#  include <nuttx/net/icmp.h>
#  include <nuttx/net/ip.h>
#  include <nuttx/net/net.h>

#endif

#include <arch/chip/bk7258_rptun.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_wifi.h>

#if defined(CONFIG_BK7258_WIFI_PACKET_DIAG) && \
    !defined(CONFIG_BK7258_AP_CORE)
#  include "bk7258_wifi_packet_diag.h"
#endif

#include "bk7258_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WIFI_CONTROL_EPT_NAME          "bk7258-wifi"
#define BK7258_WIFI_CONTROL_MAGIC             0x49465742u /* "BWFI" */
#define BK7258_WIFI_CONTROL_VERSION           2u
#define BK7258_WIFI_CONTROL_SEND_TIMEOUT_MS   500u
#define BK7258_WIFI_CONTROL_ENDPOINT_WAIT_MS  3000u
#define BK7258_WIFI_CONTROL_POLL_MS            100u
#define BK7258_WIFI_LINK_SYNC_MS               250u
#define BK7258_WIFI_SECURITY_AUTO             12
#define BK7258_WIFI_STA_STOP_COMMAND           0x312u
#define BK7258_WIFI_VENDOR_TIMEOUT             (-0x1006)
#define BK7258_WIFI_STOP_BARRIER_MS            8000u
#define BK7258_WIFI_PING_DATALEN              32u
#define BK7258_WIFI_PING_REPLY_SIZE           128u

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_WIFI_CONTROL_REMOTE_NAME     "cp"
#else
#  define BK7258_WIFI_CONTROL_REMOTE_NAME     "ap"
#endif

enum bk7258_wifi_control_command_e
{
  BK7258_WIFI_CONTROL_COMMAND_REQUEST = 1,
  BK7258_WIFI_CONTROL_COMMAND_REPORT
};

enum bk7258_wifi_link_state_e
{
  BK7258_WIFI_LINK_IDLE = 0,
  BK7258_WIFI_LINK_CONNECTING,
  BK7258_WIFI_LINK_DISCONNECTED,
  BK7258_WIFI_LINK_CONNECTED,
  BK7258_WIFI_LINK_CONNECT_FAILED
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_wifi_control_wire_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t operation;
  uint32_t timeout_ms;
  uint32_t ssid_len;
  uint32_t password_len;
  struct bk7258_wifi_echo_s echo;
  struct bk7258_wifi_result_s result;
  char ssid[BK7258_WIFI_SSID_MAX_LEN + 1u];
  char password[BK7258_WIFI_PASSWORD_MAX_LEN + 1u];
};

struct bk7258_wifi_control_dev_s
{
  struct rpmsg_endpoint ept;
  bool initialized;
  bool endpoint_created;
  int connection_error;
#ifdef CONFIG_BK7258_AP_CORE
  sem_t request_sem;
  bool abort;
  bool busy;
  bool native_link_valid;
  struct bk7258_wifi_result_s native_link;
  struct bk7258_wifi_control_wire_s request;
#else
  sem_t report_sem;
  bool report_valid;
  uint32_t waiting_generation;
  uint32_t waiting_sequence;
  struct bk7258_wifi_result_s report;
#endif
};

#ifdef CONFIG_BK7258_AP_CORE
/* Exact structures consumed by the immutable v3.1.1.9 AP Wi-Fi archive.
 * They stay private so vendor lwIP/config headers cannot redefine NuttX
 * CONFIG_* symbols in this translation unit.
 */

struct bk7258_wifi_sta_config_s
{
  char ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  uint8_t security;
  char password[65];
  uint8_t psk[65];
  uint8_t ip_addr[4];
  uint8_t netmask[4];
  uint8_t gateway[4];
  uint8_t dns1[4];
  uint8_t no_auto_fci;
  uint8_t user_fast_connect;
  uint8_t pmf;
  uint8_t tk[16];
  int auto_reconnect_count;
  int auto_reconnect_timeout;
  bool disable_auto_reconnect;
  FAR void *vsies[2];
  uint8_t reserved[32];
};

/* The official archive is compiled with -fshort-enums.  Keep the enum-backed
 * fields byte-sized even though this wrapper intentionally avoids importing
 * the SDK headers into a NuttX translation unit.  The offsets and copy sizes
 * below are also visible in the immutable archive's API implementation.
 */

_Static_assert(offsetof(struct bk7258_wifi_sta_config_s, security) == 40,
               "v3.1.1.9 STA security offset changed");
_Static_assert(offsetof(struct bk7258_wifi_sta_config_s, password) == 41,
               "v3.1.1.9 STA password offset changed");
_Static_assert(sizeof(struct bk7258_wifi_sta_config_s) == 260,
               "v3.1.1.9 STA config ABI changed");
#endif

_Static_assert(sizeof(struct bk7258_wifi_control_wire_s) <=
               BK7258_RPTUN_BUFFER_SIZE - 16u,
               "Wi-Fi control message exceeds one RPMsg buffer");

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
extern int bk_wifi_sta_set_config(
  FAR const struct bk7258_wifi_sta_config_s *config);
extern int bk_wifi_sta_start(void);
extern int bk_wifi_sta_stop(void);
extern int wifi_send_com_api_cmd(uint32_t command, uint32_t argc, ...);

static int bk7258_wifi_sync_native_link(
  FAR struct bk7258_wifi_result_s *result);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_wifi_control_dev_s g_bk7258_wifi_control;

#ifndef CONFIG_BK7258_AP_CORE
static mutex_t g_bk7258_wifi_control_lock = NXMUTEX_INITIALIZER;
static uint32_t g_bk7258_wifi_control_sequence;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
static bool bk7258_wifi_control_generation_ready(uint32_t generation)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  return generation != 0 &&
         control->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         control->version == BK7258_RPTUN_CONTROL_VERSION &&
         control->generation == generation;
}
#endif

static bool bk7258_wifi_control_endpoint_ready(void)
{
  struct bk7258_wifi_control_dev_s *priv = &g_bk7258_wifi_control;

  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         priv->ept.rdev != NULL && priv->connection_error >= 0;
}

static int bk7258_wifi_control_send_bounded(
  FAR const struct bk7258_wifi_control_wire_s *message)
{
  struct bk7258_wifi_control_dev_s *priv = &g_bk7258_wifi_control;
  uint32_t waited = 0;
  int ret;

  do
    {
      if (!bk7258_wifi_control_endpoint_ready())
        {
          return -ENOTCONN;
        }

      ret = rpmsg_trysend(&priv->ept, message, sizeof(*message));
      if (ret != -ENOMEM && ret != -EAGAIN)
        {
          return ret;
        }

      nxsig_usleep(1000);
      waited++;
    }
  while (waited < BK7258_WIFI_CONTROL_SEND_TIMEOUT_MS);

  return -ETIMEDOUT;
}

#ifdef CONFIG_BK7258_AP_CORE
struct bk7258_wifi_ping_packet_s
{
  struct icmp_hdr_s header;
  uint8_t payload[BK7258_WIFI_PING_DATALEN];
};

static int bk7258_wifi_vendor_result(int ret)
{
  return ret == 0 ? OK : (ret < 0 ? ret : -EIO);
}

static int bk7258_wifi_stop_sta(void)
{
  clock_t started;
  int ret;

  /* The official AP proxy returns success even when its two-second
   * STA_STOP confirmation wait expires.  Repeat the same no-argument SDK
   * command as a completion barrier: once it is confirmed, every earlier
   * stop request has completed on the serial CP control worker.  No pointer
   * argument is queued here, so a late confirmation cannot outlive an AP
   * buffer.
   */

  (void)bk_wifi_sta_stop();
  started = clock_systime_ticks();
  for (;;)
    {
      ret = bk7258_wifi_vendor_result(
        wifi_send_com_api_cmd(BK7258_WIFI_STA_STOP_COMMAND, 0));
      if (ret == OK)
        {
          /* Let any already-queued indication reach the AP RX worker before
           * retiring the immutable archive's stale link snapshot.
           */

          nxsig_usleep(BK7258_WIFI_LINK_SYNC_MS * 1000u);
          return bk7258_wifi_retire_link();
        }

      if (ret != BK7258_WIFI_VENDOR_TIMEOUT)
        {
          return ret;
        }

      if ((clock_systime_ticks() - started) >=
          MSEC2TICK(BK7258_WIFI_STOP_BARRIER_MS))
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(BK7258_WIFI_CONTROL_POLL_MS * 1000u);
    }
}

static uint16_t bk7258_wifi_icmp_checksum(FAR const void *buffer,
                                          size_t length)
{
  FAR const uint16_t *word = buffer;
  uint32_t sum = 0;

  while (length > 1u)
    {
      sum += *word++;
      length -= 2u;
    }

  if (length != 0u)
    {
      sum += *(FAR const uint8_t *)word;
    }

  while ((sum >> 16) != 0u)
    {
      sum = (sum & UINT16_MAX) + (sum >> 16);
    }

  return (uint16_t)~sum;
}

static int bk7258_wifi_apply_native_lease(
  FAR const struct bk7258_wifi_result_s *result)
{
  /* CP owns DHCP in the official VNET flow.  Copy that one lease into the
   * native NuttX interface; never start a second DHCP client on AP.
   */

  return bk7258_wifi_set_native_lease(result);
}

static int bk7258_wifi_sync_native_link(
  FAR struct bk7258_wifi_result_s *result)
{
  FAR struct bk7258_wifi_control_dev_s *priv = &g_bk7258_wifi_control;
  int ret;

  ret = bk7258_wifi_read_link(result);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->native_link_valid &&
      priv->native_link.link_state == result->link_state &&
      priv->native_link.ipaddr == result->ipaddr &&
      priv->native_link.netmask == result->netmask &&
      priv->native_link.router == result->router)
    {
      return OK;
    }

  if (result->link_state == BK7258_WIFI_LINK_CONNECTED &&
      result->ipaddr != 0)
    {
      ret = bk7258_wifi_apply_native_lease(result);
      if (ret < 0)
        {
          return ret;
        }

      memcpy(&priv->native_link, result, sizeof(priv->native_link));
      priv->native_link_valid = true;
      return OK;
    }

  /* Keep a disconnected status query successful while removing the stale
   * route/carrier from the native interface.
   */

  (void)bk7258_wifi_clear_native_lease();
  memcpy(&priv->native_link, result, sizeof(priv->native_link));
  priv->native_link_valid = true;
  return OK;
}

static int bk7258_wifi_read_status(struct bk7258_wifi_result_s *result)
{
  /* A connected event can arrive just after a bounded CONNECT request has
   * timed out.  Synchronize the already-published CP lease here as well, so
   * the next STATUS request makes native wlan0 usable instead of reporting a
   * connected vendor link with no NuttX route.
   */

  return bk7258_wifi_sync_native_link(result);
}

static int bk7258_wifi_ping_gateway(struct bk7258_wifi_result_s *result,
                                    uint32_t timeout_ms)
{
  struct bk7258_wifi_ping_packet_s request;
  struct sockaddr_in destination;
  struct sockaddr_in source;
  struct socket psock;
  struct timeval timeout;
  FAR struct icmp_hdr_s *reply_header;
  uint8_t reply[BK7258_WIFI_PING_REPLY_SIZE];
  uint32_t filter;
  socklen_t source_length;
  size_t ip_header_length;
  uint16_t identifier;
  int close_ret;
  ssize_t length;
  int ret;
  unsigned int i;

  ret = bk7258_wifi_sync_native_link(result);
  if (ret < 0)
    {
      return ret;
    }

  if (result->link_state != BK7258_WIFI_LINK_CONNECTED ||
      result->ipaddr == 0 || result->router == 0)
    {
      return -ENETDOWN;
    }

  if (timeout_ms == 0u)
    {
      return -EINVAL;
    }

  /* The AP Wi-Fi control worker is a pthread in the initial task group.
   * Native descriptor-table lookup can contend indefinitely with that
   * group's SMP fd-list spinlock.  Use NuttX's native psock interface here:
   * it is the same ICMP stack and driver path, but owns its socket object
   * directly and therefore keeps this RPMsg request bounded.
   */

  memset(&psock, 0, sizeof(psock));
  ret = psock_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP, &psock);
  if (ret < 0)
    {
      return ret;
    }

  filter = UINT32_MAX - (1u << ICMP_ECHO_REPLY);
  ret = psock_setsockopt(&psock, SOL_RAW, ICMP_FILTER,
                         &filter, sizeof(filter));
  if (ret < 0)
    {
      goto out_close;
    }

  timeout.tv_sec = timeout_ms / MSEC_PER_SEC;
  timeout.tv_usec = (timeout_ms % MSEC_PER_SEC) * USEC_PER_MSEC;
  ret = psock_setsockopt(&psock, SOL_SOCKET, SO_SNDTIMEO,
                         &timeout, sizeof(timeout));
  if (ret < 0)
    {
      goto out_close;
    }

  ret = psock_setsockopt(&psock, SOL_SOCKET, SO_RCVTIMEO,
                         &timeout, sizeof(timeout));
  if (ret < 0)
    {
      goto out_close;
    }

  memset(&request, 0, sizeof(request));
  request.header.type = ICMP_ECHO_REQUEST;
  identifier = (uint16_t)clock_systime_ticks();
  if (identifier == 0u)
    {
      identifier = 1u;
    }

  request.header.id = htons(identifier);
  request.header.seqno = htons(1u);
  for (i = 0; i < sizeof(request.payload); i++)
    {
      request.payload[i] = (uint8_t)(0x20u + i);
    }

  request.header.icmpchksum =
    bk7258_wifi_icmp_checksum(&request, sizeof(request));

  memset(&destination, 0, sizeof(destination));
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = result->router;

  length = psock_sendto(&psock, &request, sizeof(request), 0,
                        (FAR const struct sockaddr *)&destination,
                        sizeof(destination));
  if (length < 0)
    {
      ret = (int)length;
      goto out_close;
    }
  else if (length != sizeof(request))
    {
      ret = -EIO;
      goto out_close;
    }

  memset(reply, 0, sizeof(reply));
  memset(&source, 0, sizeof(source));
  source_length = sizeof(source);
  length = psock_recvfrom(&psock, reply, sizeof(reply), 0,
                          (FAR struct sockaddr *)&source, &source_length);
  if (length < 0)
    {
      ret = length == -EAGAIN || length == -ETIMEDOUT ?
            -ETIMEDOUT : (int)length;
      goto out_close;
    }

  if (length < IPv4_HDRLEN + sizeof(struct icmp_hdr_s) ||
      source.sin_family != AF_INET ||
      source.sin_addr.s_addr != destination.sin_addr.s_addr)
    {
      ret = -EPROTO;
      goto out_close;
    }

  ip_header_length = (reply[0] & 0x0fu) * sizeof(uint32_t);
  if (ip_header_length < IPv4_HDRLEN ||
      (size_t)length < ip_header_length + sizeof(request))
    {
      ret = -EPROTO;
      goto out_close;
    }

  reply_header = (FAR struct icmp_hdr_s *)(reply + ip_header_length);
  if (reply_header->type != ICMP_ECHO_REPLY ||
      ntohs(reply_header->id) != identifier ||
      ntohs(reply_header->seqno) != 1u ||
      memcmp(reply_header + 1, request.payload,
             sizeof(request.payload)) != 0)
    {
      ret = -EPROTO;
      goto out_close;
    }

  ret = OK;

out_close:
  close_ret = psock_close(&psock);
  explicit_bzero(&request, sizeof(request));
  explicit_bzero(reply, sizeof(reply));
  return ret < 0 ? ret : close_ret;
}

static int bk7258_wifi_echo_set_timeout(FAR struct socket *psock,
                                        clock_t started,
                                        uint32_t timeout_ms)
{
  struct timeval timeout;
  uint32_t elapsed_ms;
  uint32_t remaining_ms;
  int ret;

  elapsed_ms = (uint32_t)TICK2MSEC(clock_systime_ticks() - started);
  if (elapsed_ms >= timeout_ms)
    {
      return -ETIMEDOUT;
    }

  remaining_ms = timeout_ms - elapsed_ms;
  timeout.tv_sec = remaining_ms / MSEC_PER_SEC;
  timeout.tv_usec = (remaining_ms % MSEC_PER_SEC) * USEC_PER_MSEC;
  ret = psock_setsockopt(psock, SOL_SOCKET, SO_SNDTIMEO,
                         &timeout, sizeof(timeout));
  if (ret >= 0)
    {
      ret = psock_setsockopt(psock, SOL_SOCKET, SO_RCVTIMEO,
                             &timeout, sizeof(timeout));
    }

  return ret;
}

static int bk7258_wifi_echo_socket_error(ssize_t value)
{
  if (value == -EAGAIN || value == -ETIMEDOUT
#if EWOULDBLOCK != EAGAIN
      || value == -EWOULDBLOCK
#endif
     )
    {
      return -ETIMEDOUT;
    }

  return (int)value;
}

static int bk7258_wifi_echo_connect(FAR struct socket *psock,
                                    FAR const struct sockaddr *address,
                                    socklen_t address_length,
                                    clock_t started,
                                    uint32_t timeout_ms)
{
  struct pollfd pfd;
  sem_t poll_sem;
  socklen_t error_length;
  uint32_t elapsed_ms;
  uint32_t remaining_ms;
  bool poll_setup;
  int socket_error;
  int nonblocking;
  int teardown_ret;
  int restore_ret;
  int ret;

  /* NuttX's blocking TCP connect waits with an infinite semaphore timeout;
   * SO_SNDTIMEO does not cover that wait.  Use the native psock nonblocking
   * path and its poll callback so one unreachable peer cannot wedge the sole
   * AP Wi-Fi control worker.
   */

  nonblocking = 1;
  ret = psock_ioctl(psock, FIONBIO,
                    (unsigned long)(uintptr_t)&nonblocking);
  if (ret < 0)
    {
      return ret;
    }

  ret = psock_connect(psock, address, address_length);
  if (ret == -EINPROGRESS)
    {
      memset(&pfd, 0, sizeof(pfd));
      nxsem_init(&poll_sem, 0, 0);
      pfd.fd = -1;
      pfd.events = POLLOUT | POLLERR | POLLHUP;
      pfd.arg = &poll_sem;
      pfd.cb = poll_default_cb;

      poll_setup = false;
      ret = psock_poll(psock, &pfd, true);
      if (ret >= 0)
        {
          poll_setup = true;
        }

      if (poll_setup && pfd.revents == 0)
        {
          elapsed_ms = (uint32_t)TICK2MSEC(clock_systime_ticks() -
                                           started);
          if (elapsed_ms >= timeout_ms)
            {
              ret = -ETIMEDOUT;
            }
          else
            {
              remaining_ms = timeout_ms - elapsed_ms;
              ret = nxsem_tickwait(&poll_sem,
                                   MSEC2TICK(remaining_ms));
            }
        }

      if (poll_setup)
        {
          teardown_ret = psock_poll(psock, &pfd, false);
          if (ret >= 0 && teardown_ret < 0)
            {
              ret = teardown_ret;
            }
        }

      if (ret >= 0)
        {
          socket_error = 0;
          error_length = sizeof(socket_error);
          ret = psock_getsockopt(psock, SOL_SOCKET, SO_ERROR,
                                 &socket_error, &error_length);
          if (ret >= 0 && socket_error != 0)
            {
              ret = -socket_error;
            }
          else if (ret >= 0 && (pfd.revents & POLLOUT) == 0)
            {
              ret = -ECONNABORTED;
            }
        }

      nxsem_destroy(&poll_sem);
    }

  nonblocking = 0;
  restore_ret = psock_ioctl(psock, FIONBIO,
                            (unsigned long)(uintptr_t)&nonblocking);
  return ret < 0 ? ret : restore_ret;
}

static void bk7258_wifi_echo_pattern(FAR uint8_t *buffer, size_t length,
                                     uint32_t sequence)
{
  size_t i;

  for (i = 0; i < length; i++)
    {
      buffer[i] = (uint8_t)(0x5au ^ (sequence * 17u) ^ i);
    }
}

static int bk7258_wifi_echo_exchange(
  FAR const struct bk7258_wifi_control_wire_s *request,
  FAR struct bk7258_wifi_result_s *result)
{
  struct sockaddr_in destination;
  struct sockaddr_in source;
  struct socket psock;
  uint8_t tx[BK7258_WIFI_ECHO_SIZE_MAX];
  uint8_t rx[BK7258_WIFI_ECHO_SIZE_MAX];
  clock_t started;
  socklen_t source_length;
  size_t transferred;
  ssize_t length;
  uint32_t sequence;
  int close_ret;
  int protocol;
  int type;
  int ret;

  ret = bk7258_wifi_sync_native_link(result);
  if (ret < 0)
    {
      return ret;
    }

  if (result->link_state != BK7258_WIFI_LINK_CONNECTED ||
      result->ipaddr == 0)
    {
      return -ENETDOWN;
    }

  if ((request->operation != BK7258_WIFI_OPERATION_TCP_ECHO &&
       request->operation != BK7258_WIFI_OPERATION_UDP_ECHO) ||
      request->echo.address == 0 ||
      request->echo.address == UINT32_MAX ||
      request->echo.port == 0 || request->echo.port > UINT16_MAX ||
      request->echo.count == 0 ||
      request->echo.count > BK7258_WIFI_ECHO_COUNT_MAX ||
      request->echo.size == 0 ||
      request->echo.size > BK7258_WIFI_ECHO_SIZE_MAX ||
      request->timeout_ms < BK7258_WIFI_ECHO_MIN_MS ||
      request->timeout_ms > BK7258_WIFI_CONNECT_MAX_MS)
    {
      return -EINVAL;
    }

  type = request->operation == BK7258_WIFI_OPERATION_TCP_ECHO ?
         SOCK_STREAM : SOCK_DGRAM;
  protocol = request->operation == BK7258_WIFI_OPERATION_TCP_ECHO ?
             IPPROTO_TCP : IPPROTO_UDP;
  memset(&psock, 0, sizeof(psock));
  ret = psock_socket(AF_INET, type, protocol, &psock);
  if (ret < 0)
    {
      return ret;
    }

  started = clock_systime_ticks();
  ret = bk7258_wifi_echo_set_timeout(&psock, started,
                                     request->timeout_ms);
  if (ret < 0)
    {
      goto out_close;
    }

  memset(&destination, 0, sizeof(destination));
  destination.sin_family = AF_INET;
  destination.sin_port = htons((uint16_t)request->echo.port);
  destination.sin_addr.s_addr = request->echo.address;

  if (type == SOCK_STREAM)
    {
      ret = bk7258_wifi_echo_connect(
        &psock, (FAR const struct sockaddr *)&destination,
        sizeof(destination), started, request->timeout_ms);
      if (ret < 0)
        {
          goto out_close;
        }
    }

  result->echo_count = 0;
  result->echo_bytes = 0;
  for (sequence = 0; sequence < request->echo.count; sequence++)
    {
      bk7258_wifi_echo_pattern(tx, request->echo.size, sequence);
      memset(rx, 0, request->echo.size);

      if (type == SOCK_STREAM)
        {
          transferred = 0;
          while (transferred < request->echo.size)
            {
              ret = bk7258_wifi_echo_set_timeout(&psock, started,
                                                  request->timeout_ms);
              if (ret < 0)
                {
                  goto out_close;
                }

              length = psock_send(&psock, tx + transferred,
                                  request->echo.size - transferred, 0);
              if (length <= 0)
                {
                  ret = length == 0 ? -EPIPE :
                        bk7258_wifi_echo_socket_error(length);
                  goto out_close;
                }

              transferred += (size_t)length;
            }

          transferred = 0;
          while (transferred < request->echo.size)
            {
              ret = bk7258_wifi_echo_set_timeout(&psock, started,
                                                  request->timeout_ms);
              if (ret < 0)
                {
                  goto out_close;
                }

              length = psock_recv(&psock, rx + transferred,
                                  request->echo.size - transferred, 0);
              if (length <= 0)
                {
                  ret = length == 0 ? -ECONNRESET :
                        bk7258_wifi_echo_socket_error(length);
                  goto out_close;
                }

              transferred += (size_t)length;
            }
        }
      else
        {
          ret = bk7258_wifi_echo_set_timeout(&psock, started,
                                              request->timeout_ms);
          if (ret < 0)
            {
              goto out_close;
            }

          length = psock_sendto(&psock, tx, request->echo.size, 0,
                                (FAR const struct sockaddr *)&destination,
                                sizeof(destination));
          if (length < 0)
            {
              ret = bk7258_wifi_echo_socket_error(length);
              goto out_close;
            }
          else if ((size_t)length != request->echo.size)
            {
              ret = -EIO;
              goto out_close;
            }

          ret = bk7258_wifi_echo_set_timeout(&psock, started,
                                              request->timeout_ms);
          if (ret < 0)
            {
              goto out_close;
            }

          memset(&source, 0, sizeof(source));
          source_length = sizeof(source);
          length = psock_recvfrom(&psock, rx, request->echo.size, 0,
                                  (FAR struct sockaddr *)&source,
                                  &source_length);
          if (length < 0)
            {
              ret = bk7258_wifi_echo_socket_error(length);
              goto out_close;
            }
          else if ((size_t)length != request->echo.size ||
                   source.sin_family != AF_INET ||
                   source.sin_port != destination.sin_port ||
                   source.sin_addr.s_addr != destination.sin_addr.s_addr)
            {
              ret = -EPROTO;
              goto out_close;
            }
        }

      if (memcmp(tx, rx, request->echo.size) != 0)
        {
          ret = -EBADMSG;
          goto out_close;
        }

      result->echo_count++;
      result->echo_bytes += request->echo.size;
    }

  ret = OK;

out_close:
  close_ret = psock_close(&psock);
  explicit_bzero(tx, sizeof(tx));
  explicit_bzero(rx, sizeof(rx));
  return ret < 0 ? ret : close_ret;
}

static int bk7258_wifi_connect(
  FAR const struct bk7258_wifi_control_wire_s *request,
  FAR struct bk7258_wifi_result_s *result)
{
  struct bk7258_wifi_sta_config_s config;
  clock_t started;
  bool started_sta = false;
  int ret;

  if (request->ssid_len == 0 ||
      request->ssid_len > BK7258_WIFI_SSID_MAX_LEN ||
      request->password_len > BK7258_WIFI_PASSWORD_MAX_LEN ||
      request->ssid[request->ssid_len] != '\0' ||
      request->password[request->password_len] != '\0')
    {
      return -EINVAL;
    }

#if defined(CONFIG_BK7258_WIFI_PACKET_DIAG) && \
    !defined(CONFIG_BK7258_AP_CORE)
  bk7258_wifi_packet_diag_reset();
#endif

  memset(&config, 0, sizeof(config));
  memcpy(config.ssid, request->ssid, request->ssid_len);
  memcpy(config.password, request->password, request->password_len);
  config.security = BK7258_WIFI_SECURITY_AUTO;

  /* The official v3.1.1.9 API requires stop-before-restart.  In particular,
   * bk_wifi_sta_start() returns success without issuing another controller
   * command when STA is already marked started.  Always stop first so a
   * failed password attempt cannot prevent the next runtime credential set
   * from taking effect.
   */

  started = clock_systime_ticks();
  ret = bk7258_wifi_stop_sta();
  if (ret < 0)
    {
      explicit_bzero(&config, sizeof(config));
      return ret;
    }

  /* A previously connected indication is asynchronous to STA_STOP.  Wait
   * until it is withdrawn before starting the new generation; otherwise an
   * old lease could make the new CONNECT request report a false success.
   */

  for (;;)
    {
      ret = bk7258_wifi_read_link(result);
      if (ret == OK &&
          result->link_state != BK7258_WIFI_LINK_CONNECTED)
        {
          break;
        }

      if ((clock_systime_ticks() - started) >=
          MSEC2TICK(request->timeout_ms))
        {
          explicit_bzero(&config, sizeof(config));
          return -ETIMEDOUT;
        }

      nxsig_usleep(BK7258_WIFI_CONTROL_POLL_MS * 1000u);
    }

  ret = bk7258_wifi_sync_native_link(result);
  if (ret < 0)
    {
      explicit_bzero(&config, sizeof(config));
      return ret;
    }

  ret = bk7258_wifi_vendor_result(bk_wifi_sta_set_config(&config));
  explicit_bzero(&config, sizeof(config));
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_wifi_vendor_result(bk_wifi_sta_start());
  if (ret < 0)
    {
      return ret;
    }

  started_sta = true;
  for (;;)
    {
      ret = bk7258_wifi_read_link(result);
      if (ret == OK &&
          result->link_state == BK7258_WIFI_LINK_CONNECTED &&
          result->ipaddr != 0)
        {
          break;
        }

      if ((clock_systime_ticks() - started) >=
          MSEC2TICK(request->timeout_ms))
        {
          ret = -ETIMEDOUT;
          break;
        }

      nxsig_usleep(BK7258_WIFI_CONTROL_POLL_MS * 1000u);
    }

  if (ret == OK)
    {
      ret = bk7258_wifi_sync_native_link(result);
    }

  if (ret < 0 && started_sta)
    {
      /* Leave a failed attempt stopped and carrier-down.  The next CONNECT
       * can then install new runtime credentials deterministically.
       */

      (void)bk7258_wifi_stop_sta();
      (void)bk7258_wifi_sync_native_link(result);
    }

  return ret;
}

static void bk7258_wifi_control_report_immediate(
  FAR const struct bk7258_wifi_control_wire_s *request, int status)
{
  struct bk7258_wifi_control_wire_s report;

  memset(&report, 0, sizeof(report));
  report.magic = BK7258_WIFI_CONTROL_MAGIC;
  report.version = BK7258_WIFI_CONTROL_VERSION;
  report.command = BK7258_WIFI_CONTROL_COMMAND_REPORT;
  report.generation = request->generation;
  report.sequence = request->sequence;
  report.operation = request->operation;
  report.result.status = status;

  /* This helper runs in the RPMsg receive callback.  Never sleep there. */

  if (bk7258_wifi_control_endpoint_ready())
    {
      (void)rpmsg_trysend(&g_bk7258_wifi_control.ept, &report,
                          sizeof(report));
    }
}

static FAR void *bk7258_wifi_control_worker(FAR void *arg)
{
  struct bk7258_wifi_control_dev_s *priv = arg;
  struct bk7258_wifi_control_wire_s request;
  struct bk7258_wifi_control_wire_s report;
  struct bk7258_wifi_result_s link;
  int wait_ret;
  int status;

  for (;;)
    {
      /* Keep the SDK event task out of the NuttX wrapper.  A bounded poll on
       * this pinned CPU0 worker catches late DHCP completion and disconnects
       * without running native netdev operations in a vendor callback.
       */

      wait_ret = nxsem_tickwait_uninterruptible(
        &priv->request_sem, MSEC2TICK(BK7258_WIFI_LINK_SYNC_MS));

      if (__atomic_load_n(&priv->abort, __ATOMIC_ACQUIRE))
        {
          break;
        }

      if (wait_ret == -ETIMEDOUT)
        {
          if (!__atomic_load_n(&priv->busy, __ATOMIC_ACQUIRE))
            {
              (void)bk7258_wifi_sync_native_link(&link);
            }

          continue;
        }

      if (wait_ret < 0)
        {
          continue;
        }

      if (!__atomic_load_n(&priv->busy, __ATOMIC_ACQUIRE))
        {
          continue;
        }

      __asm volatile ("dmb sy" ::: "memory");
      memcpy(&request, &priv->request, sizeof(request));
      explicit_bzero(&priv->request, sizeof(priv->request));

      memset(&report, 0, sizeof(report));
      report.magic = BK7258_WIFI_CONTROL_MAGIC;
      report.version = BK7258_WIFI_CONTROL_VERSION;
      report.command = BK7258_WIFI_CONTROL_COMMAND_REPORT;
      report.generation = request.generation;
      report.sequence = request.sequence;
      report.operation = request.operation;

      if (!bk7258_wifi_control_generation_ready(request.generation))
        {
          status = -ESTALE;
        }
      else if (request.operation == BK7258_WIFI_OPERATION_CONNECT)
        {
          status = bk7258_wifi_connect(&request, &report.result);
        }
      else if (request.operation == BK7258_WIFI_OPERATION_STATUS)
        {
          status = bk7258_wifi_read_status(&report.result);
        }
      else if (request.operation == BK7258_WIFI_OPERATION_PING)
        {
          status = bk7258_wifi_ping_gateway(&report.result,
                                            request.timeout_ms);
        }
      else if (request.operation == BK7258_WIFI_OPERATION_TCP_ECHO ||
               request.operation == BK7258_WIFI_OPERATION_UDP_ECHO)
        {
          status = bk7258_wifi_echo_exchange(&request, &report.result);
        }
      else
        {
          status = -EINVAL;
        }

      report.result.status = status;
      (void)bk7258_wifi_control_send_bounded(&report);
      explicit_bzero(&request, sizeof(request));
      __atomic_store_n(&priv->busy, false, __ATOMIC_RELEASE);
    }

  return NULL;
}

static int bk7258_wifi_control_spawn_worker(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  cpu_set_t cpuset = (cpu_set_t)1u;
  pthread_t thread;
  bool initialized = false;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      initialized = true;
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr,
                                      CONFIG_BK7258_WIFI_CONTROL_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = CONFIG_BK7258_WIFI_CONTROL_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, bk7258_wifi_control_worker,
                           &g_bk7258_wifi_control);
      if (ret == 0)
        {
          (void)pthread_setname_np(thread, "bk-wifi-ctl");
        }
    }

  if (initialized)
    {
      (void)pthread_attr_destroy(&attr);
    }

  return ret == 0 ? OK : -ret;
}
#endif /* CONFIG_BK7258_AP_CORE */

static int bk7258_wifi_control_ept_cb(FAR struct rpmsg_endpoint *ept,
                                       FAR void *data, size_t length,
                                       uint32_t source, FAR void *arg)
{
  struct bk7258_wifi_control_dev_s *priv = arg;
  FAR struct bk7258_wifi_control_wire_s *message = data;

  (void)ept;
  (void)source;
  if (message == NULL || length != sizeof(*message) ||
      message->magic != BK7258_WIFI_CONTROL_MAGIC ||
      message->version != BK7258_WIFI_CONTROL_VERSION)
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (message->command == BK7258_WIFI_CONTROL_COMMAND_REQUEST)
    {
      bool expected = false;

      if (!__atomic_compare_exchange_n(&priv->busy, &expected, true, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
          bk7258_wifi_control_report_immediate(message, -EBUSY);
          explicit_bzero(data, length);
          return OK;
        }

      memcpy(&priv->request, message, sizeof(priv->request));
      __asm volatile ("dmb sy" ::: "memory");
      explicit_bzero(data, length);
      (void)nxsem_post(&priv->request_sem);
      return OK;
    }
#else
  if (message->command == BK7258_WIFI_CONTROL_COMMAND_REPORT &&
      message->generation == priv->waiting_generation &&
      message->sequence == priv->waiting_sequence)
    {
      memcpy(&priv->report, &message->result, sizeof(priv->report));
      __asm volatile ("dmb sy" ::: "memory");
      priv->report_valid = true;
      (void)nxsem_post(&priv->report_sem);
      return OK;
    }
#endif

  return -ENOMSG;
}

static void bk7258_wifi_control_device_created(
  FAR struct rpmsg_device *rdev, FAR void *arg)
{
  struct bk7258_wifi_control_dev_s *priv = arg;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_WIFI_CONTROL_REMOTE_NAME) != 0)
    {
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, BK7258_WIFI_CONTROL_EPT_NAME,
    RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, bk7258_wifi_control_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
#else
  priv->connection_error = OK;
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static void bk7258_wifi_control_flush_sem(FAR sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static bool bk7258_wifi_control_ns_match(FAR struct rpmsg_device *rdev,
                                         FAR void *arg,
                                         FAR const char *name,
                                         uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)arg;
  (void)dest;
  return cpuname != NULL &&
         strcmp(cpuname, BK7258_WIFI_CONTROL_REMOTE_NAME) == 0 &&
         strcmp(name, BK7258_WIFI_CONTROL_EPT_NAME) == 0;
}

static void bk7258_wifi_control_ns_bind(FAR struct rpmsg_device *rdev,
                                        FAR void *arg,
                                        FAR const char *name,
                                        uint32_t dest)
{
  struct bk7258_wifi_control_dev_s *priv = arg;

  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
    bk7258_wifi_control_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
}
#endif

static void bk7258_wifi_control_device_destroy(
  FAR struct rpmsg_device *rdev, FAR void *arg)
{
  struct bk7258_wifi_control_dev_s *priv = arg;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_WIFI_CONTROL_REMOTE_NAME) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
  (void)nxsem_post(&priv->request_sem);
#else
  priv->report_valid = false;
  (void)nxsem_post(&priv->report_sem);
#endif

  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#if defined(CONFIG_BK7258_AP_CORE) && !defined(CONFIG_BK7258_ETH)
void arm_netinitialize(void)
{
  /* VNET registers wlan0 after RPMsg is ready; there is no early MAC. */
}
#endif

int bk7258_wifi_control_initialize(void)
{
  struct bk7258_wifi_control_dev_s *priv = &g_bk7258_wifi_control;
  int ret;

  if (priv->initialized)
    {
      return OK;
    }

  memset(priv, 0, sizeof(*priv));
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  ret = nxsem_init(&priv->request_sem, 0, 0);
#else
  ret = nxsem_init(&priv->report_sem, 0, 0);
#endif
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsg_register_callback(
    priv, bk7258_wifi_control_device_created,
    bk7258_wifi_control_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
    NULL, NULL);
#else
    bk7258_wifi_control_ns_match, bk7258_wifi_control_ns_bind);
#endif
  if (ret < 0)
    {
#ifdef CONFIG_BK7258_AP_CORE
      nxsem_destroy(&priv->request_sem);
#else
      nxsem_destroy(&priv->report_sem);
#endif
      return ret;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = bk7258_wifi_control_spawn_worker();
  if (ret < 0)
    {
      rpmsg_unregister_callback(
        priv, bk7258_wifi_control_device_created,
        bk7258_wifi_control_device_destroy, NULL, NULL);
      nxsem_destroy(&priv->request_sem);
      return ret;
    }
#endif

  priv->initialized = true;
  return OK;
}

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_wifi_control_request(enum bk7258_wifi_operation_e operation,
                                const char *ssid, const char *password,
                                const struct bk7258_wifi_echo_s *echo,
                                uint32_t timeout_ms,
                                struct bk7258_wifi_result_s *result)
{
  struct bk7258_wifi_control_dev_s *priv = &g_bk7258_wifi_control;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_wifi_control_wire_s request;
  clock_t started;
  size_t ssid_len = 0;
  size_t password_len = 0;
  int ret;

  bool echo_operation = operation == BK7258_WIFI_OPERATION_TCP_ECHO ||
                        operation == BK7258_WIFI_OPERATION_UDP_ECHO;

  if (result == NULL ||
      (operation != BK7258_WIFI_OPERATION_CONNECT &&
       operation != BK7258_WIFI_OPERATION_STATUS &&
       operation != BK7258_WIFI_OPERATION_PING &&
       !echo_operation) ||
      timeout_ms < (echo_operation ? BK7258_WIFI_ECHO_MIN_MS :
                                      BK7258_WIFI_CONNECT_MIN_MS) ||
      timeout_ms > BK7258_WIFI_CONNECT_MAX_MS)
    {
      return -EINVAL;
    }

  if (echo_operation)
    {
      if (echo == NULL || echo->address == 0 ||
          echo->address == UINT32_MAX || echo->port == 0 ||
          echo->port > UINT16_MAX || echo->count == 0 ||
          echo->count > BK7258_WIFI_ECHO_COUNT_MAX || echo->size == 0 ||
          echo->size > BK7258_WIFI_ECHO_SIZE_MAX)
        {
          return -EINVAL;
        }
    }
  else if (echo != NULL)
    {
      return -EINVAL;
    }

  if (operation == BK7258_WIFI_OPERATION_CONNECT)
    {
      if (ssid == NULL || password == NULL)
        {
          return -EINVAL;
        }

      ssid_len = strnlen(ssid, BK7258_WIFI_SSID_MAX_LEN + 1u);
      password_len = strnlen(password,
                             BK7258_WIFI_PASSWORD_MAX_LEN + 1u);
      if (ssid_len == 0 || ssid_len > BK7258_WIFI_SSID_MAX_LEN ||
          password_len > BK7258_WIFI_PASSWORD_MAX_LEN)
        {
          return -EINVAL;
        }
    }

  memset(result, 0, sizeof(*result));
  ret = bk7258_wifi_control_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&g_bk7258_wifi_control_lock);
  if (ret < 0)
    {
      return ret;
    }

  started = clock_systime_ticks();
  while (!bk7258_wifi_control_endpoint_ready() &&
         (clock_systime_ticks() - started) <
         MSEC2TICK(BK7258_WIFI_CONTROL_ENDPOINT_WAIT_MS))
    {
      nxsig_usleep(1000);
    }

  if (!bk7258_wifi_control_endpoint_ready())
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -ENOTCONN;
      goto out_unlock;
    }

  bk7258_wifi_control_flush_sem(&priv->report_sem);
  if (++g_bk7258_wifi_control_sequence == 0)
    {
      g_bk7258_wifi_control_sequence++;
    }

  priv->waiting_generation = control->generation;
  priv->waiting_sequence = g_bk7258_wifi_control_sequence;
  priv->report_valid = false;
  memset(&priv->report, 0, sizeof(priv->report));

  memset(&request, 0, sizeof(request));
  request.magic = BK7258_WIFI_CONTROL_MAGIC;
  request.version = BK7258_WIFI_CONTROL_VERSION;
  request.command = BK7258_WIFI_CONTROL_COMMAND_REQUEST;
  request.generation = priv->waiting_generation;
  request.sequence = priv->waiting_sequence;
  request.operation = (uint32_t)operation;
  request.timeout_ms = timeout_ms;
  request.ssid_len = (uint32_t)ssid_len;
  request.password_len = (uint32_t)password_len;
  if (echo_operation)
    {
      memcpy(&request.echo, echo, sizeof(request.echo));
    }

  if (ssid_len > 0)
    {
      memcpy(request.ssid, ssid, ssid_len);
      memcpy(request.password, password, password_len);
    }

  ret = bk7258_wifi_control_send_bounded(&request);
  explicit_bzero(&request, sizeof(request));
  if (ret < 0)
    {
      goto out_unlock;
    }

  ret = nxsem_tickwait_uninterruptible(&priv->report_sem,
                                        MSEC2TICK(timeout_ms + 1000u));
  if (ret < 0)
    {
      goto out_unlock;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (!priv->report_valid)
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -EPROTO;
      goto out_unlock;
    }

  memcpy(result, &priv->report, sizeof(*result));
  ret = result->status;

out_unlock:
  nxmutex_unlock(&g_bk7258_wifi_control_lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_WIFI_VNET */
