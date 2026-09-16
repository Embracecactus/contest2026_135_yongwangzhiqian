/* SPDX-License-Identifier: Apache-2.0 */
/* Real EOF worker/close with a controlled lower-half completion queue. */
#include <assert.h>
#include <errno.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CONFIG_BK7258_APP_AGENT 1
#define CONFIG_BK7258_AUD 1
#define CONFIG_BK7258_AUD_QUEUE_DEPTH 4
#define CONFIG_BK7258_AUD_DEVNAME "pcm0p"
#define AUDIO_TYPE_OUTPUT 4
#define AUDIO_TYPE_FEATURE 5
#define AUDIO_FU_VOLUME 1
#define AUDIO_VOLUME_MAX 1000

static int test_ioctl(int fd, unsigned long request, ...);
static ssize_t test_receive(mqd_t mq, char *buffer, size_t bytes,
                            unsigned int *priority);
#define ioctl test_ioctl
#define mq_receive test_receive
#include "bk7258_agent_media_player.c"
#undef ioctl
#undef mq_receive

static atomic_int queued;
static atomic_int completed;
static atomic_int callbacks;
static atomic_int closed;
static int freed_buffers;
static sem_t callback_entered;
static sem_t callback_release;

int nxmutex_init(mutex_t *m) { return -pthread_mutex_init(m, NULL); }
int nxmutex_destroy(mutex_t *m) { return -pthread_mutex_destroy(m); }
int nxmutex_lock(mutex_t *m) { return -pthread_mutex_lock(m); }
int nxmutex_unlock(mutex_t *m) { return -pthread_mutex_unlock(m); }

static int test_ioctl(int fd, unsigned long request, ...)
{
  (void)fd;
  if (request == AUDIOIOC_FREEBUFFER)
    {
      freed_buffers++;
      return sizeof(struct audio_buf_desc_s);
    }
  if (request == AUDIOIOC_ENQUEUEBUFFER)
    {
      atomic_store(&queued, 1);
    }
  return 0;
}

static ssize_t test_receive(mqd_t mq, char *buffer, size_t bytes,
                            unsigned int *priority)
{
  struct audio_msg_s msg = {.msg_id = AUDIO_MSG_COMPLETE};
  (void)mq;
  (void)priority;
  assert(bytes == sizeof(msg));
  if (atomic_exchange(&completed, 0))
    {
      memcpy(buffer, &msg, sizeof(msg));
      return sizeof(msg);
    }
  errno = EAGAIN;
  return -1;
}

static void on_complete(void *cookie, int event, int result, const char *extra)
{
  (void)cookie;
  (void)extra;
  assert(event == MEDIA_EVENT_COMPLETED && result == 0);
  atomic_fetch_add(&callbacks, 1);
  assert(sem_post(&callback_entered) == 0);
  assert(sem_wait(&callback_release) == 0);
}

static void wait_queued(void)
{
  for (int i = 0; i < 1000 && !atomic_load(&queued); i++)
    {
      usleep(1000);
    }
  assert(atomic_load(&queued));
}

static struct bk7258_agent_player_s *fixture(struct ap_buffer_s *apb)
{
  struct bk7258_agent_player_s *p = calloc(1, sizeof(*p));
  assert(p != NULL && nxmutex_init(&p->lock) == 0);
  p->fd = -1;
  p->mq = (mqd_t)-1;
  p->prepared = p->started = p->hardware_started = p->reserved = true;
  p->current = apb;
  p->current_bytes = apb->nmaxbytes;
  p->callback = on_complete;
  g_bk7258_agent_player = p;
  atomic_store(&queued, 0);
  return p;
}

static void *close_player(void *arg)
{
  assert(media_player_close(arg, 0) == 0);
  atomic_store(&closed, 1);
  return NULL;
}

int main(void)
{
  uint8_t pcm[2] = {1, 2};
  struct ap_buffer_s apb = {.samp = pcm, .nmaxbytes = sizeof(pcm)};
  struct bk7258_agent_player_s *p;
  pthread_t closer;
  bool joining = false;

  /* Match the real upper-half success result and repeat teardown: ownership
   * must be dropped after the first successful buffer release.
   */
  struct bk7258_agent_player_s cleanup = { .fd = -1, .mq = (mqd_t)-1 };
  cleanup.buffers[0] = &apb;
  cleanup.buffer_count = 1;
  assert(bk7258_agent_player_cleanup_locked(&cleanup, false) == 0);
  assert(cleanup.buffers[0] == NULL && cleanup.buffer_count == 0);
  assert(bk7258_agent_player_cleanup_locked(&cleanup, false) == 0);
  assert(freed_buffers == 1);

  assert(sem_init(&callback_entered, 0, 0) == 0);
  assert(sem_init(&callback_release, 0, 0) == 0);
  p = fixture(&apb);
  media_player_close_socket(p);
  wait_queued();
  assert(apb.flags & AUDIO_APB_FINAL);
  assert(media_player_close(p, 0) == 0);
  assert(atomic_load(&callbacks) == 0);

  p = fixture(&apb);
  media_player_close_socket(p);
  wait_queued();
  atomic_store(&completed, 1);
  assert(sem_wait(&callback_entered) == 0);
  assert(pthread_create(&closer, NULL, close_player, p) == 0);
  for (int i = 0; i < 1000 && !joining; i++)
    {
      nxmutex_lock(&p->lock);
      joining = p->eof_joining;
      nxmutex_unlock(&p->lock);
      if (!joining) usleep(1000);
    }
  assert(joining && !atomic_load(&closed));
  assert(sem_post(&callback_release) == 0);
  assert(pthread_join(closer, NULL) == 0);
  assert(atomic_load(&closed) && atomic_load(&callbacks) == 1);
  assert(sem_destroy(&callback_entered) == 0);
  assert(sem_destroy(&callback_release) == 0);
  puts("BKVOICE_PLAYER_EOF_HOST_PASS");
  return 0;
}
