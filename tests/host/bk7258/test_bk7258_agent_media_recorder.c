/* SPDX-License-Identifier: Apache-2.0 */
#ifdef TEST_AGENT_CAPTURE
#define _GNU_SOURCE
#define CAP_START_TIMEOUT_MS 100u
/* The actual Agent capture owner, with a socket-backed Media peer. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CONFIG_AI_AGENT_AUDIO_CAPTURE_GAIN 1
int media_recorder_get_socket(void *handle);
int media_recorder_reset(void *handle);
#include "voice/audio_capture.c"

static int peer_sockets[2];
static int peer_handle;
static unsigned int opens;
static unsigned int closes;
static unsigned int starts;
static unsigned int route_on;
static unsigned int route_off;
static media_event_callback start_callback;
static void *start_cookie;
static pthread_t start_thread;
static bool start_thread_valid;
static atomic_int format_queued;
static int start_mode;
static bool require_warm_route;
static bool warm_prepared;

int media_recorder_set_event_callback(void *handle, void *cookie,
                                      media_event_callback callback)
{
  assert(handle == &peer_handle);
  start_cookie = cookie;
  start_callback = callback;
  return 0;
}

static void *peer_started(void *unused)
{
  (void)unused;
  usleep(10000);
  atomic_store(&format_queued, 1);
  start_callback(start_cookie, MEDIA_EVENT_STARTED,
                 start_mode == 1 ? -EINVAL : 0, NULL);
  return NULL;
}

void *media_recorder_open(const char *params)
{
  assert(strcmp(params, MEDIA_SOURCE_MIC) == 0);
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, peer_sockets) == 0);
  opens++;
  atomic_store(&format_queued, 0);
  return &peer_handle;
}

int media_recorder_prepare(void *handle, const char *url, const char *options)
{
  assert(handle == &peer_handle && url == NULL);
  assert(strcmp(options, "format=s16le:sample_rate=16000:ch_layout=mono") == 0);
  return 0;
}

int media_recorder_start(void *handle)
{
  assert(handle == &peer_handle);
  assert(!require_warm_route || warm_prepared);
  starts++;
  if (start_mode != 2)
    {
      assert(pthread_create(&start_thread, NULL, peer_started, NULL) == 0);
      start_thread_valid = true;
    }
  return 0;
}

int media_recorder_get_socket(void *handle)
{
  assert(handle == &peer_handle);
  return peer_sockets[0];
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
  assert(handle == &peer_handle);
  ssize_t n = recv(peer_sockets[0], data, len, 0);
  return n < 0 ? -errno : n;
}

int media_recorder_stop(void *handle)
{
  assert(handle == &peer_handle);
  return 0;
}

int media_recorder_reset(void *handle)
{
  assert(handle == &peer_handle);
  return 0;
}

int media_recorder_close(void *handle)
{
  assert(handle == &peer_handle);
  if (start_thread_valid)
    {
      assert(pthread_join(start_thread, NULL) == 0);
      start_thread_valid = false;
    }
  assert(close(peer_sockets[0]) == 0);
  assert(close(peer_sockets[1]) == 0);
  closes++;
  return 0;
}

static int capture_route(int active)
{
  if (active)
    {
      assert(atomic_load(&format_queued) && start_mode == 0);
      route_on++;
    }
  else { route_off++; warm_prepared = false; }
  return 0;
}

static int prepare_warm_route(unsigned int rate, unsigned int channels,
                              unsigned int bits)
{
  assert(rate == 16000 && channels == 1 && bits == 16);
  assert(!atomic_load(&format_queued));
  warm_prepared = true;
  route_on++;
  return 1;
}

static void peer_samples(unsigned int first, unsigned int count)
{
  int16_t pcm[320];
  while (count)
    {
      unsigned int n = count > 320 ? 320 : count;
      for (unsigned int i = 0; i < n; i++)
        pcm[i] = (first + i) % 30000;
      size_t done = 0;
      while (done < n * sizeof(int16_t))
        {
          ssize_t sent = send(peer_sockets[1], (char *)pcm + done,
                               n * sizeof(int16_t) - done, 0);
          assert(sent > 0);
          done += sent;
        }
      first += n;
      count -= n;
    }
}

static void check_samples(const int16_t *pcm, unsigned int first,
                          unsigned int count)
{
  for (unsigned int i = 0; i < count; i++)
    assert(pcm[i] == (int16_t)((first + i) % 30000));
}

static void local_samples(audio_capture_t *cap, unsigned int first,
                          unsigned int count)
{
  int16_t pcm[320];
  unsigned int done = 0;
  while (done < count)
    {
      uint64_t sample = UINT64_MAX;
      int n = audio_capture_read_local(cap, pcm, sizeof(pcm), &sample);
      if (n == -EAGAIN) continue;
      assert(n > 0 && n % 2 == 0);
      assert(sample == first + done);
      check_samples(pcm, first + done, n / 2);
      done += n / 2;
    }
  assert(done == count);
}

static void test_single_open_handoff_and_cancel(void)
{
  audio_capture_t *local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  assert(audio_capture_open(NULL, 16000, 1, 16) == NULL && errno == EBUSY);
  assert(audio_capture_cleanup(100) == -EBUSY);
  assert(closes == 0);
  peer_samples(0, 9600);
  local_samples(local, 0, 9600);
  /* Consumed history older than 400 ms must already be erased. */
  pthread_mutex_lock(&local->lock);
  for (unsigned int i = 0; i < 6400; i++) assert(local->ring[i] == 0);
  pthread_mutex_unlock(&local->lock);
  assert(audio_capture_handoff(local) == 0);
  audio_capture_stats_t stats;
  assert(audio_capture_get_stats(local, &stats) == 0);
  assert(stats.first_sample == 0 && stats.end_sample == 9600);
  assert(stats.samples == 9600 && stats.stream_error == 0);
  peer_samples(9600, 1600);
  audio_capture_t *turn = audio_capture_open(NULL, 16000, 1, 16);
  assert(turn == local && audio_capture_start(turn) == 0);
  assert(opens == 1 && starts == 1 && closes == 0);
  unsigned int done = 0;
  int16_t pcm[320];
  while (done < 8000)
    {
      int n = audio_capture_read(turn, pcm, sizeof(pcm));
      if (n == -EAGAIN) continue;
      assert(n > 0);
      check_samples(pcm, 3200 + done, n / 2);
      done += n / 2;
    }
  assert(done == 8000);
  assert(audio_capture_abort(turn) == 0);
  assert(audio_capture_read(turn, pcm, sizeof(pcm)) == -ECANCELED);
  assert(audio_capture_close(turn) == 0);
  assert(closes == 1 && route_on == 1 && route_off == 1);
}

static void test_overload_and_next_turn(void)
{
  audio_capture_t *local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  peer_samples(0, 6400);
  local_samples(local, 0, 6400);
  assert(audio_capture_handoff(local) == 0);
  peer_samples(6400, 30000);
  int error = 0;
  for (unsigned int i = 0; i < 2000 && !error; i++)
    {
      pthread_mutex_lock(&local->lock);
      error = local->stream_error;
      pthread_mutex_unlock(&local->lock);
      if (!error) usleep(1000);
    }
  assert(error == -EOVERFLOW);
  audio_capture_stats_t stats;
  assert(audio_capture_get_stats(local, &stats) == 0);
  assert(stats.stream_error == -EOVERFLOW && stats.samples >= 6400);
  assert(audio_capture_open(NULL, 16000, 1, 16) == NULL);
  assert(errno == EOVERFLOW);
  assert(audio_capture_cleanup(100) == 0);
  assert(opens == 2 && closes == 2);

  local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  /* A byte-stream transport may split even a single PCM sample. */
  const uint8_t split[] = { 0, 0, 1, 0 };
  assert(send(peer_sockets[1], split, 3, 0) == 3);
  local_samples(local, 0, 1);
  assert(send(peer_sockets[1], split + 3, 1, 0) == 1);
  peer_samples(2, 318);
  local_samples(local, 1, 319);
  for (unsigned int first = 320; first < 38720; first += 320)
    {
      peer_samples(first, 320);
      local_samples(local, first, 320);
    }
  assert(audio_capture_handoff(local) == 0);
  audio_capture_t *turn = audio_capture_open(NULL, 16000, 1, 16);
  assert(turn == local);
  int16_t pcm[320];
  for (unsigned int first = 32320; first < 38720;)
    {
      int n = audio_capture_read(turn, pcm, sizeof(pcm));
      assert(n > 0);
      check_samples(pcm, first, n / 2);
      first += n / 2;
    }
  assert(audio_capture_close(local) == 0);
  assert(opens == 3 && closes == 3 && route_on == 3 && route_off == 3);
}

static void test_bounded_pcm_stats_and_reset(void)
{
  audio_capture_t *local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  int16_t sent[320], received[320];
  for (unsigned int i = 0; i < 320; i++)
    sent[i] = i < 80 ? 0 : i < 160 ? 1000 :
              i < 240 ? -1000 : 32767;
  assert(send(peer_sockets[1], sent, sizeof(sent), 0) == sizeof(sent));
  for (;;)
    {
      int n = audio_capture_read_local(local, received, sizeof(received), NULL);
      if (n == -EAGAIN) continue;
      assert(n == sizeof(received));
      assert(memcmp(sent, received, sizeof(sent)) == 0);
      break;
    }
  audio_capture_stats_t stats;
  assert(audio_capture_get_stats(local, &stats) == 0);
  assert(stats.first_sample == 0 && stats.end_sample == 320);
  assert(stats.samples == 320 && stats.peak == 32767);
  assert(stats.dc == 8191 && stats.clipped_permyriad == 2500);
  assert(stats.rms > 16000 && stats.rms < 17000);
  assert(stats.stream_error == 0);
  usleep(50000);
  memset(sent, 0, sizeof(sent));
  assert(send(peer_sockets[1], sent, sizeof(sent), 0) == sizeof(sent));
  for (;;)
    {
      int n = audio_capture_read_local(local, received, sizeof(received), NULL);
      if (n == -EAGAIN) continue;
      assert(n == sizeof(received));
      assert(memcmp(sent, received, sizeof(sent)) == 0);
      break;
    }
  assert(audio_capture_get_stats(local, &stats) == 0);
  assert(stats.end_sample == 640 && stats.samples == 640);
  assert(stats.max_receive_interval_ms >= 40);
  assert(stats.clipped_permyriad == 1250);
  pthread_mutex_lock(&local->lock);
  for (unsigned int i = 0; i < CAP_STATS_SECONDS; i++)
    if (local->stats[i].samples)
      local->stats[i].second -= CAP_STATS_SECONDS;
  pthread_mutex_unlock(&local->lock);
  assert(audio_capture_get_stats(local, &stats) == 0);
  assert(stats.samples == 0); /* Expired PCM aggregates are excluded. */
  assert(audio_capture_handoff(local) == 0);
  assert(audio_capture_close(local) == 0);

  local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  assert(audio_capture_get_stats(local, &stats) == 0);
  assert(stats.samples == 0 && stats.stream_error == 0);
  assert(audio_capture_close(local) == 0);
}

static void test_ack_discard_same_recorder(void)
{
  audio_capture_t *local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  peer_samples(0, 320);
  local_samples(local, 0, 320);
  assert(audio_capture_handoff_at(local, 320) == 0);
  audio_capture_t *turn = audio_capture_open(NULL, 16000, 1, 16);
  assert(turn == local && audio_capture_start(turn) == 0);
  uint64_t marker = UINT64_MAX, first = UINT64_MAX;
  assert(audio_capture_get_handoff_sample(turn, &marker) == 0);
  assert(marker == 320 && opens == 6 && closes == 5);
  int16_t pcm[320];
  int n = audio_capture_read_at(turn, pcm, sizeof(pcm), &first);
  assert(n == sizeof(pcm) && first == 0);
  check_samples(pcm, 0, 320);

  /* The playback peer's own acoustic PCM is deliberately never delivered. */
  assert(audio_capture_set_discard(turn, 1) == 0);
  peer_samples(320, 3200);
  for (int i = 0; i < 1000; i++)
    {
      pthread_mutex_lock(&turn->lock);
      uint64_t written = turn->written;
      pthread_mutex_unlock(&turn->lock);
      if (written >= (320 + 3200) * sizeof(int16_t)) break;
      usleep(1000);
    }
  pthread_mutex_lock(&turn->lock);
  assert(turn->written == turn->consumed);
  assert(turn->written >= (320 + 3200) * sizeof(int16_t));
  pthread_mutex_unlock(&turn->lock);
  assert(audio_capture_read_at(turn, pcm, sizeof(pcm), &first) == -EAGAIN);
  const uint8_t split_self_sample = 0x55;
  assert(send(peer_sockets[1], &split_self_sample, 1, 0) == 1);
  int pending = -1;
  for (int i = 0; i < 100; i++)
    {
      assert(ioctl(peer_sockets[0], FIONREAD, &pending) == 0);
      if (pending == 0) break;
      usleep(1000);
    }
  assert(pending == 0); /* Producer owns the first byte of a split sample. */
  assert(audio_capture_set_discard(turn, 0) == 0);
  assert(send(peer_sockets[1], &split_self_sample, 1, 0) == 1);
  for (int i = 0; i < 100; i++)
    {
      pthread_mutex_lock(&turn->lock);
      uint64_t written = turn->written;
      pthread_mutex_unlock(&turn->lock);
      if (written >= 3521 * sizeof(int16_t)) break;
      usleep(1000);
    }
  peer_samples(3521, 320);
  for (;;)
    {
      n = audio_capture_read_at(turn, pcm, sizeof(pcm), &first);
      if (n == -EAGAIN) continue;
      assert(n == sizeof(pcm) && first == 3521);
      check_samples(pcm, 3521, 320);
      break;
    }
  assert(audio_capture_abort(turn) == 0);
  assert(audio_capture_close(turn) == 0);
  local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  assert(audio_capture_close(local) == 0);
}

static void test_ack_cancel_while_discarding(void)
{
  audio_capture_t *local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  peer_samples(0, 320);
  local_samples(local, 0, 320);
  assert(audio_capture_handoff_at(local, 320) == 0);
  audio_capture_t *turn = audio_capture_open(NULL, 16000, 1, 16);
  assert(turn == local && audio_capture_set_discard(turn, 1) == 0);
  peer_samples(320, 320);
  assert(audio_capture_abort(turn) == 0);
  assert(audio_capture_set_discard(turn, 0) == -ECANCELED);
  assert(audio_capture_close(turn) == 0);
  local = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(local && audio_capture_start(local) == 0);
  assert(audio_capture_close(local) == 0);
}

static void test_start_failure_keeps_hardware_off(void)
{
  unsigned int before = route_on;
  for (int mode = 1; mode <= 2; mode++)
    {
      start_mode = mode;
      audio_capture_t *cap = audio_capture_open_local(NULL, 16000, 1, 16);
      assert(cap);
      assert(audio_capture_start(cap) == (mode == 1 ? -EINVAL : -ETIMEDOUT));
      assert(route_on == before);
      /* A delayed event cannot activate hardware or make retry safe. */
      start_callback(start_cookie, MEDIA_EVENT_STARTED, 0, NULL);
      assert(audio_capture_start(cap) == -EALREADY);
      assert(route_on == before);
      assert(audio_capture_close(cap) == 0);
    }
  start_mode = 0;
  audio_capture_t *cap = audio_capture_open_local(NULL, 16000, 1, 16);
  assert(cap && audio_capture_start(cap) == 0);
  assert(route_on == before + 1);
  assert(audio_capture_close(cap) == 0);
}

int main(void)
{
  assert(audio_capture_set_route(capture_route) == 0);
  test_single_open_handoff_and_cancel();
  test_overload_and_next_turn();
  test_bounded_pcm_stats_and_reset();
  test_ack_discard_same_recorder();
  test_ack_cancel_while_discarding();
  assert(opens == 9 && closes == 9 && route_on == 9 && route_off == 9);
  test_start_failure_keeps_hardware_off();
  assert(opens == 12 && closes == 12 && route_on == 10 && route_off == 10);
  assert(audio_capture_set_route_prepare(prepare_warm_route) == 0);
  require_warm_route = true;
  for (int mode = 0; mode <= 2; mode++)
    {
      start_mode = mode;
      audio_capture_t *cap = audio_capture_open_local(NULL, 16000, 1, 16);
      assert(cap);
      assert(audio_capture_set_route_prepare(NULL) == -EBUSY);
      assert(audio_capture_start(cap) ==
             (mode == 0 ? 0 : mode == 1 ? -EINVAL : -ETIMEDOUT));
      assert(warm_prepared);
      assert(audio_capture_close(cap) == 0);
      assert(!warm_prepared);
    }
  assert(opens == 15 && closes == 15 && route_on == 13 && route_off == 13);
  assert(audio_capture_set_route_prepare(NULL) == 0);
  puts("BK7258_AGENT_CAPTURE_HOST_PASS opens=15 closes=15 cold=event-gated warm=before-link error-and-timeout=route-released handoff=400ms cancel-recover");
  return 0;
}
#else
/****************************************************************************
 * tests/host/bk7258/test_bk7258_agent_media_recorder.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Regression coverage for the standalone Dolphin recorder STOP boundary. The real
 * bridge is included so the test can inspect its private ownership state and
 * prove that the reader wake precedes the synchronous lower-half STOP while
 * the recorder mutex is released.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* 适配器只在无 Media 的独立录音目标编入；不能冒充 Agent 录音验收。 */
#define CONFIG_DOLPHIN_RECORDER 1
#define CONFIG_BK7258_MIC 1
#define CONFIG_BK7258_MIC_DEVNAME "pcm0c"

static int test_open(const char *path, int oflag, ...);
static int test_close(int fd);
static int test_ioctl(int fd, unsigned long request, ...);
static mqd_t test_mq_open(const char *name, int oflag, ...);
static int test_mq_close(mqd_t mq);
static int test_mq_unlink(const char *name);
static int test_mq_send(mqd_t mq, const char *msg, size_t len,
                        unsigned int priority);
static ssize_t test_mq_timedreceive(mqd_t mq, char *msg, size_t len,
                                    unsigned int *priority,
                                    const struct timespec *abstime);
static int test_clock_gettime(clockid_t clockid, struct timespec *value);
static void test_syslog(int priority, const char *format, ...);

#define open       test_open
#define close      test_close
#define ioctl      test_ioctl
#define mq_open    test_mq_open
#define mq_close   test_mq_close
#define mq_unlink  test_mq_unlink
#define mq_send    test_mq_send
#define mq_timedreceive test_mq_timedreceive
#define clock_gettime   test_clock_gettime
#define syslog     test_syslog

#include "../../../app/bk7258/bk7258_agent_media_recorder.c"

#undef open
#undef close
#undef ioctl
#undef mq_open
#undef mq_close
#undef mq_unlink
#undef mq_send
#undef mq_timedreceive
#undef clock_gettime
#undef syslog

struct test_state_s
{
  struct bk7258_agent_audio_s *rec;
  unsigned int sequence;
  unsigned int wake_sequence;
  unsigned int stop_sequence;
  unsigned int wake_calls;
  unsigned int stop_calls;
  unsigned int receive_calls;
  unsigned int receive_error_count;
  unsigned int clock_calls;
  int wake_errno;
  int stop_errno;
  int receive_errors[4];
  int clock_errno;
  uint16_t receive_msg_id;
  struct timespec now;
  struct timespec receive_deadlines[4];
};

static struct test_state_s g_test;

static void test_assert_recorder_unlocked(void)
{
  int ret = pthread_mutex_trylock(&g_test.rec->lock);

  assert(ret == 0);
  assert(pthread_mutex_unlock(&g_test.rec->lock) == 0);
}

static int test_open(const char *path, int oflag, ...)
{
  (void)path;
  (void)oflag;
  errno = ENOSYS;
  return -1;
}

static int test_close(int fd)
{
  (void)fd;
  return 0;
}

static int test_ioctl(int fd, unsigned long request, ...)
{
  (void)fd;

  if (request == AUDIOIOC_STOP)
    {
      test_assert_recorder_unlocked();
      g_test.stop_calls++;
      g_test.stop_sequence = ++g_test.sequence;
      assert(g_test.wake_calls >= 1);
      assert(g_test.wake_sequence < g_test.stop_sequence);
      if (g_test.stop_errno != 0)
        {
          errno = g_test.stop_errno;
          return -1;
        }
    }

  return 0;
}

static mqd_t test_mq_open(const char *name, int oflag, ...)
{
  (void)name;
  (void)oflag;
  errno = ENOSYS;
  return (mqd_t)-1;
}

static int test_mq_close(mqd_t mq)
{
  (void)mq;
  return 0;
}

static int test_mq_unlink(const char *name)
{
  (void)name;
  return 0;
}

static int test_mq_send(mqd_t mq, const char *msg, size_t len,
                        unsigned int priority)
{
  const struct audio_msg_s *audio = (const struct audio_msg_s *)msg;

  (void)mq;
  (void)priority;
  test_assert_recorder_unlocked();
  assert(len == sizeof(*audio));
  assert(audio->msg_id == AUDIO_MSG_STOP);
  g_test.wake_calls++;
  g_test.wake_sequence = ++g_test.sequence;
  if (g_test.wake_errno != 0)
    {
      errno = g_test.wake_errno;
      return -1;
    }

  return 0;
}

static ssize_t test_mq_timedreceive(mqd_t mq, char *msg, size_t len,
                                    unsigned int *priority,
                                    const struct timespec *abstime)
{
  struct audio_msg_s *audio = (struct audio_msg_s *)msg;
  unsigned int call = g_test.receive_calls++;

  (void)mq;
  (void)priority;
  assert(len == sizeof(*audio));
  assert(abstime != NULL);
  assert(call < sizeof(g_test.receive_deadlines) /
                sizeof(g_test.receive_deadlines[0]));
  g_test.receive_deadlines[call] = *abstime;

  if (call < g_test.receive_error_count)
    {
      errno = g_test.receive_errors[call];
      return -1;
    }

  memset(audio, 0, sizeof(*audio));
  audio->msg_id = g_test.receive_msg_id;
  return sizeof(*audio);
}

static int test_clock_gettime(clockid_t clockid, struct timespec *value)
{
  assert(clockid == CLOCK_REALTIME);
  assert(value != NULL);
  g_test.clock_calls++;
  if (g_test.clock_errno != 0)
    {
      errno = g_test.clock_errno;
      return -1;
    }

  *value = g_test.now;
  return 0;
}

static void test_syslog(int priority, const char *format, ...)
{
  (void)priority;
  (void)format;
}

int nxmutex_init(mutex_t *mutex)
{
  int ret = pthread_mutex_init(mutex, NULL);
  return ret == 0 ? 0 : -ret;
}

int nxmutex_destroy(mutex_t *mutex)
{
  int ret = pthread_mutex_destroy(mutex);
  return ret == 0 ? 0 : -ret;
}

int nxmutex_lock(mutex_t *mutex)
{
  int ret = pthread_mutex_lock(mutex);
  return ret == 0 ? 0 : -ret;
}

int nxmutex_timedlock(mutex_t *mutex, unsigned int timeout_ms)
{
  (void)timeout_ms;
  return nxmutex_lock(mutex);
}

int nxmutex_unlock(mutex_t *mutex)
{
  int ret = pthread_mutex_unlock(mutex);
  return ret == 0 ? 0 : -ret;
}

static void test_recorder_init(struct bk7258_agent_audio_s *rec)
{
  memset(&g_test, 0, sizeof(g_test));
  memset(rec, 0, sizeof(*rec));
  assert(nxmutex_init(&rec->lock) == 0);
  rec->lock_initialized = true;
  rec->fd = 7;
  rec->mq = (mqd_t)9;
  rec->mq_registered = true;
  rec->prepared = true;
  rec->started = true;
  rec->buffers_queued = true;
  rec->frame_bytes = sizeof(int16_t);
  g_test.now.tv_sec = 100;
  g_test.now.tv_nsec = 900000000l;
  g_test.rec = rec;
}

static void test_recorder_destroy(struct bk7258_agent_audio_s *rec)
{
  assert(!rec->stop_in_progress);
  assert(nxmutex_destroy(&rec->lock) == 0);
}

static void test_wake_precedes_unlocked_stop(void)
{
  struct bk7258_agent_audio_s rec;

  test_recorder_init(&rec);
  assert(media_recorder_stop(&rec) == 0);
  assert(g_test.wake_calls == 1);
  assert(g_test.stop_calls == 1);
  assert(rec.wake_sent);
  assert(rec.stopping);
  assert(!rec.started);
  assert(!rec.buffers_queued);
  assert(!rec.stop_in_progress);
  test_recorder_destroy(&rec);
}

static void test_lower_stop_failure_is_retryable(void)
{
  struct bk7258_agent_audio_s rec;

  test_recorder_init(&rec);
  g_test.stop_errno = ETIMEDOUT;
  assert(media_recorder_stop(&rec) == -ETIMEDOUT);
  assert(g_test.wake_calls == 1);
  assert(g_test.stop_calls == 1);
  assert(rec.wake_sent);
  assert(rec.buffers_queued);
  assert(!rec.stop_in_progress);

  g_test.stop_errno = 0;
  assert(media_recorder_stop(&rec) == 0);
  assert(g_test.wake_calls == 1);
  assert(g_test.stop_calls == 2);
  assert(!rec.buffers_queued);
  assert(!rec.stop_in_progress);
  test_recorder_destroy(&rec);
}

static void test_wake_failure_is_retryable(void)
{
  struct bk7258_agent_audio_s rec;

  test_recorder_init(&rec);
  g_test.wake_errno = EAGAIN;
  assert(media_recorder_stop(&rec) == -EAGAIN);
  assert(g_test.wake_calls == 1);
  assert(g_test.stop_calls == 1);
  assert(!rec.wake_sent);
  assert(!rec.buffers_queued);
  assert(!rec.stop_in_progress);

  g_test.wake_errno = 0;
  g_test.stop_sequence = 0;
  assert(media_recorder_stop(&rec) == 0);
  assert(g_test.wake_calls == 2);
  assert(g_test.stop_calls == 1);
  assert(rec.wake_sent);
  assert(!rec.stop_in_progress);
  test_recorder_destroy(&rec);
}

static void test_concurrent_stop_is_rejected(void)
{
  struct bk7258_agent_audio_s rec;

  test_recorder_init(&rec);
  rec.stop_in_progress = true;
  assert(media_recorder_stop(&rec) == -EBUSY);
  assert(g_test.wake_calls == 0);
  assert(g_test.stop_calls == 0);
  rec.stop_in_progress = false;
  test_recorder_destroy(&rec);
}

static void test_receive_timeout_is_bounded(void)
{
  struct bk7258_agent_audio_s rec;
  uint8_t pcm[640];

  test_recorder_init(&rec);
  g_test.receive_error_count = 1;
  g_test.receive_errors[0] = ETIMEDOUT;
  assert(media_recorder_read_data(&rec, pcm, sizeof(pcm)) == -ETIMEDOUT);
  assert(g_test.clock_calls == 1);
  assert(g_test.receive_calls == 1);
  assert(g_test.receive_deadlines[0].tv_sec == 101);
  assert(g_test.receive_deadlines[0].tv_nsec == 900000000l);
  assert(!rec.stopping);
  test_recorder_destroy(&rec);
}

static void test_receive_eintr_keeps_original_deadline(void)
{
  struct bk7258_agent_audio_s rec;
  uint8_t pcm[640];

  test_recorder_init(&rec);
  g_test.receive_error_count = 2;
  g_test.receive_errors[0] = EINTR;
  g_test.receive_errors[1] = ETIMEDOUT;
  assert(media_recorder_read_data(&rec, pcm, sizeof(pcm)) == -ETIMEDOUT);
  assert(g_test.clock_calls == 1);
  assert(g_test.receive_calls == 2);
  assert(g_test.receive_deadlines[0].tv_sec ==
         g_test.receive_deadlines[1].tv_sec);
  assert(g_test.receive_deadlines[0].tv_nsec ==
         g_test.receive_deadlines[1].tv_nsec);
  test_recorder_destroy(&rec);
}

static void test_receive_stop_message_is_epipe(void)
{
  struct bk7258_agent_audio_s rec;
  uint8_t pcm[640];

  test_recorder_init(&rec);
  g_test.receive_msg_id = AUDIO_MSG_STOP;
  assert(media_recorder_read_data(&rec, pcm, sizeof(pcm)) == -EPIPE);
  assert(g_test.receive_calls == 1);
  assert(rec.stopping);
  test_recorder_destroy(&rec);
}

int main(void)
{
  test_wake_precedes_unlocked_stop();
  test_lower_stop_failure_is_retryable();
  test_wake_failure_is_retryable();
  test_concurrent_stop_is_rejected();
  test_receive_timeout_is_bounded();
  test_receive_eintr_keeps_original_deadline();
  test_receive_stop_message_is_epipe();
  puts("BK7258_MEDIA_RECORDER_HOST_PASS");
  return 0;
}
#endif
