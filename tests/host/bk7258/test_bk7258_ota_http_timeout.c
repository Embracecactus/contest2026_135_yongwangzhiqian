/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic BIO/deadline fixture for test_bk7258_ota_transport.py. */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>

#define MBEDTLS_ERR_SSL_WANT_READ   -0x6900
#define MBEDTLS_ERR_SSL_WANT_WRITE  -0x6880
#define MBEDTLS_ERR_SSL_TIMEOUT     -0x6800
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004e
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004c

struct socket
{
  int unused;
};

struct bk7258_ota_http_source_priv_s
{
  struct socket socket;
  struct timespec io_deadline;
  int ssl;
  bool canceled;
  bool tls_active;
};

static struct bk7258_ota_http_source_priv_s g_priv;
static int64_t g_now_ns;
static int64_t g_step_ns;
static int g_clock_error;
static int g_socket_result;
static int g_tls_result;
static int g_setsockopt_result;
static unsigned int g_socket_calls;
static unsigned int g_tls_calls;
static unsigned int g_want_count;
static bool g_cancel_during_io;
static bool g_fragmented_record;
static struct timeval g_timeout;

static int mock_clock_gettime(clockid_t id, struct timespec *now)
{
  assert(id == CLOCK_MONOTONIC);
  now->tv_sec = g_now_ns / 1000000000;
  now->tv_nsec = g_now_ns % 1000000000;
  g_now_ns += g_step_ns;
  return g_clock_error;
}

static int psock_setsockopt(struct socket *socket, int level, int option,
                           const void *data, socklen_t size)
{
  assert(socket == &g_priv.socket && level == SOL_SOCKET);
  assert(option == SO_RCVTIMEO || option == SO_SNDTIMEO);
  assert(size == sizeof(g_timeout));
  memcpy(&g_timeout, data, size);
  assert(g_timeout.tv_sec > 0 || g_timeout.tv_usec > 0);
  return g_setsockopt_result;
}

static ssize_t socket_io(struct socket *socket, size_t size, int flags)
{
  assert(socket == &g_priv.socket && flags == 0);
  assert(++g_socket_calls < 100);
  if (g_cancel_during_io)
    {
      g_priv.canceled = true;
    }
  if (g_socket_result <= 0)
    {
      return g_socket_result;
    }
  return size < (size_t)g_socket_result ? (ssize_t)size : g_socket_result;
}

static ssize_t psock_recv(struct socket *socket, void *buffer,
                          size_t size, int flags)
{
  (void)buffer;
  return socket_io(socket, size, flags);
}

static ssize_t psock_send(struct socket *socket, const void *buffer,
                          size_t size, int flags)
{
  (void)buffer;
  return socket_io(socket, size, flags);
}

static int mbedtls_ssl_read(int *ssl, void *buffer, size_t size);
static int mbedtls_ssl_write(int *ssl, const void *buffer, size_t size);
static int mbedtls_ssl_handshake(int *ssl);

#define clock_gettime mock_clock_gettime
#include "http_io.inc"
#undef clock_gettime

static int tls_io(int *ssl, void *buffer, size_t size, bool send)
{
  int ret;

  assert(ssl == &g_priv.ssl);
  assert(++g_tls_calls < 100);
  if (g_want_count > 0)
    {
      g_want_count--;
      return g_tls_result;
    }

  /* A single TLS call may consume many tiny BIO fragments without returning
   * WANT_* to the outer loop.  The BIO must enforce the same deadline.
   */

  do
    {
      ret = send ? bk7258_ota_http_tls_send(&g_priv, buffer, size) :
                   bk7258_ota_http_tls_recv(&g_priv, buffer, size);
    }
  while (g_fragmented_record && ret > 0);
  return ret;
}

static int mbedtls_ssl_read(int *ssl, void *buffer, size_t size)
{
  return tls_io(ssl, buffer, size, false);
}

static int mbedtls_ssl_write(int *ssl, const void *buffer, size_t size)
{
  return tls_io(ssl, (void *)buffer, size, true);
}

static int mbedtls_ssl_handshake(int *ssl)
{
  uint8_t buffer = 0;
  int ret = tls_io(ssl, &buffer, sizeof(buffer), false);

  return ret > 0 ? 0 : ret;
}

static void reset_fixture(void)
{
  memset(&g_priv, 0, sizeof(g_priv));
  g_priv.tls_active = true;
  g_priv.io_deadline.tv_sec = 15;
  g_now_ns = 0;
  g_step_ns = 0;
  g_clock_error = 0;
  g_socket_result = 1;
  g_tls_result = MBEDTLS_ERR_SSL_WANT_READ;
  g_setsockopt_result = 0;
  g_socket_calls = 0;
  g_tls_calls = 0;
  g_want_count = 0;
  g_cancel_during_io = false;
  g_fragmented_record = false;
}

static int transfer(unsigned int operation, uint8_t *buffer, size_t size)
{
  if (operation == 0)
    {
      return bk7258_ota_http_read_tls(&g_priv, buffer, size);
    }
  if (operation == 1)
    {
      return bk7258_ota_http_write_tls(&g_priv, buffer, size);
    }
  return bk7258_ota_http_handshake(&g_priv);
}

int main(void)
{
  static const int timeouts[] = {-EAGAIN, -EWOULDBLOCK, -ETIMEDOUT};
  static const int wants[] =
    {MBEDTLS_ERR_SSL_WANT_READ, MBEDTLS_ERR_SSL_WANT_WRITE};
  uint8_t buffer[4] = {0};
  unsigned int operation;
  unsigned int index;

  for (operation = 0; operation < 3; operation++)
    {
      for (index = 0; index < sizeof(timeouts) / sizeof(timeouts[0]); index++)
        {
          reset_fixture();
          g_socket_result = timeouts[index];
          assert(transfer(operation, buffer, sizeof(buffer)) == -ETIMEDOUT);
          assert(g_socket_calls == 1 && g_tls_calls == 1);
        }

      for (index = 0; index < sizeof(wants) / sizeof(wants[0]); index++)
        {
          reset_fixture();
          g_tls_result = wants[index];
          g_want_count = 100;
          g_step_ns = 1000000000;
          assert(transfer(operation, buffer, sizeof(buffer)) == -ETIMEDOUT);
          assert(g_socket_calls == 0 && g_tls_calls <= 15);

          reset_fixture();
          g_tls_result = wants[index];
          g_want_count = 2;
          assert(transfer(operation, buffer, sizeof(buffer)) == 0);
          assert(g_tls_calls > 2);
        }

      reset_fixture();
      g_priv.canceled = true;
      assert(transfer(operation, buffer, sizeof(buffer)) == -ECANCELED);
      assert(g_socket_calls == 0 && g_tls_calls == 0);

      reset_fixture();
      g_cancel_during_io = true;
      g_socket_result = -ECONNRESET;
      assert(transfer(operation, buffer, sizeof(buffer)) == -ECANCELED);

      reset_fixture();
      g_fragmented_record = true;
      g_step_ns = 1000000000;
      assert(transfer(operation, buffer, sizeof(buffer)) == -ETIMEDOUT);
      assert(g_tls_calls == 1 && g_socket_calls < 15);

      reset_fixture();
      g_clock_error = -1;
      assert(transfer(operation, buffer, sizeof(buffer)) == -EIO);
      assert(g_socket_calls == 0 && g_tls_calls == 0);
    }

  for (operation = 0; operation < 2; operation++)
    {
      reset_fixture();
      g_priv.tls_active = false;
      g_socket_result = -EAGAIN;
      assert(transfer(operation, buffer, sizeof(buffer)) == -ETIMEDOUT);
      assert(g_socket_calls == 1);

      reset_fixture();
      g_step_ns = 2000000000;
      assert(transfer(operation, buffer, sizeof(buffer)) == 0);
      assert(g_socket_calls == sizeof(buffer));
      assert(transfer(operation, buffer, sizeof(buffer)) == -ETIMEDOUT);

      reset_fixture();
      g_setsockopt_result = -EINVAL;
      assert(transfer(operation, buffer, sizeof(buffer)) == -EIO);
      assert(g_socket_calls == 0);
      g_priv.tls_active = false;
      assert(transfer(operation, buffer, sizeof(buffer)) == -EINVAL);
      assert(g_socket_calls == 0);

      reset_fixture();
      g_socket_result = 0;
      assert(transfer(operation, buffer, sizeof(buffer)) == -ECONNRESET);
    }

  reset_fixture();
  g_now_ns = 15000000000 - 1;
  assert(bk7258_ota_http_tls_recv(&g_priv, buffer, sizeof(buffer)) == 1);
  assert(g_timeout.tv_sec == 0 && g_timeout.tv_usec == 1);
  g_now_ns++;
  assert(bk7258_ota_http_tls_recv(&g_priv, buffer, sizeof(buffer)) ==
         MBEDTLS_ERR_SSL_TIMEOUT);
  assert(g_socket_calls == 1);
  puts("PASS: HTTP timeout, bounded WANT/fragmentation, "
       "shared deadline, cancel");
  return 0;
}
