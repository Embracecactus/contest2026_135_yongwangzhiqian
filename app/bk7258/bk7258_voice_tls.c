/****************************************************************************
 * app/bk7258/bk7258_voice_tls.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_voice_tls.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __NuttX__
#  include <sys/ioctl.h>
#  include <nuttx/clock.h>
#  include <nuttx/semaphore.h>
#endif

#include <mbedtls/net_sockets.h>
#include <mbedtls/sha1.h>

#if !defined(MBEDTLS_HAVE_TIME_DATE)
#  error "BKVoice TLS requires certificate validity-date verification"
#endif

#define BKVOICE_TLS_POLL_MS 20u

static int bkvoice_tls_deadline(struct bkvoice_tls_s *tls,
                                uint64_t deadline_ms)
{
  if (__atomic_load_n(&tls->interrupted, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  return tls->config.now_ms(tls->config.clock_context) >= deadline_ms ?
         -ETIMEDOUT : 0;
}

static int bkvoice_tls_lock(struct bkvoice_tls_s *tls,
                            uint64_t deadline_ms)
{
  int ret;

  for (; ; )
    {
      ret = bkvoice_tls_deadline(tls, deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      ret = pthread_mutex_trylock(&tls->crypto_lock);
      if (ret != EBUSY)
        {
          return ret == 0 ? 0 : -ret;
        }

      (void)poll(NULL, 0, 1);
    }
}

static int bkvoice_tls_wait(struct bkvoice_tls_s *tls, short events,
                            uint64_t deadline_ms)
{
  struct pollfd pfd;
  uint64_t now;
  uint64_t remaining;
  int ret;

  for (; ; )
    {
      ret = bkvoice_tls_deadline(tls, deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      now = tls->config.now_ms(tls->config.clock_context);
      if (now >= deadline_ms)
        {
          return -ETIMEDOUT;
        }

      remaining = deadline_ms - now;
      if (remaining > BKVOICE_TLS_POLL_MS)
        {
          remaining = BKVOICE_TLS_POLL_MS;
        }

#ifdef __NuttX__
      /* Own the socket directly, like AP Wi-Fi and HTTPS OTA. Polling must
       * not re-enter the shared task group's descriptor table. Teardown
       * completes before the stack semaphore leaves this iteration.
       */

      sem_t sem;
      ret = nxsem_init(&sem, 0, 0);
      if (ret < 0) return ret;
      memset(&pfd, 0, sizeof(pfd));
      pfd.fd = -1;
      pfd.events = events;
      pfd.arg = &sem;
      pfd.cb = poll_default_cb;
      ret = psock_poll(&tls->socket, &pfd, true);
      if (ret >= 0)
        {
          if (pfd.revents == 0)
            ret = nxsem_tickwait_uninterruptible(
                    &sem, MSEC2TICK((unsigned int)remaining));
          int teardown = psock_poll(&tls->socket, &pfd, false);
          if (teardown < 0) ret = teardown;
        }
      (void)nxsem_destroy(&sem);
      if (ret < 0 && ret != -ETIMEDOUT && ret != -EINTR) return ret;
      if (pfd.revents != 0) return bkvoice_tls_deadline(tls, deadline_ms);
#else
      pfd.fd = tls->fd;
      pfd.events = events;
      pfd.revents = 0;
      ret = poll(&pfd, 1, (int)remaining);
      if (ret < 0 && errno != EINTR)
        {
          return -errno;
        }

      if (ret > 0)
        {
          /* Let the next nonblocking I/O observe buffered data, EOF or
           * SO_ERROR even when POLLHUP/POLLERR accompany readiness.
           */

          return bkvoice_tls_deadline(tls, deadline_ms);
        }
#endif
    }
}

static int bkvoice_tls_socket_send(void *context,
                                   const unsigned char *buffer, size_t bytes)
{
  struct bkvoice_tls_s *tls = context;
  ssize_t ret;

#ifdef __NuttX__
  ret = psock_send(&tls->socket, buffer, bytes, 0);
  if (ret < 0)
    return ret == -EAGAIN || ret == -EWOULDBLOCK || ret == -EINTR ?
           MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
#else
#ifdef MSG_NOSIGNAL
  ret = send(tls->fd, buffer, bytes, MSG_NOSIGNAL);
#else
  ret = send(tls->fd, buffer, bytes, 0);
#endif
  if (ret < 0)
    {
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ?
             MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    }

#endif
  return (int)ret;
}

static int bkvoice_tls_socket_recv(void *context, unsigned char *buffer,
                                   size_t bytes)
{
  struct bkvoice_tls_s *tls = context;
#ifdef __NuttX__
  ssize_t ret = psock_recv(&tls->socket, buffer, bytes, 0);
  if (ret < 0)
    return ret == -EAGAIN || ret == -EWOULDBLOCK || ret == -EINTR ?
           MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
#else
  ssize_t ret = recv(tls->fd, buffer, bytes, 0);

  if (ret < 0)
    {
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ?
             MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    }

#endif
  return (int)ret;
}

static int bkvoice_tls_close(void *context)
{
  struct bkvoice_tls_s *tls = context;

  if (tls == NULL || !tls->initialized)
    {
      return -EINVAL;
    }

  /* The owner has already joined I/O.  Do not wait for the peer or invoke
   * a blocking TLS close-notify from the teardown path.
   */

  if (tls->socket_open)
    {
#ifdef __NuttX__
      (void)psock_close(&tls->socket);
#else
      (void)close(tls->fd);
      tls->fd = -1;
#endif
      tls->socket_open = false;
    }

  mbedtls_ssl_free(&tls->ssl);
  mbedtls_ssl_config_free(&tls->ssl_config);
  mbedtls_ssl_init(&tls->ssl);
  mbedtls_ssl_config_init(&tls->ssl_config);
  tls->opened = false;
  __atomic_store_n(&tls->faulted, false, __ATOMIC_RELEASE);
  return 0;
}

static int bkvoice_tls_open(void *context, const char *host, uint16_t port,
                            uint64_t deadline_ms)
{
  struct bkvoice_tls_s *tls = context;
  struct sockaddr_in peer;
  socklen_t error_size;
  int socket_error;
  int ret;

  if (tls == NULL || !tls->initialized || host == NULL || host[0] == '\0' ||
      port == 0)
    {
      return -EINVAL;
    }

  if (tls->opened || tls->socket_open)
    {
      return -EALREADY;
    }

  __atomic_store_n(&tls->interrupted, false, __ATOMIC_RELEASE);
  ret = bkvoice_tls_deadline(tls, deadline_ms);
  if (ret < 0)
    {
      return ret;
    }

  ret = tls->config.trusted_time(tls->config.clock_context);
  if (ret != 0)
    {
      return ret < 0 ? ret : -EKEYREJECTED;
    }

  ret = mbedtls_ssl_config_defaults(&tls->ssl_config,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret == 0)
    {
      mbedtls_ssl_conf_authmode(&tls->ssl_config,
                                MBEDTLS_SSL_VERIFY_REQUIRED);
      mbedtls_ssl_conf_ca_chain(&tls->ssl_config, tls->config.server_ca,
                                NULL);
      mbedtls_ssl_conf_rng(&tls->ssl_config, mbedtls_ctr_drbg_random,
                           &tls->random);
      mbedtls_ssl_conf_min_tls_version(&tls->ssl_config,
                                       MBEDTLS_SSL_VERSION_TLS1_2);
      mbedtls_ssl_conf_max_tls_version(&tls->ssl_config,
                                       MBEDTLS_SSL_VERSION_TLS1_2);
#ifdef MBEDTLS_SSL_RENEGOTIATION
      mbedtls_ssl_conf_renegotiation(&tls->ssl_config,
                                     MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
      if (!tls->config.server_auth_only)
        {
          ret = mbedtls_ssl_conf_own_cert(&tls->ssl_config,
                                      tls->config.client_certificate,
                                      tls->config.client_key);
        }
    }

  if (ret == 0)
    {
      ret = mbedtls_ssl_setup(&tls->ssl, &tls->ssl_config);
    }

  if (ret == 0)
    {
      ret = mbedtls_ssl_set_hostname(&tls->ssl, host);
    }

  if (ret != 0)
    {
      ret = -EKEYREJECTED;
      goto fail;
    }

  memset(&peer, 0, sizeof(peer));
  peer.sin_family = AF_INET;
  peer.sin_addr = tls->config.peer_address;
  peer.sin_port = htons(port);
#ifdef __NuttX__
  ret = psock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP, &tls->socket);
  if (ret < 0) goto fail;
  tls->socket_open = true;
  int nonblock = 1;
  ret = psock_ioctl(&tls->socket, FIONBIO,
                    (unsigned long)(uintptr_t)&nonblock);
  if (ret < 0) goto fail;
  ret = psock_connect(&tls->socket, (struct sockaddr *)&peer, sizeof(peer));
  if (ret < 0 && ret != -EINPROGRESS) goto fail;
#else
  tls->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (tls->fd < 0)
    {
      ret = -errno;
      goto fail;
    }

  tls->socket_open = true;
  ret = fcntl(tls->fd, F_GETFL);
  if (ret < 0 || fcntl(tls->fd, F_SETFL, ret | O_NONBLOCK) < 0)
    {
      ret = -errno;
      goto fail;
    }

  ret = connect(tls->fd, (struct sockaddr *)&peer, sizeof(peer));
  if (ret < 0 && errno != EINPROGRESS)
    {
      ret = -errno;
      goto fail;
    }

#endif
  if (ret < 0)
    {
      ret = bkvoice_tls_wait(tls, POLLOUT, deadline_ms);
      if (ret < 0)
        {
          goto fail;
        }

      socket_error = 0;
      error_size = sizeof(socket_error);
#ifdef __NuttX__
      ret = psock_getsockopt(&tls->socket, SOL_SOCKET, SO_ERROR,
                             &socket_error, &error_size);
      if (ret < 0) goto fail;
#else
      if (getsockopt(tls->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                     &error_size) < 0)
        {
          ret = -errno;
          goto fail;
        }

#endif
      if (socket_error != 0)
        {
          ret = -socket_error;
          goto fail;
        }
    }

  mbedtls_ssl_set_bio(&tls->ssl, tls, bkvoice_tls_socket_send,
                      bkvoice_tls_socket_recv, NULL);
  for (; ; )
    {
      ret = bkvoice_tls_deadline(tls, deadline_ms);
      if (ret < 0)
        {
          goto fail;
        }

      ret = mbedtls_ssl_handshake(&tls->ssl);
      if (ret == 0)
        {
          break;
        }

      if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
          ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          ret = -EKEYREJECTED;
          goto fail;
        }

      ret = bkvoice_tls_wait(tls,
                             ret == MBEDTLS_ERR_SSL_WANT_READ ?
                             POLLIN : POLLOUT, deadline_ms);
      if (ret < 0)
        {
          goto fail;
        }
    }

  if (mbedtls_ssl_get_verify_result(&tls->ssl) != 0 ||
      tls->config.trusted_time(tls->config.clock_context) != 0)
    {
      ret = -EKEYREJECTED;
      goto fail;
    }

  ret = bkvoice_tls_deadline(tls, deadline_ms);
  if (ret < 0)
    {
      goto fail;
    }

  tls->opened = true;
  return 0;

fail:
  (void)bkvoice_tls_close(tls);
  return ret;
}

static ssize_t bkvoice_tls_io(struct bkvoice_tls_s *tls, void *buffer,
                              size_t bytes, uint64_t deadline_ms, bool write)
{
  bool locked = false;
  int ret;
  int want = write ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_WANT_READ;

  if (tls == NULL || !tls->initialized || !tls->opened)
    {
      return -ENOTCONN;
    }

  if (buffer == NULL || bytes == 0)
    {
      return -EINVAL;
    }

  for (; ; )
    {
      if (!locked)
        {
          ret = bkvoice_tls_lock(tls, deadline_ms);
          if (ret < 0)
            {
              goto fail;
            }

          locked = true;
        }

      ret = bkvoice_tls_deadline(tls, deadline_ms);
      if (ret < 0)
        {
          goto fail;
        }

      if (__atomic_load_n(&tls->faulted, __ATOMIC_ACQUIRE))
        {
          ret = -ENOTCONN;
          goto fail;
        }

      ret = write ? mbedtls_ssl_write(&tls->ssl, buffer, bytes) :
                    mbedtls_ssl_read(&tls->ssl, buffer, bytes);
      if (ret > 0)
        {
          (void)pthread_mutex_unlock(&tls->crypto_lock);
          return ret;
        }

      if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
          ret = 0;
          goto fail;
        }

      if (ret != want)
        {
          /* Post-handshake transitions requiring cross-direction I/O are
           * outside this TLS 1.2, no-renegotiation provider.  Invalidate both
           * directions under the lock before another caller can use ssl.
           */

          ret = -EIO;
          goto fail;
        }

      if (!write)
        {
          (void)pthread_mutex_unlock(&tls->crypto_lock);
          locked = false;
        }

      /* A write owns ssl across WANT_WRITE retries with the same buffer.
       * Otherwise a read-generated alert could flush out_left and make the
       * writer's retry transmit that application record a second time.
       * Idle WANT_READ waits release the lock, so uplink remains possible.
       */

      ret = bkvoice_tls_wait(tls, write ? POLLOUT : POLLIN, deadline_ms);
      if (ret < 0)
        {
          goto fail;
        }
    }

fail:
  __atomic_store_n(&tls->faulted, true, __ATOMIC_RELEASE);
  if (locked)
    {
      (void)pthread_mutex_unlock(&tls->crypto_lock);
    }

  return ret;
}

static ssize_t bkvoice_tls_send(void *context, const uint8_t *buffer,
                                size_t bytes, uint64_t deadline_ms)
{
  return bkvoice_tls_io(context, (void *)buffer, bytes, deadline_ms, true);
}

static ssize_t bkvoice_tls_recv(void *context, uint8_t *buffer, size_t bytes,
                                uint64_t deadline_ms)
{
  return bkvoice_tls_io(context, buffer, bytes, deadline_ms, false);
}

static int bkvoice_tls_interrupt(void *context)
{
  struct bkvoice_tls_s *tls = context;

  if (tls == NULL || !tls->initialized)
    {
      return -EINVAL;
    }

  __atomic_store_n(&tls->interrupted, true, __ATOMIC_RELEASE);
  return 0;
}

static int bkvoice_tls_random(void *context, uint8_t *buffer, size_t bytes)
{
  struct bkvoice_tls_s *tls = context;
  int ret;

  if (tls == NULL || !tls->initialized || buffer == NULL || bytes == 0)
    {
      return -EINVAL;
    }

  ret = pthread_mutex_lock(&tls->crypto_lock);
  if (ret != 0)
    {
      return -ret;
    }

  ret = mbedtls_ctr_drbg_random(&tls->random, buffer, bytes);
  (void)pthread_mutex_unlock(&tls->crypto_lock);
  return ret == 0 ? 0 : -EIO;
}

static int bkvoice_tls_sha1(void *context, const uint8_t *buffer,
                            size_t bytes, uint8_t digest[20])
{
  (void)context;
  return mbedtls_sha1(buffer, bytes, digest) == 0 ? 0 : -EIO;
}

static const struct bkvoice_wss_tls_ops_s g_bkvoice_tls_ops =
{
  .open_verified = bkvoice_tls_open,
  .send = bkvoice_tls_send,
  .recv = bkvoice_tls_recv,
  .interrupt = bkvoice_tls_interrupt,
  .close = bkvoice_tls_close,
  .random = bkvoice_tls_random,
  .sha1 = bkvoice_tls_sha1,
};

int bkvoice_tls_initialize(struct bkvoice_tls_s *tls,
                           const struct bkvoice_tls_config_s *config)
{
  static const unsigned char personalization[] = "bkvoice-wss";
  uint32_t address;
  int ret;

  if (tls == NULL || config == NULL || config->server_ca == NULL ||
      config->trusted_time == NULL || config->now_ms == NULL)
    {
      return -EINVAL;
    }

  if (config->server_auth_only ?
      (config->client_certificate != NULL || config->client_key != NULL) :
      (config->client_certificate == NULL || config->client_key == NULL))
    {
      return -EINVAL;
    }

  address = ntohl(config->peer_address.s_addr);
  if (address == 0 || address >= 0xe0000000u)
    {
      return -EINVAL;
    }

  memset(tls, 0, sizeof(*tls));
#ifndef __NuttX__
  tls->fd = -1;
#endif
  tls->config = *config;
  ret = pthread_mutex_init(&tls->crypto_lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  mbedtls_ssl_init(&tls->ssl);
  mbedtls_ssl_config_init(&tls->ssl_config);
  mbedtls_entropy_init(&tls->entropy);
  mbedtls_ctr_drbg_init(&tls->random);
  ret = mbedtls_ctr_drbg_seed(&tls->random, mbedtls_entropy_func,
                              &tls->entropy, personalization,
                              sizeof(personalization) - 1u);
  if (ret != 0)
    {
      mbedtls_ctr_drbg_free(&tls->random);
      mbedtls_entropy_free(&tls->entropy);
      (void)pthread_mutex_destroy(&tls->crypto_lock);
      return -EIO;
    }

  tls->initialized = true;
  return 0;
}

int bkvoice_tls_uninitialize(struct bkvoice_tls_s *tls)
{
  int ret;

  if (tls == NULL || !tls->initialized)
    {
      return -EINVAL;
    }

  if (tls->opened || tls->socket_open)
    {
      return -EBUSY;
    }

  ret = pthread_mutex_destroy(&tls->crypto_lock);
  if (ret != 0)
    {
      return -ret;
    }

  mbedtls_ssl_free(&tls->ssl);
  mbedtls_ssl_config_free(&tls->ssl_config);
  mbedtls_ctr_drbg_free(&tls->random);
  mbedtls_entropy_free(&tls->entropy);
  memset(tls, 0, sizeof(*tls));
  return 0;
}

const struct bkvoice_wss_tls_ops_s *bkvoice_tls_ops(void)
{
  return &g_bkvoice_tls_ops;
}
