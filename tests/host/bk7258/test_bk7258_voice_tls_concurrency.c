/* SPDX-License-Identifier: Apache-2.0 */
/* Run from the repository root (no libraries, sockets or generated shims):
 * cc -std=c11 -D_GNU_SOURCE -DFAR= -Wall -Wextra -Werror -pthread \
 *   -ffunction-sections -fdata-sections -Wl, --gc-sections \
 *   -Iapp/bk7258 -I../apps/crypto/mbedtls/mbedtls/include \
 *   tests/host/bk7258/test_bk7258_voice_tls_concurrency.c \
 *   -o /tmp/test_bk7258_voice_tls_concurrency
 * /tmp/test_bk7258_voice_tls_concurrency
 *
 * Compile the actual provider with real mbedTLS declarations.  Unused
 * connection/credential functions are discarded; only SSL I/O and readiness
 * are mocked.  This tests provider scheduling, not TLS interoperability.
 */

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bk7258_voice_tls.h"

static int mock_write(mbedtls_ssl_context *, const unsigned char *, size_t);
static int mock_read(mbedtls_ssl_context *, unsigned char *, size_t);
static int mock_poll(struct pollfd *, nfds_t, int);
static int mock_trylock(pthread_mutex_t *);
static ssize_t mock_recv(int, void *, size_t, int);

#define mbedtls_ssl_write mock_write
#define mbedtls_ssl_read mock_read
#define poll mock_poll
#define pthread_mutex_trylock mock_trylock
#define recv(...) mock_recv(__VA_ARGS__)
#include "bk7258_voice_tls.c"
#undef recv
#undef pthread_mutex_trylock
#undef poll
#undef mbedtls_ssl_read
#undef mbedtls_ssl_write

enum mode_e { WRITE_WAIT, READ_WAIT, DIRECT, TIMEOUT, INTERRUPT,
              SOCKET_DELAY, TLS_DELAY };
static struct
{
  pthread_mutex_t lock;
  pthread_cond_t condition;
  struct bkvoice_tls_s *tls;
  enum mode_e mode;
  int reads;
  int writes;
  int records;
  int direct_result;
  int read_attempts;
  int first_read_lock;
  bool pending;
  bool waiting;
  bool release;
  uint64_t now;
  const unsigned char *write_buffer;
  size_t write_bytes;
} g = { .lock = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER };
static _Thread_local bool g_reader;

#ifdef CONFIG_BK7258_TTS_ISOLATION
static struct observation_s { unsigned int kind; uint64_t begin, end; int result; } obs[64];
static unsigned int obs_count;
static void observe(void *context, unsigned int kind, uint64_t begin,
                      uint64_t end, int result)
{
  (void)context;
  assert(obs_count < 64);
  obs[obs_count++] = (struct observation_s){kind, begin, end, result};
}
#endif

static ssize_t mock_recv(int fd, void *buffer, size_t size, int flags)
{
  (void)fd; (void)buffer; (void)size; (void)flags;
  if (g.reads == 1) { errno = EAGAIN; return -1; }
  return 1;
}

static uint64_t now_ms(void *arg)
{
  (void)arg;
  return __atomic_load_n(&g.now, __ATOMIC_ACQUIRE);
}

static int mock_trylock(pthread_mutex_t *lock)
{
  int ret = pthread_mutex_trylock(lock);
  if (g_reader)
    {
      pthread_mutex_lock(&g.lock);
      if (g.read_attempts++ == 0)
        {
          g.first_read_lock = ret;
        }
      pthread_cond_broadcast(&g.condition);
      pthread_mutex_unlock(&g.lock);
    }
  return ret;
}

static int mock_write(mbedtls_ssl_context *ssl, const unsigned char *buf,
                      size_t bytes)
{
  int ret;
  (void)ssl;
  pthread_mutex_lock(&g.lock);
  g.writes++;
  ret = g.direct_result;
  if (g.mode == WRITE_WAIT || g.mode == TIMEOUT || g.mode == INTERRUPT)
    {
      if (g.writes == 1)
        {
          g.pending = true;
          g.write_buffer = buf;
          g.write_bytes = bytes;
          ret = MBEDTLS_ERR_SSL_WANT_WRITE;
        }
      else
        {
          assert(buf == g.write_buffer && bytes == g.write_bytes);
          g.records++;
          g.pending = false;
          ret = (int)bytes;
        }
    }
  else if (g.mode == READ_WAIT)
    {
      g.records++;
      ret = (int)bytes;
    }
  pthread_cond_broadcast(&g.condition);
  pthread_mutex_unlock(&g.lock);
  return ret;
}

static int mock_read(mbedtls_ssl_context *ssl, unsigned char *buf,
                     size_t bytes)
{
  int ret;
  (void)ssl;
  (void)buf;
  (void)bytes;
  pthread_mutex_lock(&g.lock);
  g.reads++;
  ret = g.direct_result;
  if (g.mode == WRITE_WAIT)
    {
      /* Model TLS 1.2 HelloRequest -> no-renegotiation alert: mbedTLS
       * ssl_msg.c send_alert_message flushes an existing out_left first.
       */
      if (g.pending)
        {
          g.records++;
          g.pending = false;
        }
      ret = 1;
    }
  else if (g.mode == READ_WAIT)
    {
      ret = g.reads == 1 ? MBEDTLS_ERR_SSL_WANT_READ : 1;
    }
  else if (g.mode == SOCKET_DELAY)
    ret = bkvoice_tls_socket_recv(g.tls, buf, bytes);
  else if (g.mode == TLS_DELAY)
    { __atomic_store_n(&g.now, 3000, __ATOMIC_RELEASE); ret = 1; }
  pthread_cond_broadcast(&g.condition);
  pthread_mutex_unlock(&g.lock);
  return ret;
}

static int mock_poll(struct pollfd *fds, nfds_t count, int timeout)
{
  (void)fds;
  (void)timeout;
  if (count == 0)
    {
      /* The actual provider's bounded trylock retry. */
      return poll(NULL, 0, 1);
    }

  pthread_mutex_lock(&g.lock);
  g.waiting = true;
  pthread_cond_broadcast(&g.condition);
  if (g.mode == TIMEOUT)
    {
      __atomic_store_n(&g.now, 100, __ATOMIC_RELEASE);
    }
  else if (g.mode == INTERRUPT)
    {
      assert(bkvoice_tls_interrupt(g.tls) == 0);
    }
  else if (g.mode == SOCKET_DELAY)
    { __atomic_store_n(&g.now, 3000, __ATOMIC_RELEASE); fds->revents = POLLIN; }
  else
    {
      while (!g.release)
        {
          pthread_cond_wait(&g.condition, &g.lock);
        }
    }
  pthread_mutex_unlock(&g.lock);
  return 1;
}

static void setup(struct bkvoice_tls_s *tls, enum mode_e mode, int result)
{
  memset(tls, 0, sizeof(*tls));
  assert(pthread_mutex_init(&tls->crypto_lock, NULL) == 0);
  tls->initialized = true;
  tls->opened = true;
  tls->fd = -1;
  tls->config.now_ms = now_ms;
#ifdef CONFIG_BK7258_TTS_ISOLATION
  obs_count = 0;
  /* Old concurrency cases have two simultaneous callers; they exercise the
   * unchanged provider with observation disabled. */
  if (mode == SOCKET_DELAY || mode == TLS_DELAY) tls->config.observe = observe;
#endif
  g.tls = tls;
  g.mode = mode;
  g.direct_result = result;
  g.reads = g.writes = g.records = g.read_attempts = 0;
  g.first_read_lock = -1;
  g.pending = g.waiting = g.release = false;
  __atomic_store_n(&g.now, 0, __ATOMIC_RELEASE);
}

struct call_s
{
  struct bkvoice_tls_s *tls;
  bool read;
  ssize_t result;
};

static void *call_io(void *arg)
{
  struct call_s *call = arg;
  uint8_t buffer[4] = {0};
  g_reader = call->read;
  call->result = call->read ?
    bkvoice_tls_recv(call->tls, buffer, sizeof(buffer), 100) :
    bkvoice_tls_send(call->tls, buffer, sizeof(buffer), 100);
  return NULL;
}

static void test_write_retry(void)
{
  struct bkvoice_tls_s tls;
  struct call_s writer = { .tls = &tls };
  struct call_s reader = { .tls = &tls, .read = true };
  pthread_t wt;
  pthread_t rt;
  setup(&tls, WRITE_WAIT, 1);
  assert(pthread_create(&wt, NULL, call_io, &writer) == 0);
  pthread_mutex_lock(&g.lock);
  while (!g.waiting)
    {
      pthread_cond_wait(&g.condition, &g.lock);
    }
  pthread_mutex_unlock(&g.lock);
  assert(pthread_create(&rt, NULL, call_io, &reader) == 0);
  pthread_mutex_lock(&g.lock);
  while (g.read_attempts == 0 ||
         (g.first_read_lock == 0 && g.reads == 0))
    {
      pthread_cond_wait(&g.condition, &g.lock);
    }
  g.release = true;
  pthread_cond_broadcast(&g.condition);
  pthread_mutex_unlock(&g.lock);
  assert(pthread_join(wt, NULL) == 0);
  assert(pthread_join(rt, NULL) == 0);
  assert(writer.result == 4 && reader.result == 1);
  assert(g.first_read_lock == EBUSY);
  assert(g.writes == 2 && g.reads == 1 && g.records == 1);
  assert(pthread_mutex_destroy(&tls.crypto_lock) == 0);
}

static void test_idle_read(void)
{
  struct bkvoice_tls_s tls;
  struct call_s reader = { .tls = &tls, .read = true };
  pthread_t rt;
  uint8_t buffer[4] = {0};
  setup(&tls, READ_WAIT, 1);
  assert(pthread_create(&rt, NULL, call_io, &reader) == 0);
  pthread_mutex_lock(&g.lock);
  while (!g.waiting)
    {
      pthread_cond_wait(&g.condition, &g.lock);
    }
  pthread_mutex_unlock(&g.lock);
  assert(bkvoice_tls_send(&tls, buffer, sizeof(buffer), 100) == 4);
  pthread_mutex_lock(&g.lock);
  g.release = true;
  pthread_cond_broadcast(&g.condition);
  pthread_mutex_unlock(&g.lock);
  assert(pthread_join(rt, NULL) == 0);
  assert(reader.result == 1 && g.records == 1);
  assert(pthread_mutex_destroy(&tls.crypto_lock) == 0);
}

static void test_terminal(enum mode_e mode, bool read, int ssl_result,
                          int expected)
{
  struct bkvoice_tls_s tls;
  struct call_s call = { .tls = &tls, .read = read };
  uint8_t buffer[4] = {0};
  int reads;
  int writes;
  setup(&tls, mode, ssl_result);
  call_io(&call);
  g_reader = false;
  assert(call.result == expected);
  reads = g.reads;
  writes = g.writes;
  assert(bkvoice_tls_send(&tls, buffer, sizeof(buffer), 100) < 0);
  assert(bkvoice_tls_recv(&tls, buffer, sizeof(buffer), 100) < 0);
  assert(g.reads == reads && g.writes == writes);
  assert(pthread_mutex_destroy(&tls.crypto_lock) == 0);
}

int main(void)
{
  /* A failed ordering assertion must terminate instead of hanging CI. */
  alarm(10);
  test_write_retry();
  test_idle_read();
  test_terminal(DIRECT, true, MBEDTLS_ERR_SSL_INTERNAL_ERROR, -EIO);
  test_terminal(DIRECT, false, MBEDTLS_ERR_SSL_INTERNAL_ERROR, -EIO);
  test_terminal(DIRECT, true, 0, 0);
  test_terminal(DIRECT, true, MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY, 0);
  test_terminal(DIRECT, true, MBEDTLS_ERR_SSL_WANT_WRITE, -EIO);
  test_terminal(TIMEOUT, false, 0, -ETIMEDOUT);
  test_terminal(INTERRUPT, false, 0, -ECANCELED);
#ifdef CONFIG_BK7258_TTS_ISOLATION
  for (int mode = SOCKET_DELAY; mode <= TLS_DELAY; mode++)
    {
      struct bkvoice_tls_s tls;
      unsigned char buffer[4];
      setup(&tls, mode, 1);
      assert(bkvoice_tls_recv(&tls, buffer, sizeof(buffer), 5000) == 1);
      bool wait = false, work = false, empty = false, data = false;
      for (unsigned int i = 0; i < obs_count; i++)
        {
          assert(obs[i].end >= obs[i].begin);
          uint64_t gap = obs[i].end - obs[i].begin;
          wait |= obs[i].kind == 10 && gap == 3000;
          work |= obs[i].kind == 13 && gap == 3000;
          empty |= obs[i].kind == 9 && obs[i].result == -EAGAIN;
          data |= obs[i].kind == 9 && obs[i].result == 1;
        }
      if (mode == SOCKET_DELAY) assert(wait && !work && empty && data);
      else assert(!wait && work && !empty && !data);
      assert(!tls.faulted);
      assert(pthread_mutex_destroy(&tls.crypto_lock) == 0);
    }
#endif
  puts("voice TLS concurrency: 9 cases PASS");
  return 0;
}
