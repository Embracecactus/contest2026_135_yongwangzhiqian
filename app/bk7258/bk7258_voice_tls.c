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
#include <syslog.h>
#include <unistd.h>
#ifdef __NuttX__
#  include <sys/ioctl.h>
#  include <nuttx/clock.h>
#  include <nuttx/semaphore.h>
#  ifdef CONFIG_BK7258_ENGINEERING_TEST
#    include <nuttx/sched.h>
#  endif
#endif

#include <mbedtls/net_sockets.h>
#include <mbedtls/sha1.h>

#if !defined(MBEDTLS_HAVE_TIME_DATE)
#  error "BKVoice TLS requires certificate validity-date verification"
#endif

#define BKVOICE_TLS_POLL_MS 20u

#ifdef CONFIG_BK7258_TTS_ISOLATION
#define OBS_NOW(tls) ((tls)->config.observe ? \
  (tls)->config.now_ms((tls)->config.clock_context) : 0)
static void tls_observe(struct bkvoice_tls_s *tls, unsigned int kind,
                         uint64_t begin, uint64_t end, int result)
{
  if (tls->config.observe)
    tls->config.observe(tls->config.observe_context, kind, begin, end, result);
}
#define OBS(tls, kind, begin, end, result) \
  tls_observe(tls, kind, begin, end, result)
#ifdef __NuttX__
struct isolation_poll_s
{
  sem_t sem; /* poll_default_cb uses pfd.arg as sem_t *. */
  struct bkvoice_tls_s *tls;
  uint32_t ready; /* Same bounded boot-ms range as BVA2; lock-free on ARM32. */
};
static void isolation_poll_cb(struct pollfd *pfd)
{
  struct isolation_poll_s *sample = pfd->arg;
  uint32_t expected = 0;
  uint32_t now = (uint32_t)OBS_NOW(sample->tls);
  (void)__atomic_compare_exchange_n(&sample->ready, &expected, now, false,
                                     __ATOMIC_RELEASE, __ATOMIC_RELAXED);
  poll_default_cb(pfd);
}
#endif
#else
#define OBS_NOW(tls) 0
#define OBS(tls, kind, begin, end, result) ((void)0)
#endif

#ifdef CONFIG_BK7258_ENGINEERING_TEST
enum trace_phase_e
{
  TRACE_IDLE, TRACE_LOCK, TRACE_SOCKET, TRACE_NONBLOCK, TRACE_CONNECT,
  TRACE_SO_ERROR, TRACE_SSL, TRACE_POLL_ADD, TRACE_POLL_WAIT, TRACE_POLL_DEL,
  TRACE_SEND, TRACE_RECV, TRACE_CLOSE, TRACE_FREE, TRACE_SESSION
};

static uint32_t trace_phase(struct bkvoice_tls_s *tls, unsigned int lane,
                             unsigned int phase)
{
  uint32_t stamp = (uint32_t)(tls->config.now_ms(tls->config.clock_context)
                              >> 4);
#ifdef __NuttX__
  __atomic_store_n(&tls->trace_owner[lane], (uint32_t)gettid(), __ATOMIC_RELEASE);
#endif
  return __atomic_exchange_n(&tls->trace[lane], (stamp << 4) | phase,
                              __ATOMIC_ACQ_REL);
}

#define TRACE(tls, lane, phase) ((void)trace_phase(tls, lane, phase))
#define TRACE_SAVE(tls, lane, phase) \
  uint32_t trace_saved = trace_phase(tls, lane, phase)
#define TRACE_RESTORE(tls, lane) \
  __atomic_store_n(&(tls)->trace[lane], trace_saved, __ATOMIC_RELEASE)

#ifdef __NuttX__
struct trace_task_s
{
  pid_t pid;
  int state;
  int priority;
  int lockcount;
  unsigned int affinity;
  unsigned int cpu;
  bool timer_active;
  uint64_t expires;
  unsigned int peers;
  struct
  {
    pid_t pid;
    int state;
    int priority;
    int lockcount;
    unsigned int cpu;
    unsigned int affinity;
    char name[17];
  } ready[16];
};

static void trace_task(struct tcb_s *tcb, void *context)
{
  struct trace_task_s *snapshot = context;
  if (tcb->task_state >= TSTATE_TASK_PENDING &&
      tcb->task_state <= TSTATE_TASK_RUNNING && snapshot->peers < 16)
    {
      unsigned int i = snapshot->peers++;
      snapshot->ready[i].pid = tcb->pid;
      snapshot->ready[i].state = tcb->task_state;
      snapshot->ready[i].priority = tcb->sched_priority;
      snapshot->ready[i].lockcount = tcb->lockcount;
      snapshot->ready[i].cpu = tcb->cpu;
      snapshot->ready[i].affinity = (unsigned int)tcb->affinity;
#if CONFIG_TASK_NAME_SIZE > 0
      size_t n = sizeof(snapshot->ready[i].name) - 1;
      if (n > CONFIG_TASK_NAME_SIZE) n = CONFIG_TASK_NAME_SIZE;
      memcpy(snapshot->ready[i].name, tcb->name, n);
#endif
    }
  if (tcb->pid != snapshot->pid) return;
  /* foreach pins the TCB. Do not dereference waitobj/stack: their lifetime
   * belongs to the waiting owner and may end during this observation.
   */
  snapshot->state = tcb->task_state;
  snapshot->cpu = tcb->cpu;
  snapshot->priority = tcb->sched_priority;
  snapshot->lockcount = tcb->lockcount;
  snapshot->affinity = (unsigned int)tcb->affinity;
  snapshot->timer_active = tcb->waitdog.func != NULL;
  snapshot->expires = (uint64_t)tcb->waitdog.expired;
}
#endif

void bkvoice_tls_trace_wait(struct bkvoice_tls_s *tls, const char *backend,
                            uint32_t now_ms)
{
  static const char *const names[] =
    { "idle", "crypto-lock", "socket", "nonblock", "connect", "so-error",
      "ssl", "poll-add", "poll-wait", "poll-del", "send", "recv", "close",
      "ssl-free", "session" };
  for (unsigned int lane = 0; lane < 3; lane++)
    {
      uint32_t word = __atomic_load_n(&tls->trace[lane], __ATOMIC_ACQUIRE);
      unsigned int phase = word & 15u;
      uint32_t elapsed = (((now_ms >> 4) - (word >> 4)) & 0x0fffffffu) << 4;
      if (!phase || phase >= sizeof(names) / sizeof(names[0]) ||
          elapsed < 5000u || now_ms - tls->trace_reported_ms[lane] < 5000u)
        continue;
      tls->trace_reported_ms[lane] = now_ms;
      syslog(LOG_INFO, "BKVOICE wait backend=%s lane=%u phase=%s age_ms=%lu "
             "mono_ms=%lu interrupted=%u\n", backend, lane, names[phase],
             (unsigned long)elapsed, (unsigned long)now_ms,
             __atomic_load_n(&tls->interrupted, __ATOMIC_ACQUIRE));
#ifdef __NuttX__
      struct trace_task_s snapshot =
        { .pid = (pid_t)__atomic_load_n(&tls->trace_owner[lane], __ATOMIC_ACQUIRE),
          .state = -1 };
      uint64_t ticks = (uint64_t)clock();
      nxsched_foreach(trace_task, &snapshot);
      syslog(LOG_INFO, "BKVOICE wait-task pid=%ld state=%d cpu=%u timer_active=%u "
             "ticks=%llu expires=%llu priority=%d lock=%d affinity=%x\n",
             (long)snapshot.pid, snapshot.state,
             snapshot.cpu, snapshot.timer_active, (unsigned long long)ticks,
             (unsigned long long)snapshot.expires, snapshot.priority,
             snapshot.lockcount, snapshot.affinity);
      for (unsigned int i = 0; i < snapshot.peers; i++)
        syslog(LOG_INFO, "BKVOICE ready-task pid=%ld state=%d cpu=%u priority=%d "
               "lock=%d affinity=%x name=%s\n", (long)snapshot.ready[i].pid,
               snapshot.ready[i].state, snapshot.ready[i].cpu,
               snapshot.ready[i].priority, snapshot.ready[i].lockcount,
               snapshot.ready[i].affinity, snapshot.ready[i].name);
#endif
    }
}
#else
#define TRACE(tls, lane, phase) ((void)0)
#define TRACE_SAVE(tls, lane, phase) ((void)0)
#define TRACE_RESTORE(tls, lane) ((void)0)
#endif

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

static int bkvoice_tls_wait_inner(struct bkvoice_tls_s *tls, short events,
                            uint64_t deadline_ms, uint64_t *ready)
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

      struct
      {
        sem_t sem;
      } ordinary;
#ifdef CONFIG_BK7258_TTS_ISOLATION
      struct isolation_poll_s sample = { .tls = tls };
      sem_t *sem = tls->config.observe && events == POLLIN ?
                   &sample.sem : &ordinary.sem;
#else
      sem_t *sem = &ordinary.sem;
#endif
      ret = nxsem_init(sem, 0, 0);
      if (ret < 0) return ret;
      memset(&pfd, 0, sizeof(pfd));
      pfd.fd = -1;
      pfd.events = events;
      pfd.arg = sem;
      pfd.cb = poll_default_cb;
#ifdef CONFIG_BK7258_TTS_ISOLATION
      if (sem == &sample.sem) pfd.cb = isolation_poll_cb;
#endif
      TRACE(tls, events == POLLOUT ? 1 : 2, TRACE_POLL_ADD);
      ret = psock_poll(&tls->socket, &pfd, true);
      if (ret >= 0)
        {
          TRACE(tls, events == POLLOUT ? 1 : 2, TRACE_POLL_WAIT);
          if (pfd.revents == 0)
            ret = nxsem_tickwait_uninterruptible(
                    sem, MSEC2TICK((unsigned int)remaining));
          TRACE(tls, events == POLLOUT ? 1 : 2, TRACE_POLL_DEL);
          int teardown = psock_poll(&tls->socket, &pfd, false);
          if (teardown < 0) ret = teardown;
        }
      (void)nxsem_destroy(sem);
#ifdef CONFIG_BK7258_TTS_ISOLATION
      uint32_t notified = __atomic_load_n(&sample.ready, __ATOMIC_ACQUIRE);
      if (notified) *ready = notified;
#else
      (void)ready;
#endif
      if (ret < 0 && ret != -ETIMEDOUT && ret != -EINTR) return ret;
      if (pfd.revents != 0) return bkvoice_tls_deadline(tls, deadline_ms);
#else
      pfd.fd = tls->fd;
      pfd.events = events;
      pfd.revents = 0;
      TRACE(tls, events == POLLOUT ? 1 : 2, TRACE_POLL_WAIT);
      ret = poll(&pfd, 1, (int)remaining);
      if (ret < 0 && errno != EINTR)
        {
          return -errno;
        }

      if (ret > 0)
        {
          *ready = OBS_NOW(tls);
          /* Let the next nonblocking I/O observe buffered data, EOF or
           * SO_ERROR even when POLLHUP/POLLERR accompany readiness.
           */

          return bkvoice_tls_deadline(tls, deadline_ms);
        }
#endif
    }
}

static int bkvoice_tls_wait(struct bkvoice_tls_s *tls, short events,
                            uint64_t deadline_ms)
{
  TRACE_SAVE(tls, events == POLLOUT ? 1 : 2, TRACE_POLL_WAIT);
  uint64_t begin = OBS_NOW(tls), ready = 0;
  int ret = bkvoice_tls_wait_inner(tls, events, deadline_ms, &ready);
  if (events == POLLIN)
    {
      uint64_t end = OBS_NOW(tls);
      OBS(tls, 10, begin, ready ? ready : end, ret);
      if (ready) OBS(tls, 11, ready, end, ret);
      (void)end;
    }
  (void)begin;
  TRACE_RESTORE(tls, events == POLLOUT ? 1 : 2);
  return ret;
}

static int bkvoice_tls_socket_send(void *context,
                                   const unsigned char *buffer, size_t bytes)
{
  struct bkvoice_tls_s *tls = context;
  ssize_t ret;
  TRACE_SAVE(tls, 1, TRACE_SEND);

#ifdef __NuttX__
  ret = psock_send(&tls->socket, buffer, bytes, 0);
  TRACE_RESTORE(tls, 1);
  if (ret < 0)
    return ret == -EAGAIN || ret == -EWOULDBLOCK || ret == -EINTR ?
           MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
#else
#ifdef MSG_NOSIGNAL
  ret = send(tls->fd, buffer, bytes, MSG_NOSIGNAL);
#else
  ret = send(tls->fd, buffer, bytes, 0);
#endif
  TRACE_RESTORE(tls, 1);
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
  TRACE_SAVE(tls, 2, TRACE_RECV);
  uint64_t begin = OBS_NOW(tls);
#ifdef __NuttX__
  ssize_t ret = psock_recv(&tls->socket, buffer, bytes, 0);
  OBS(tls, 9, begin, OBS_NOW(tls), (int)ret);
  TRACE_RESTORE(tls, 2);
  if (ret < 0)
    return ret == -EAGAIN || ret == -EWOULDBLOCK || ret == -EINTR ?
           MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
#else
  ssize_t ret = recv(tls->fd, buffer, bytes, 0);
  int saved_errno = errno;
  OBS(tls, 9, begin, OBS_NOW(tls), ret < 0 ? -saved_errno : (int)ret);
  errno = saved_errno;
  TRACE_RESTORE(tls, 2);

  if (ret < 0)
    {
      return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ?
             MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    }

#endif
  (void)begin;
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
      TRACE(tls, 0, TRACE_CLOSE);
#ifdef __NuttX__
      (void)psock_close(&tls->socket);
#else
      (void)close(tls->fd);
      tls->fd = -1;
#endif
      tls->socket_open = false;
    }

  TRACE(tls, 0, TRACE_FREE);
  mbedtls_ssl_free(&tls->ssl);
  mbedtls_ssl_config_free(&tls->ssl_config);
  mbedtls_ssl_init(&tls->ssl);
  mbedtls_ssl_config_init(&tls->ssl_config);
  tls->opened = false;
  __atomic_store_n(&tls->faulted, false, __ATOMIC_RELEASE);
  TRACE(tls, 0, TRACE_IDLE);
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

  if (tls->config.session_load)
    {
      TRACE(tls, 0, TRACE_SESSION);
      ret = tls->config.session_load(tls->config.session_context, &tls->ssl,
                                      host, port);
      if (ret < 0) goto fail;
    }

  memset(&peer, 0, sizeof(peer));
  peer.sin_family = AF_INET;
  peer.sin_addr = tls->config.peer_address;
  peer.sin_port = htons(port);
#ifdef __NuttX__
  TRACE(tls, 0, TRACE_SOCKET);
  ret = psock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP, &tls->socket);
  if (ret < 0) goto fail;
  tls->socket_open = true;
  int nonblock = 1;
  TRACE(tls, 0, TRACE_NONBLOCK);
  ret = psock_ioctl(&tls->socket, FIONBIO,
                    (unsigned long)(uintptr_t)&nonblock);
  if (ret < 0) goto fail;
  TRACE(tls, 0, TRACE_CONNECT);
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
      TRACE(tls, 0, TRACE_SO_ERROR);
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

  TRACE(tls, 0, TRACE_SSL);
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

  if (tls->config.session_save)
    {
      TRACE(tls, 0, TRACE_SESSION);
      ret = tls->config.session_save(tls->config.session_context, &tls->ssl,
                                      host, port);
      if (ret < 0) goto fail;
    }
  tls->opened = true;
  TRACE(tls, 0, TRACE_IDLE);
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
          TRACE(tls, write ? 1 : 2, TRACE_LOCK);
          uint64_t lock_begin = OBS_NOW(tls);
          ret = bkvoice_tls_lock(tls, deadline_ms);
          if (!write) OBS(tls, 12, lock_begin, OBS_NOW(tls), ret);
          (void)lock_begin;
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

      TRACE(tls, write ? 1 : 2, TRACE_SSL);
      uint64_t ssl_begin = OBS_NOW(tls);
      ret = write ? mbedtls_ssl_write(&tls->ssl, buffer, bytes) :
                    mbedtls_ssl_read(&tls->ssl, buffer, bytes);
      if (!write) OBS(tls, 13, ssl_begin, OBS_NOW(tls), ret);
      (void)ssl_begin;
      if (ret > 0)
        {
          (void)pthread_mutex_unlock(&tls->crypto_lock);
          TRACE(tls, write ? 1 : 2, TRACE_IDLE);
          return ret;
        }

      if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
          ret = 0;
          goto fail;
        }

      if (ret != want)
        {
          /* Preserve the library failure category without logging peer,
           * credentials or payload. The public error contract stays EIO.
           */

          syslog(LOG_WARNING, "BKVOICE TLS io=%s library_result=%d\n",
                 write ? "write" : "read", ret);

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

  TRACE(tls, write ? 1 : 2, TRACE_IDLE);
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

  if ((config->session_load == NULL) != (config->session_save == NULL) ||
      (config->session_load && !config->server_auth_only))
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

int bkvoice_tls_prepare_request(struct bkvoice_tls_s *tls)
{
  if (tls == NULL || !tls->initialized)
    {
      return -EINVAL;
    }

  if (tls->opened || tls->socket_open)
    {
      return -EBUSY;
    }

  /* The request owner excludes cancellation until preparation returns.
   * A new connection within that request must never clear its interrupt.
   */

  __atomic_store_n(&tls->interrupted, false, __ATOMIC_RELEASE);
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
