/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_http.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>
#include <mbedtls/platform_util.h>
#if defined(__NuttX__) && defined(CONFIG_NET_STATISTICS) && \
    defined(CONFIG_NET_TCP)
#  include <nuttx/net/net.h>
#  include <nuttx/net/netstats.h>
#  define BKCLOUD_HAVE_TCP_STATS 1
#endif
#if defined(__NuttX__) && defined(CONFIG_MM_IOB) && \
    defined(CONFIG_FS_PROCFS) && !defined(CONFIG_DISABLE_MOUNTPOINT) && \
    !defined(CONFIG_FS_PROCFS_EXCLUDE_IOBINFO)
#  include <nuttx/mm/iob.h>
#  define BKCLOUD_HAVE_IOB_STATS 1
#endif

static uint64_t monotonic_ms(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
  return (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
}

#ifdef BKCLOUD_HAVE_TCP_STATS
static bool read_tcp_stats(struct tcp_stats_s *stats)
{
  if (net_lock() < 0) return false;
  *stats = g_netstats.tcp;
  net_unlock();
  return true;
}
#endif

static int connect_tls(void *context, const char *host, const char *port,
                       unsigned int timeout,
                       struct webclient_tls_connection **connection)
{
  struct bkcloud_http_s *http = context;
  char expected[6];
  (void)timeout; /* Absolute deadline belongs to the verified TLS provider. */
  *connection = NULL;
  snprintf(expected, sizeof(expected), "%u", http->config->port);
  if (http->connected || strcmp(host, http->config->host) ||
      strcmp(port, expected)) return -EPERM;
  uint64_t started = monotonic_ms();
  int ret = http->tls->open_verified(http->tls_context, host,
                                     http->config->port, http->deadline_ms);
  http->connect_ms += monotonic_ms() - started;
  if (ret == 0)
    {
      http->connected = true;
      *connection = (struct webclient_tls_connection *)http;
    }
  return ret;
}

static ssize_t send_tls(void *context,
                        struct webclient_tls_connection *connection,
                        const void *data, size_t size)
{
  struct bkcloud_http_s *http = context;
  if (connection != (struct webclient_tls_connection *)http ||
      !http->connected) return -ENOTCONN;
  uint64_t started = monotonic_ms();
  ssize_t ret = http->tls->send(http->tls_context, data, size, http->deadline_ms);
  http->send_ms += monotonic_ms() - started;
  http->send_calls++;
  return ret;
}

static ssize_t recv_tls(void *context,
                        struct webclient_tls_connection *connection,
                        void *data, size_t size)
{
  struct bkcloud_http_s *http = context;
  if (connection != (struct webclient_tls_connection *)http ||
      !http->connected) return -ENOTCONN;
  uint64_t started = monotonic_ms();
  ssize_t ret = http->tls->recv(http->tls_context, data, size, http->deadline_ms);
  uint64_t elapsed = monotonic_ms() - started;
  http->receive_ms += elapsed;
  http->receive_calls++;
  if (elapsed >= 200u) http->slow_receives++;
  if (elapsed > http->max_receive_ms) http->max_receive_ms = elapsed;
#ifdef BKCLOUD_HAVE_IOB_STATS
  if (ret < 0 && ret != -ECANCELED)
    {
      /* Observe network buffer pressure before webclient closes the socket
       * and returns its queued buffers. No packet or endpoint is retained.
       */
      struct iob_stats_s stats;
      iob_getstats(&stats);
      syslog(LOG_WARNING, "BKVOICE HTTP receive ret=%d iob=%u/%u "
             "waiting=%u throttle=%u\n", (int)ret,
             (unsigned int)stats.nfree, (unsigned int)stats.ntotal,
             (unsigned int)stats.nwait, (unsigned int)stats.nthrottle);
    }
#endif
  return ret;
}

static int close_tls(void *context,
                     struct webclient_tls_connection *connection)
{
  struct bkcloud_http_s *http = context;
  if (connection != (struct webclient_tls_connection *)http) return -EINVAL;
  if (!http->connected) return 0;
  http->connected = false;
  return http->tls->close(http->tls_context);
}

static const struct webclient_tls_ops g_tls =
{
  .connect = connect_tls,
  .send = send_tls,
  .recv = recv_tls,
  .close = close_tls,
};

static bool media_type(const char *value, const char *type)
{
  size_t n = strlen(type);
  return strncasecmp(value, type, n) == 0 &&
    (value[n] == 0 || value[n] == ';' || value[n] == '\r' || value[n] == '\n');
}

static int header(const char *line, bool truncated, void *context)
{
  struct bkcloud_http_s *http = context;
  /* Called before webclient processes Location or follows a redirect. */
  if (truncated) return -E2BIG;
  if (strncasecmp(line, "location:", 9) == 0) return -EPERM;
  if (strncasecmp(line, "content-type:", 13) == 0)
    {
      const char *value = line + 13;
      while (*value == ' ' || *value == '\t') value++;
      http->event_stream = media_type(value, "text/event-stream");
      http->pcm_stream = media_type(value, "audio/pcm") || media_type(value, "application/octet-stream");
    }
  return 0;
}

static int response_error(int status)
{
  if (status >= 200 && status < 300) return 0;
  return status == 401 || status == 403 ? -EACCES : -EREMOTEIO;
}

static int sink(char **buffer, int offset, int end, int *length, void *context)
{
  struct bkcloud_http_s *http = context;
  if (offset < 0 || end < offset || end > *length) return -EPROTO;
  /* A provider's error document is neither model output nor playable audio.
   * Reject it before applying the success payload limit or calling consumers.
   */
  int error = response_error(http->active->http_status);
  if (error < 0) return error;
  size_t count = end - offset;
  if (http->consume != NULL)
    {
      if (count > http->capacity - http->received) return -E2BIG;
      http->received += count;
      if (http->pcm_response ? !http->pcm_stream : !http->event_stream) return -EPROTO;
      uint64_t started = monotonic_ms();
      int ret = http->consume(http->consume_context, *buffer + offset, count);
      http->consume_ms += monotonic_ms() - started;
      if (ret == BKCLOUD_HTTP_STREAM_COMPLETE && !http->pcm_response)
        {
          /* webclient's sink error path closes the connection. Keep a
           * separate completion fact so an actual cancellation stays an
           * error, even though both terminate its receive loop.
           */
          http->stream_complete = true;
          return -ECANCELED;
        }
      return ret > 0 ? -EIO : ret;
    }
  if (count >= http->capacity - http->received) return -E2BIG;
  memcpy(http->response + http->received, *buffer + offset, count);
  http->received += count;
  http->response[http->received] = 0;
  return 0;
}

static int post(struct bkcloud_http_s *http,
                     const struct bkcloud_config_s *config,
                     const char *endpoint,
                     const struct bkvoice_wss_tls_ops_s *tls,
                     void *tls_context, uint64_t deadline_ms,
                     webclient_body_callback_t body, void *body_context,
                     size_t body_size, char *response, size_t capacity,
                     bkcloud_write_t consume, void *consume_context, bool pcm_response)
{
  struct webclient_context client;
  char url[320];
  int ret;
  if (capacity == 0 || (consume == NULL && response == NULL)) return -EINVAL;
  if (response != NULL) memset(response, 0, capacity);
  if (http == NULL || config == NULL || endpoint == NULL || tls == NULL ||
      tls->open_verified == NULL || tls->send == NULL || tls->recv == NULL ||
      tls->close == NULL || body == NULL || body_size == 0 ||
      (consume == NULL && capacity > 65537) || deadline_ms == 0) return -EINVAL;
  memset(http, 0, sizeof(*http));
  /* Public config is decoded before entry; enforce bounded strings here too
   * to avoid accidental header injection from an adapter-created config.
   */
  size_t host = strnlen(config->host, sizeof(config->host));
  size_t path = strnlen(config->base_path, sizeof(config->base_path));
  size_t key = strnlen(config->api_key, sizeof(config->api_key));
  if (host == 0 || host == sizeof(config->host) || path == 0 ||
      path == sizeof(config->base_path) || config->base_path[0] != '/' ||
      key == 0 || key > BKCLOUD_KEY_MAX || config->port == 0) return -EINVAL;
  for (size_t i = 0; i < host; i++)
    if (!((config->host[i] >= 'a' && config->host[i] <= 'z') ||
          (config->host[i] >= 'A' && config->host[i] <= 'Z') ||
          (config->host[i] >= '0' && config->host[i] <= '9') ||
          config->host[i] == '.' || config->host[i] == '-')) return -EINVAL;
  for (size_t i = 0; i < path; i++)
    if (!((config->base_path[i] >= 'a' && config->base_path[i] <= 'z') ||
          (config->base_path[i] >= 'A' && config->base_path[i] <= 'Z') ||
          (config->base_path[i] >= '0' && config->base_path[i] <= '9') ||
          config->base_path[i] == '/' || config->base_path[i] == '_' ||
          config->base_path[i] == '.' || config->base_path[i] == '-'))
      return -EINVAL;
  if (strstr(config->base_path, "..")) return -EINVAL;
  for (size_t i = 0; i < key; i++)
    if ((unsigned char)config->api_key[i] < 33 ||
        (unsigned char)config->api_key[i] > 126) return -EINVAL;
  size_t suffix = strnlen(endpoint, 64);
  if (suffix == 0 || suffix == 64 || endpoint[0] == '/') return -EINVAL;
  for (size_t i = 0; i < suffix; i++)
    if (!((endpoint[i] >= 'a' && endpoint[i] <= 'z') ||
          endpoint[i] == '/' || endpoint[i] == '_')) return -EINVAL;
  ret = snprintf(url, sizeof(url), "https://%s:%u%s%s%s", config->host,
                  config->port, config->base_path,
                  config->base_path[path - 1] == '/' ? "" : "/", endpoint);
  if (ret < 0 || (size_t)ret >= sizeof(url)) return -ENAMETOOLONG;
  http->config = config;
  http->tls = tls;
  http->tls_context = tls_context;
  http->deadline_ms = deadline_ms;
  http->response = response;
  http->capacity = capacity;
  http->consume = consume;
  http->pcm_response = pcm_response;
  http->consume_context = consume_context;
  http->active = &client;
  snprintf(http->authorization, sizeof(http->authorization),
            "Authorization: Bearer %s", config->api_key);
  const char *headers[] = {http->authorization,
    "Content-Type: application/json", consume == NULL ?
    "Accept: application/json" : pcm_response ? "Accept: audio/pcm, application/octet-stream" : "Accept: text/event-stream"};
  webclient_set_defaults(&client);
  client.protocol_version = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
  client.method = "POST";
  client.url = url;
  client.headers = headers;
  client.nheaders = sizeof(headers) / sizeof(headers[0]);
  client.buffer = http->buffer;
  client.buflen = sizeof(http->buffer);
  client.bodylen = body_size;
  client.body_callback = body;
  client.body_callback_arg = body_context;
  client.sink_callback = sink;
  client.sink_callback_arg = http;
  client.header_callback = header;
  client.header_callback_arg = http;
  client.tls_ops = &g_tls;
  client.tls_ctx = http;
#ifdef BKCLOUD_HAVE_TCP_STATS
  struct tcp_stats_s tcp_before;
  bool have_tcp_before = read_tcp_stats(&tcp_before);
#endif
  uint64_t started = monotonic_ms();
  ret = webclient_perform(&client);
  uint64_t elapsed = monotonic_ms() - started;
  http->status = client.http_status;
  if (http->connected) close_tls(http, (void *)http);
  if (ret == -ECANCELED && http->stream_complete) ret = 0;
  if (ret == 0) ret = response_error(http->status);
  /* One bounded summary per request; never include credentials, user text,
   * response bodies or audio. Separate remote waits from local backpressure.
   */
  syslog(LOG_INFO,
         "BKVOICE HTTP request=%s status=%u ret=%d bytes=%lu receive_ms=%llu consume_ms=%llu read_calls=%lu slow_reads=%lu max_read_ms=%llu body_bytes=%zu total_ms=%llu connect_ms=%llu send_ms=%llu send_calls=%lu\n",
         endpoint, http->status, ret, (unsigned long)http->received,
         (unsigned long long)http->receive_ms,
         (unsigned long long)http->consume_ms,
         (unsigned long)http->receive_calls,
         (unsigned long)http->slow_receives,
         (unsigned long long)http->max_receive_ms, body_size,
         (unsigned long long)elapsed,
         (unsigned long long)http->connect_ms,
         (unsigned long long)http->send_ms,
         (unsigned long)http->send_calls);
#ifdef BKCLOUD_HAVE_TCP_STATS
  struct tcp_stats_s tcp_after;
  if (have_tcp_before && read_tcp_stats(&tcp_after))
    {
      /* Native stack counters cover all TCP traffic during this request,
       * including other clients. Keep only aggregate deltas, never packets.
       */
      syslog(LOG_INFO, "BKVOICE HTTP system_tcp_delta rx=%u tx=%u drop=%u "
             "checksum=%u ack=%u retransmit=%u reset=%u\n",
             (unsigned int)(net_stats_t)(tcp_after.recv - tcp_before.recv),
             (unsigned int)(net_stats_t)(tcp_after.sent - tcp_before.sent),
             (unsigned int)(net_stats_t)(tcp_after.drop - tcp_before.drop),
             (unsigned int)(net_stats_t)(tcp_after.chkerr - tcp_before.chkerr),
             (unsigned int)(net_stats_t)(tcp_after.ackerr - tcp_before.ackerr),
             (unsigned int)(net_stats_t)(tcp_after.rexmit - tcp_before.rexmit),
             (unsigned int)(net_stats_t)(tcp_after.rst - tcp_before.rst));
    }
#endif
  mbedtls_platform_zeroize(http->authorization, sizeof(http->authorization));
  mbedtls_platform_zeroize(http->buffer, sizeof(http->buffer));
  http->config = NULL;
  http->response = NULL;
  http->tls = NULL;
  http->tls_context = NULL;
  http->active = NULL;
  http->consume = NULL;
  http->consume_context = NULL;
  if (ret != 0)
    { if (response != NULL) memset(response, 0, capacity); http->received = 0; }
  return ret;
}

int bkcloud_http_get(struct bkcloud_http_s *http,
                    const struct bkcloud_config_s *config, const char *url,
                    const struct bkvoice_wss_tls_ops_s *tls,
                    void *tls_context, uint64_t deadline_ms,
                    char *response, size_t capacity)
{
  struct webclient_context client;
  if (!http || !config || !url || strncmp(url, "https://", 8) ||
      !tls || !tls->open_verified || !tls->send || !tls->recv || !tls->close ||
      !deadline_ms || !response || capacity < 2 || capacity > 131073u)
    return -EINVAL;
  memset(http, 0, sizeof(*http));
  http->config = config;
  http->tls = tls;
  http->tls_context = tls_context;
  http->deadline_ms = deadline_ms;
  http->response = response;
  http->capacity = capacity;
  http->active = &client;
  response[0] = 0;
  webclient_set_defaults(&client);
  client.protocol_version = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
  client.method = "GET";
  client.url = url;
  client.buffer = http->buffer;
  client.buflen = sizeof(http->buffer);
  client.sink_callback = sink;
  client.sink_callback_arg = http;
  client.header_callback = header;
  client.header_callback_arg = http;
  client.tls_ops = &g_tls;
  client.tls_ctx = http;
  int ret = webclient_perform(&client);
  http->status = client.http_status;
  if (http->connected) close_tls(http, (void *)http);
  if (!ret && http->status != 200) ret = -EREMOTEIO;
  http->config = NULL;
  http->response = NULL;
  http->tls = NULL;
  http->tls_context = NULL;
  http->active = NULL;
  if (ret) { memset(response, 0, capacity); http->received = 0; }
  return ret;
}

int bkcloud_http_post(struct bkcloud_http_s *http,
                     const struct bkcloud_config_s *config,
                     const char *endpoint,
                     const struct bkvoice_wss_tls_ops_s *tls,
                     void *tls_context, uint64_t deadline_ms,
                     webclient_body_callback_t body, void *body_context,
                     size_t body_size, char *response, size_t capacity)
{
  return post(http, config, endpoint, tls, tls_context, deadline_ms, body,
              body_context, body_size, response, capacity, NULL, NULL, false);
}

int bkcloud_http_events(struct bkcloud_http_s *http,
                       const struct bkcloud_config_s *config,
                       const struct bkvoice_wss_tls_ops_s *tls,
                       void *tls_context, uint64_t deadline_ms,
                       const char *request, size_t request_size,
                       bkcloud_write_t consume, void *context, size_t limit)
{
  struct webclient_context body;
  if (consume == NULL || request == NULL || limit > 8u * 1024u * 1024u)
    return -EINVAL;
  webclient_set_defaults(&body);
  webclient_set_static_body(&body, request, request_size);
  return post(http, config, "chat/completions", tls, tls_context, deadline_ms,
              body.body_callback, body.body_callback_arg, body.bodylen,
              NULL, limit, consume, context, false);
}

int bkcloud_http_pcm(struct bkcloud_http_s *http,
                     const struct bkcloud_config_s *config,
                     const struct bkvoice_wss_tls_ops_s *tls,
                     void *tls_context, uint64_t deadline_ms,
                     const char *request, size_t request_size,
                     bkcloud_write_t consume, void *context, size_t limit)
{
  struct webclient_context body;
  if (!consume || !request || !limit || limit > 8u * 1024u * 1024u) return -EINVAL;
  webclient_set_defaults(&body);
  webclient_set_static_body(&body, request, request_size);
  int ret = post(http, config, "audio/speech", tls, tls_context, deadline_ms,
      body.body_callback, body.body_callback_arg, body.bodylen,
      NULL, limit, consume, context, true);
  if (ret) return ret;
  if (!http->received) return -ENODATA;
  return (http->received & 1u) ? -EBADMSG : 0;
}
