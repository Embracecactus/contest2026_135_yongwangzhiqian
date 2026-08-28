/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/ap/
 * bk7258_ota_source_http.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-owned HTTPS Range source.  Wi-Fi only supplies wlan0; CP never owns an
 * HTTP/TLS stack.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <nuttx/kmalloc.h>
#include <nuttx/clock.h>
#include <nuttx/net/net.h>
#include <nuttx/semaphore.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <arch/chip/bk7258_ota_catalog.h>
#include <arch/chip/bk7258_ota_source_http.h>

#define BK7258_OTA_HTTP_URL_SIZE       384u
#define BK7258_OTA_HTTP_HOST_SIZE      96u
#define BK7258_OTA_HTTP_PORT_SIZE      6u
#define BK7258_OTA_HTTP_PATH_SIZE      256u
#define BK7258_OTA_HTTP_CA_MAX         8192u
#define BK7258_OTA_HTTP_HEADER_MAX     1024u
#define BK7258_OTA_HTTP_IO_TIMEOUT_MS  15000u
#define BK7258_OTA_HTTP_RANGE_CACHE_SIZE 16384u
#define BK7258_OTA_HTTP_CATALOG_NAME   "catalog.json"
#define BK7258_OTA_HTTP_SIGNATURE_NAME "catalog.sig"

struct bk7258_ota_http_url_s
{
  char host[BK7258_OTA_HTTP_HOST_SIZE];
  char port[BK7258_OTA_HTTP_PORT_SIZE];
  char path[BK7258_OTA_HTTP_PATH_SIZE];
  bool secure;
};

struct bk7258_ota_http_source_priv_s
{
  char catalog_url[BK7258_OTA_HTTP_URL_SIZE];
  char signature_url[BK7258_OTA_HTTP_URL_SIZE];
  char object_url[2][BK7258_OTA_HTTP_URL_SIZE];
  char ca_path[BK7258_OTA_HTTP_PATH_SIZE];
  struct bk7258_ota_catalog_s catalog;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_x509_crt ca;
  struct socket socket;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  uint8_t *range_cache;
  enum bk7258_ota_image_e range_cache_image;
  uint32_t range_cache_start;
  uint32_t range_cache_length;
  bool range_cache_valid;
  bool socket_open;
  bool tls_active;
  bool secure;
  bool crypto_ready;
  volatile bool canceled;
};

static int bk7258_ota_http_copy(char *output, size_t output_size,
                                const char *input)
{
  size_t length = strlen(input);

  if (length == 0u || length >= output_size)
    {
      return -ENAMETOOLONG;
    }

  memcpy(output, input, length + 1u);
  return 0;
}

static int bk7258_ota_http_parse_url(const char *url,
                                     struct bk7258_ota_http_url_s *parsed)
{
  const char *authority;
  const char *path;
  const char *colon;
  size_t host_size;
  size_t port_size;

  if (strncmp(url, "https://", 8u) == 0)
    {
      parsed->secure = true;
      authority = url + 8u;
    }
#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP_PLAINTEXT
  else if (strncmp(url, "http://", 7u) == 0)
    {
      parsed->secure = false;
      authority = url + 7u;
    }
#endif
  else
    {
      return -EPROTONOSUPPORT;
    }

  path = strchr(authority, '/');
  if (path == NULL || path == authority || strlen(path) >= sizeof(parsed->path))
    {
      return -EINVAL;
    }

  colon = memchr(authority, ':', (size_t)(path - authority));
  host_size = (size_t)((colon == NULL ? path : colon) - authority);
  if (host_size == 0u || host_size >= sizeof(parsed->host))
    {
      return -EINVAL;
    }
  memcpy(parsed->host, authority, host_size);
  parsed->host[host_size] = '\0';

  if (colon == NULL)
    {
      if (parsed->secure)
        {
          memcpy(parsed->port, "443", sizeof("443"));
        }
      else
        {
          memcpy(parsed->port, "80", sizeof("80"));
        }
    }
  else
    {
      port_size = (size_t)(path - colon - 1);
      if (port_size == 0u || port_size >= sizeof(parsed->port))
        {
          return -EINVAL;
        }
      memcpy(parsed->port, colon + 1, port_size);
      parsed->port[port_size] = '\0';
    }

  memcpy(parsed->path, path, strlen(path) + 1u);
  return 0;
}

static int bk7258_ota_http_derive_urls(
  struct bk7258_ota_http_source_priv_s *priv)
{
  size_t length = strlen(priv->catalog_url);
  size_t prefix;
  int image;

  if (length <= sizeof(BK7258_OTA_HTTP_CATALOG_NAME) - 1u ||
      strcmp(priv->catalog_url + length -
             (sizeof(BK7258_OTA_HTTP_CATALOG_NAME) - 1u),
             BK7258_OTA_HTTP_CATALOG_NAME) != 0)
    {
      return -EINVAL;
    }

  prefix = length - (sizeof(BK7258_OTA_HTTP_CATALOG_NAME) - 1u);
  if (prefix + sizeof(BK7258_OTA_HTTP_SIGNATURE_NAME) >
      sizeof(priv->signature_url))
    {
      return -ENAMETOOLONG;
    }
  memcpy(priv->signature_url, priv->catalog_url, prefix);
  memcpy(priv->signature_url + prefix, BK7258_OTA_HTTP_SIGNATURE_NAME,
         sizeof(BK7258_OTA_HTTP_SIGNATURE_NAME));

  for (image = BK7258_OTA_IMAGE_CP; image <= BK7258_OTA_IMAGE_AP; image++)
    {
      size_t uri_size = strlen(priv->catalog.uri[image]);
      if (prefix + uri_size >= sizeof(priv->object_url[image]))
        {
          return -ENAMETOOLONG;
        }
      memcpy(priv->object_url[image], priv->catalog_url, prefix);
      memcpy(priv->object_url[image] + prefix, priv->catalog.uri[image],
             uri_size + 1u);
    }

  return 0;
}

static void bk7258_ota_http_disconnect(
  struct bk7258_ota_http_source_priv_s *priv)
{
  if (priv->socket_open && priv->tls_active)
    {
      (void)mbedtls_ssl_close_notify(&priv->ssl);
    }
  if (priv->socket_open)
    {
      (void)psock_close(&priv->socket);
      priv->socket_open = false;
    }
  memset(&priv->socket, 0, sizeof(priv->socket));
  mbedtls_ssl_free(&priv->ssl);
  mbedtls_ssl_config_free(&priv->config);
  mbedtls_ssl_init(&priv->ssl);
  mbedtls_ssl_config_init(&priv->config);
  priv->tls_active = false;
}

static int bk7258_ota_http_read_tls(
  struct bk7258_ota_http_source_priv_s *priv, uint8_t *buffer, size_t size)
{
  size_t done = 0u;

  while (done < size)
    {
      int ret;

      if (__atomic_load_n(&priv->canceled, __ATOMIC_ACQUIRE))
        {
          return -ECANCELED;
        }
      ret = priv->tls_active ?
            mbedtls_ssl_read(&priv->ssl, buffer + done, size - done) :
            psock_recv(&priv->socket, buffer + done, size - done, 0);
      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }
      if (ret <= 0)
        {
          return ret == 0 ? -ECONNRESET : -EIO;
        }
      done += (size_t)ret;
    }

  return 0;
}

static int bk7258_ota_http_write_tls(
  struct bk7258_ota_http_source_priv_s *priv, const uint8_t *buffer,
  size_t size)
{
  size_t done = 0u;

  while (done < size)
    {
      int ret = priv->tls_active ?
                mbedtls_ssl_write(&priv->ssl, buffer + done, size - done) :
                psock_send(&priv->socket, buffer + done, size - done, 0);
      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          continue;
        }
      if (ret <= 0)
        {
          return -EIO;
        }
      done += (size_t)ret;
    }

  return 0;
}

static int bk7258_ota_http_tls_send(void *context,
                                    const unsigned char *buffer,
                                    size_t size)
{
  ssize_t ret = psock_send((struct socket *)context, buffer, size, 0);

  if (ret == -EAGAIN || ret == -EWOULDBLOCK)
    {
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

  return ret < 0 ? MBEDTLS_ERR_NET_SEND_FAILED : (int)ret;
}

static int bk7258_ota_http_tls_recv(void *context, unsigned char *buffer,
                                    size_t size)
{
  ssize_t ret = psock_recv((struct socket *)context, buffer, size, 0);

  if (ret == -EAGAIN || ret == -EWOULDBLOCK)
    {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }

  return ret < 0 ? MBEDTLS_ERR_NET_RECV_FAILED : (int)ret;
}

static int bk7258_ota_http_wait_connected(
  struct bk7258_ota_http_source_priv_s *priv)
{
  struct pollfd pfd;
  sem_t sem;
  socklen_t error_size = sizeof(int);
  int socket_error = 0;
  bool polling = false;
  int ret;

  ret = nxsem_init(&sem, 0, 0);
  if (ret < 0)
    {
      return ret;
    }

  memset(&pfd, 0, sizeof(pfd));
  pfd.fd = -1;
  pfd.events = POLLOUT;
  pfd.arg = &sem;
  pfd.cb = poll_default_cb;
  ret = psock_poll(&priv->socket, &pfd, true);
  if (ret >= 0)
    {
      polling = true;
      if (pfd.revents == 0)
        {
          ret = nxsem_tickwait_uninterruptible(
                  &sem, MSEC2TICK(BK7258_OTA_HTTP_IO_TIMEOUT_MS));
        }
    }

  if (polling)
    {
      int teardown = psock_poll(&priv->socket, &pfd, false);
      if (ret >= 0 && teardown < 0)
        {
          ret = teardown;
        }
    }
  (void)nxsem_destroy(&sem);
  if (ret < 0)
    {
      return ret == -ETIMEDOUT ? ret : -ENETUNREACH;
    }

  ret = psock_getsockopt(&priv->socket, SOL_SOCKET, SO_ERROR,
                         &socket_error, &error_size);
  if (ret < 0)
    {
      return ret;
    }
  if (socket_error != 0)
    {
      return socket_error < 0 ? socket_error : -socket_error;
    }

  return (pfd.revents & POLLOUT) != 0 ? 0 : -ENETUNREACH;
}

static int bk7258_ota_http_connect(
  struct bk7258_ota_http_source_priv_s *priv,
  const struct bk7258_ota_http_url_s *url)
{
  struct addrinfo hints;
  struct addrinfo numeric_address;
  struct addrinfo *addresses = NULL;
  struct addrinfo *address;
  struct sockaddr_in numeric_sockaddr;
  struct timeval timeout;
  unsigned long port;
  bool allocated_addresses = false;
  int ret = -ENETUNREACH;

  bk7258_ota_http_disconnect(priv);
  timeout.tv_sec = BK7258_OTA_HTTP_IO_TIMEOUT_MS / 1000u;
  timeout.tv_usec =
    (BK7258_OTA_HTTP_IO_TIMEOUT_MS % 1000u) * 1000u;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  memset(&numeric_address, 0, sizeof(numeric_address));
  memset(&numeric_sockaddr, 0, sizeof(numeric_sockaddr));
  port = strtoul(url->port, NULL, 10);
  if (port > 0u && port <= UINT16_MAX &&
      inet_pton(AF_INET, url->host, &numeric_sockaddr.sin_addr) == 1)
    {
      numeric_sockaddr.sin_family = AF_INET;
      numeric_sockaddr.sin_port = htons((uint16_t)port);
      numeric_address.ai_family = AF_INET;
      numeric_address.ai_socktype = SOCK_STREAM;
      numeric_address.ai_protocol = IPPROTO_TCP;
      numeric_address.ai_addrlen = sizeof(numeric_sockaddr);
      numeric_address.ai_addr = (struct sockaddr *)&numeric_sockaddr;
      addresses = &numeric_address;
    }
  else if (getaddrinfo(url->host, url->port, &hints, &addresses) != 0)
    {
      return -ENETUNREACH;
    }
  else
    {
      allocated_addresses = true;
    }

  for (address = addresses; address != NULL; address = address->ai_next)
    {
      ret = psock_socket(address->ai_family,
                         address->ai_socktype | SOCK_NONBLOCK,
                         address->ai_protocol, &priv->socket);
      if (ret < 0)
        {
          continue;
        }

      priv->socket_open = true;
      ret = psock_setsockopt(&priv->socket, SOL_SOCKET, SO_RCVTIMEO,
                             &timeout, sizeof(timeout));
      if (ret >= 0)
        {
          ret = psock_setsockopt(&priv->socket, SOL_SOCKET, SO_SNDTIMEO,
                                 &timeout, sizeof(timeout));
        }
      if (ret < 0)
        {
          (void)psock_close(&priv->socket);
          memset(&priv->socket, 0, sizeof(priv->socket));
          priv->socket_open = false;
          continue;
        }

      ret = psock_connect(&priv->socket, address->ai_addr,
                          address->ai_addrlen);
      if (ret == -EINPROGRESS)
        {
          ret = bk7258_ota_http_wait_connected(priv);
        }
      if (ret >= 0)
        {
          int nonblock = 0;

          ret = psock_ioctl(&priv->socket, FIONBIO,
                            (unsigned long)(uintptr_t)&nonblock);
        }
      if (ret >= 0)
        {
          break;
        }

      (void)psock_close(&priv->socket);
      memset(&priv->socket, 0, sizeof(priv->socket));
      priv->socket_open = false;
    }

  if (allocated_addresses)
    {
      freeaddrinfo(addresses);
    }
  if (ret < 0)
    {
      return ret;
    }

  priv->tls_active = url->secure;
  if (!url->secure)
    {
      return 0;
    }

  ret = mbedtls_ssl_config_defaults(&priv->config,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret == 0)
    {
      mbedtls_ssl_conf_authmode(&priv->config,
                                MBEDTLS_SSL_VERIFY_REQUIRED);
      mbedtls_ssl_conf_ca_chain(&priv->config, &priv->ca, NULL);
      mbedtls_ssl_conf_rng(&priv->config, mbedtls_ctr_drbg_random,
                           &priv->random);
      ret = mbedtls_ssl_setup(&priv->ssl, &priv->config);
    }
  if (ret == 0)
    {
      ret = mbedtls_ssl_set_hostname(&priv->ssl, url->host);
    }
  if (ret == 0)
    {
      mbedtls_ssl_set_bio(&priv->ssl, &priv->socket,
                          bk7258_ota_http_tls_send,
                          bk7258_ota_http_tls_recv, NULL);
      do
        {
          ret = mbedtls_ssl_handshake(&priv->ssl);
        }
      while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    }
  if (ret != 0 || mbedtls_ssl_get_verify_result(&priv->ssl) != 0u)
    {
      bk7258_ota_http_disconnect(priv);
      return -EKEYREJECTED;
    }

  return 0;
}

static int bk7258_ota_http_headers(
  struct bk7258_ota_http_source_priv_s *priv, int *status,
  uint32_t *content_length, uint32_t *range_start)
{
  char header[BK7258_OTA_HTTP_HEADER_MAX];
  size_t used = 0u;
  char *line;
  char *save;

  while (used + 1u < sizeof(header))
    {
      int ret = bk7258_ota_http_read_tls(
                  priv, (uint8_t *)&header[used], 1u);
      if (ret < 0)
        {
          return ret;
        }
      used++;
      if (used >= 4u && memcmp(&header[used - 4u], "\r\n\r\n", 4u) == 0)
        {
          break;
        }
    }
  if (used + 1u >= sizeof(header))
    {
      return -EOVERFLOW;
    }
  header[used] = '\0';

  *content_length = UINT32_MAX;
  *range_start = UINT32_MAX;
  line = strtok_r(header, "\r\n", &save);
  if (line == NULL || sscanf(line, "HTTP/%*u.%*u %d", status) != 1)
    {
      return -EPROTO;
    }
  while ((line = strtok_r(NULL, "\r\n", &save)) != NULL)
    {
      unsigned long value;

      if (strncasecmp(line, "Content-Length:", 15u) == 0 &&
          sscanf(line + 15u, "%lu", &value) == 1 && value <= UINT32_MAX)
        {
          *content_length = (uint32_t)value;
        }
      else if (strncasecmp(line, "Content-Range:", 14u) == 0 &&
               sscanf(line + 14u, " bytes %lu-", &value) == 1 &&
               value <= UINT32_MAX)
        {
          *range_start = (uint32_t)value;
        }
    }

  return *content_length == UINT32_MAX ? -EPROTO : 0;
}

static int bk7258_ota_http_begin(
  struct bk7258_ota_http_source_priv_s *priv, const char *url,
  bool range, uint32_t offset, uint32_t expected_length)
{
  struct bk7258_ota_http_url_s parsed;
  char request[512];
  uint32_t content_length;
  uint32_t range_start;
  int status;
  int size;
  int ret;

  memset(&parsed, 0, sizeof(parsed));
  ret = bk7258_ota_http_parse_url(url, &parsed);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_http_connect(priv, &parsed);
  if (ret < 0)
    {
      return ret;
    }

  if (expected_length == 0u ||
      (uint64_t)offset + expected_length > (uint64_t)UINT32_MAX + 1u)
    {
      bk7258_ota_http_disconnect(priv);
      return -EINVAL;
    }

  if (range)
    {
      size = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nRange: bytes=%lu-%lu\r\n"
        "Connection: close\r\nUser-Agent: openvela-bk7258-ota/1\r\n\r\n",
        parsed.path, parsed.host, (unsigned long)offset,
        (unsigned long)(offset + expected_length - 1u));
    }
  else
    {
      size = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
        "User-Agent: openvela-bk7258-ota/1\r\n\r\n",
        parsed.path, parsed.host);
    }
  if (size <= 0 || (size_t)size >= sizeof(request))
    {
      return -ENAMETOOLONG;
    }

  ret = bk7258_ota_http_write_tls(priv, (const uint8_t *)request,
                                  (size_t)size);
  if (ret == 0)
    {
      ret = bk7258_ota_http_headers(priv, &status, &content_length,
                                    &range_start);
    }
  if (ret == 0 &&
      ((!range && status != 200) ||
       (range && (status != 206 || range_start != offset)) ||
       content_length != expected_length))
    {
      ret = -EPROTO;
    }
  if (ret < 0)
    {
      bk7258_ota_http_disconnect(priv);
    }
  return ret;
}

static int bk7258_ota_http_download_small(
  struct bk7258_ota_http_source_priv_s *priv, const char *url,
  uint8_t *buffer, size_t capacity, size_t *size)
{
  struct bk7258_ota_http_url_s parsed;
  struct bk7258_ota_http_source_priv_s *p = priv;
  char request[512];
  uint32_t content_length;
  uint32_t range_start;
  int status;
  int request_size;
  int ret;

  memset(&parsed, 0, sizeof(parsed));
  ret = bk7258_ota_http_parse_url(url, &parsed);
  if (ret == 0)
    {
      ret = bk7258_ota_http_connect(p, &parsed);
    }
  request_size = snprintf(
    request, sizeof(request),
    "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
    "User-Agent: openvela-bk7258-ota/1\r\n\r\n",
    parsed.path, parsed.host);
  if (ret == 0 &&
      (request_size <= 0 || (size_t)request_size >= sizeof(request)))
    {
      ret = -ENAMETOOLONG;
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_write_tls(
              p, (const uint8_t *)request, (size_t)request_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_headers(p, &status, &content_length,
                                    &range_start);
    }
  if (ret == 0 && (status != 200 || content_length == 0u ||
                   content_length > capacity))
    {
      ret = -EFBIG;
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_read_tls(p, buffer, content_length);
    }
  if (ret == 0)
    {
      *size = content_length;
    }
  bk7258_ota_http_disconnect(p);
  return ret;
}

static int bk7258_ota_http_load_ca(
  struct bk7258_ota_http_source_priv_s *priv)
{
  struct stat status;
  uint8_t *buffer;
  size_t done = 0u;
  int fd;
  int ret;

  fd = open(priv->ca_path, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }
  if (fstat(fd, &status) < 0 || !S_ISREG(status.st_mode) ||
      status.st_size <= 0 || status.st_size >= BK7258_OTA_HTTP_CA_MAX)
    {
      close(fd);
      return -EINVAL;
    }

  buffer = kmm_malloc((size_t)status.st_size + 1u);
  if (buffer == NULL)
    {
      close(fd);
      return -ENOMEM;
    }
  while (done < (size_t)status.st_size)
    {
      ssize_t count = read(fd, buffer + done,
                           (size_t)status.st_size - done);
      if (count <= 0)
        {
          kmm_free(buffer);
          close(fd);
          return count < 0 ? -errno : -EIO;
        }
      done += (size_t)count;
    }
  close(fd);
  buffer[done] = '\0';
  ret = mbedtls_x509_crt_parse(&priv->ca, buffer, done + 1u);
  kmm_free(buffer);
  return ret == 0 ? 0 : -EKEYREJECTED;
}

static int bk7258_ota_http_open(
  void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct bk7258_ota_http_source_s *source = context;
  struct bk7258_ota_http_source_priv_s *priv = source->priv;
  uint8_t catalog[BK7258_OTA_CATALOG_MAX_SIZE];
  uint8_t signature[BK7258_OTA_CATALOG_MAX_SIGNATURE];
  size_t catalog_size;
  size_t signature_size;
  static const uint8_t personalization[] = "bk7258-ota-source-http-v1";
  int ret;

  if (priv == NULL || manifest == NULL)
    {
      return -EINVAL;
    }

  ret = 0;
  if (priv->secure)
    {
      ret = mbedtls_ctr_drbg_seed(&priv->random, mbedtls_entropy_func,
                                  &priv->entropy, personalization,
                                  sizeof(personalization) - 1u);
      if (ret == 0)
        {
          ret = bk7258_ota_http_load_ca(priv);
        }
      if (ret == 0)
        {
          priv->crypto_ready = true;
        }
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_download_small(
              priv, priv->catalog_url, catalog, sizeof(catalog),
              &catalog_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_download_small(
              priv, priv->signature_url, signature, sizeof(signature),
              &signature_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_catalog_verify(catalog, catalog_size,
                                      signature, signature_size,
                                      &priv->catalog);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_derive_urls(priv);
    }
  if (ret == 0)
    {
      memcpy(manifest, &priv->catalog.manifest, sizeof(*manifest));
    }

  return ret;
}

static int bk7258_ota_http_read_at(
  void *context, enum bk7258_ota_image_e image, uint32_t offset,
  uint8_t *buffer, size_t nbytes)
{
  struct bk7258_ota_http_source_s *source = context;
  struct bk7258_ota_http_source_priv_s *priv = source->priv;
  uint32_t image_size;
  size_t done = 0u;

  if (priv == NULL || buffer == NULL || nbytes == 0u ||
      image < BK7258_OTA_IMAGE_CP || image > BK7258_OTA_IMAGE_AP)
    {
      return -EINVAL;
    }

  image_size = priv->catalog.manifest.image[image].physical_size;
  if ((uint64_t)offset + nbytes > image_size)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&priv->canceled, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  while (done < nbytes)
    {
      uint32_t position = offset + (uint32_t)done;
      uint32_t cache_end = priv->range_cache_start +
                           priv->range_cache_length;
      size_t available;
      size_t copy_size;
      int ret;

      if (!priv->range_cache_valid || priv->range_cache_image != image ||
          position < priv->range_cache_start || position >= cache_end)
        {
          uint32_t length = image_size - position;

          if (length > BK7258_OTA_HTTP_RANGE_CACHE_SIZE)
            {
              length = BK7258_OTA_HTTP_RANGE_CACHE_SIZE;
            }

          priv->range_cache_valid = false;
          ret = bk7258_ota_http_begin(priv, priv->object_url[image], true,
                                      position, length);
          if (ret == 0)
            {
              ret = bk7258_ota_http_read_tls(priv, priv->range_cache,
                                             length);
            }
          bk7258_ota_http_disconnect(priv);
          if (ret < 0)
            {
              return ret;
            }

          priv->range_cache_image = image;
          priv->range_cache_start = position;
          priv->range_cache_length = length;
          priv->range_cache_valid = true;
          cache_end = position + length;
        }

      available = cache_end - position;
      copy_size = nbytes - done;
      if (copy_size > available)
        {
          copy_size = available;
        }
      memcpy(buffer + done,
             priv->range_cache + position - priv->range_cache_start,
             copy_size);
      done += copy_size;
    }

  return 0;
}

static int bk7258_ota_http_checkpoint(
  void *context, const struct bk7258_ota_progress_s *progress)
{
  struct bk7258_ota_http_source_s *source = context;
  struct bk7258_ota_http_source_priv_s *priv = source->priv;

  (void)progress;
  return priv != NULL &&
         __atomic_load_n(&priv->canceled, __ATOMIC_ACQUIRE) ?
         -ECANCELED : 0;
}

static int bk7258_ota_http_cancel(void *context)
{
  struct bk7258_ota_http_source_s *source = context;
  struct bk7258_ota_http_source_priv_s *priv = source->priv;

  if (priv == NULL)
    {
      return -EINVAL;
    }
  __atomic_store_n(&priv->canceled, true, __ATOMIC_RELEASE);
  if (priv->socket_open)
    {
      (void)psock_shutdown(&priv->socket, SHUT_RDWR);
    }
  return 0;
}

static void bk7258_ota_http_close(void *context)
{
  struct bk7258_ota_http_source_s *source = context;
  struct bk7258_ota_http_source_priv_s *priv = source->priv;

  if (priv == NULL)
    {
      return;
    }
  bk7258_ota_http_disconnect(priv);
  kmm_free(priv->range_cache);
  mbedtls_x509_crt_free(&priv->ca);
  mbedtls_ctr_drbg_free(&priv->random);
  mbedtls_entropy_free(&priv->entropy);
  kmm_free(priv);
  source->priv = NULL;
}

static const struct bk7258_ota_source_ops_s g_bk7258_ota_http_ops =
{
  .open = bk7258_ota_http_open,
  .read_at = bk7258_ota_http_read_at,
  .checkpoint = bk7258_ota_http_checkpoint,
  .cancel = bk7258_ota_http_cancel,
  .close = bk7258_ota_http_close,
};

int bk7258_ota_http_source_initialize(
  struct bk7258_ota_http_source_s *source, const char *catalog_url,
  const char *ca_path)
{
  struct bk7258_ota_http_source_priv_s *priv;
  struct bk7258_ota_http_url_s parsed;
  int ret;

  if (source == NULL || catalog_url == NULL || ca_path == NULL ||
      source->priv != NULL)
    {
      return -EINVAL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }
  priv->range_cache = kmm_malloc(BK7258_OTA_HTTP_RANGE_CACHE_SIZE);
  if (priv->range_cache == NULL)
    {
      kmm_free(priv);
      return -ENOMEM;
    }
  ret = bk7258_ota_http_copy(priv->catalog_url,
                             sizeof(priv->catalog_url), catalog_url);
  if (ret == 0)
    {
      memset(&parsed, 0, sizeof(parsed));
      ret = bk7258_ota_http_parse_url(catalog_url, &parsed);
    }
  if (ret == 0)
    {
      priv->secure = parsed.secure;
      if (priv->secure)
        {
          ret = bk7258_ota_http_copy(priv->ca_path,
                                     sizeof(priv->ca_path), ca_path);
        }
      else if (ca_path[0] != '\0')
        {
          ret = -EINVAL;
        }
    }
  if (ret == 0)
    {
      ret = bk7258_ota_http_derive_urls(priv);
    }
  if (ret < 0)
    {
      kmm_free(priv->range_cache);
      kmm_free(priv);
      return ret;
    }

  mbedtls_entropy_init(&priv->entropy);
  mbedtls_ctr_drbg_init(&priv->random);
  mbedtls_x509_crt_init(&priv->ca);
  mbedtls_ssl_init(&priv->ssl);
  mbedtls_ssl_config_init(&priv->config);
  source->priv = priv;
  return 0;
}

const struct bk7258_ota_source_ops_s *bk7258_ota_http_source_ops(void)
{
  return &g_bk7258_ota_http_ops;
}

#endif /* CONFIG_BK7258_OTA_SOURCE_HTTP */
