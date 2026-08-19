/****************************************************************************
 * app/vela_claw/src/platform/claw_rtos.c
 *
 * Pure-POSIX RTOS backend (pthreads + mutex + semaphore + queue). Compiles
 * unchanged on NuttX and on the host.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include "claw_common.h"
#include "claw_rtos.h"

claw_err_t claw_thread_create(claw_thread_t *tid, void *(*entry)(void *),
                              void *arg, int priority, int stack_bytes)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (stack_bytes > 0)
    {
      pthread_attr_setstacksize(&attr, (size_t)stack_bytes);
    }

  /* Real-time scheduling with a priority is advisory; ignore failures so the
   * code stays portable between glibc and NuttX. */
  if (priority > 0)
    {
      struct sched_param sp;
      sp.sched_priority = priority;
      pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
      pthread_attr_setschedpolicy(&attr, SCHED_RR);
      pthread_attr_setschedparam(&attr, &sp);
    }

  if (pthread_create(tid, &attr, entry, arg) != 0)
    {
      pthread_attr_destroy(&attr);
      return CLAW_EAGAIN;
    }

  pthread_attr_destroy(&attr);
  return CLAW_OK;
}

void claw_thread_join(claw_thread_t tid)
{
  pthread_join(tid, NULL);
}

void claw_thread_detach(claw_thread_t tid)
{
  pthread_detach(tid);
}

claw_err_t claw_mutex_init(claw_mutex_t *m)
{
  return pthread_mutex_init(m, NULL) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_mutex_lock(claw_mutex_t *m)
{
  return pthread_mutex_lock(m) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_mutex_unlock(claw_mutex_t *m)
{
  return pthread_mutex_unlock(m) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_mutex_destroy(claw_mutex_t *m)
{
  return pthread_mutex_destroy(m) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_sem_init(claw_sem_t *s, unsigned value)
{
  return sem_init(s, 0, value) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_sem_wait(claw_sem_t *s)
{
  return sem_wait(s) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_sem_trywait(claw_sem_t *s)
{
  return sem_trywait(s) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_sem_post(claw_sem_t *s)
{
  return sem_post(s) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

claw_err_t claw_sem_destroy(claw_sem_t *s)
{
  return sem_destroy(s) == 0 ? CLAW_OK : CLAW_EAGAIN;
}

/* ---- thread-safe queue (mutex + sem + intrusive list) ---- */

struct claw_queue_node {
  struct claw_queue_node *next;
  void *data;
};

struct claw_queue_s {
  claw_mutex_t   lock;
  claw_sem_t     sem;
  struct claw_queue_node *head;
  struct claw_queue_node *tail;
  size_t         msg_size;
  size_t         count;
  size_t         max;
};

claw_queue_t *claw_queue_create(size_t max_msgs, size_t msg_size)
{
  claw_queue_t *q = calloc(1, sizeof(*q));
  if (!q)
    {
      return NULL;
    }

  q->msg_size = msg_size;
  q->max = max_msgs;
  claw_mutex_init(&q->lock);
  claw_sem_init(&q->sem, 0);
  return q;
}

void claw_queue_destroy(claw_queue_t *q)
{
  if (!q)
    {
      return;
    }

  struct claw_queue_node *n = q->head;
  while (n)
    {
      struct claw_queue_node *next = n->next;
      free(n->data);
      free(n);
      n = next;
    }

  claw_sem_destroy(&q->sem);
  claw_mutex_destroy(&q->lock);
  free(q);
}

claw_err_t claw_queue_push(claw_queue_t *q, const void *msg, int timeout_ms)
{
  if (!q)
    {
      return CLAW_EINVAL;
    }

  struct claw_queue_node *node = calloc(1, sizeof(*node));
  void *data = malloc(q->msg_size);
  if (!node || !data)
    {
      free(node);
      free(data);
      return CLAW_ENOMEM;
    }

  memcpy(data, msg, q->msg_size);
  node->data = data;

  claw_mutex_lock(&q->lock);
  if (q->max && q->count >= q->max)
    {
      /* drop oldest */
      struct claw_queue_node *old = q->head;
      q->head = old->next;
      if (!q->head)
        {
          q->tail = NULL;
        }

      q->count--;
      free(old->data);
      free(old);
    }

  node->next = NULL;
  if (q->tail)
    {
      q->tail->next = node;
    }
  else
    {
      q->head = node;
    }

  q->tail = node;
  q->count++;
  claw_mutex_unlock(&q->lock);

  (void)timeout_ms;
  claw_sem_post(&q->sem);
  return CLAW_OK;
}

claw_err_t claw_queue_pop(claw_queue_t *q, void *msg, int timeout_ms)
{
  if (!q)
    {
      return CLAW_EINVAL;
    }

  if (timeout_ms == 0)
    {
      if (claw_sem_trywait(&q->sem) != CLAW_OK)
        {
          return CLAW_EAGAIN;
        }
    }
  else
    {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += timeout_ms / 1000;
      ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
      if (ts.tv_nsec >= 1000000000L)
        {
          ts.tv_sec++;
          ts.tv_nsec -= 1000000000L;
        }

#if defined(__NuttX__)
      if (sem_wait(&q->sem) != 0)
#else
      if (sem_timedwait(&q->sem, &ts) != 0)
#endif
        {
          return CLAW_ETIMEDOUT;
        }
    }

  claw_mutex_lock(&q->lock);
  struct claw_queue_node *node = q->head;
  if (node)
    {
      q->head = node->next;
      if (!q->head)
        {
          q->tail = NULL;
        }

      q->count--;
      memcpy(msg, node->data, q->msg_size);
      free(node->data);
      free(node);
    }

  claw_mutex_unlock(&q->lock);
  return node ? CLAW_OK : CLAW_EAGAIN;
}

void claw_sleep_ms(unsigned ms)
{
  struct timespec req;
  req.tv_sec = ms / 1000;
  req.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&req, NULL);
}

uint64_t claw_now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
