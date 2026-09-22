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
