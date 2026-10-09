/* SPDX-License-Identifier: Apache-2.0 */
/* Production query/publish functions; external mutex/IRQ shims only. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bk7258_display_service.h"
#define nxmutex_lock pthread_mutex_lock
#define nxmutex_unlock pthread_mutex_unlock
struct bkdisplay_service_s {
  pthread_mutex_t lock;
  struct bkdisplay_service_status_s status;
};
static struct bkdisplay_service_s g_bkdisplay_service = {
  .lock = PTHREAD_MUTEX_INITIALIZER
};
#include "bk7258_display_snapshot.inc"
static int result_pipe[2];
static struct bkdisplay_service_status_s observed;
static void *query(void *unused)
{
  (void)unused;
  int ret = bk7258_display_get_status(&observed);
  assert(write(result_pipe[1], &ret, sizeof(ret)) == sizeof(ret));
  return NULL;
}
int main(void)
{
  pthread_t thread;
  struct bkdisplay_service_s *service = &g_bkdisplay_service;
  assert(bk7258_display_get_status(NULL) == -EINVAL);
  assert(bk7258_display_get_status(&observed) == 0);
  assert(observed.state == BKDISPLAY_SERVICE_STOPPED);
  pthread_mutex_lock(&service->lock);
  service->status.state = BKDISPLAY_SERVICE_READY;
  service->status.render_sequence = 7;
  strcpy(service->status.pack_id, "verified-pack");
  bkdisplay_unlock(service);
  assert(pipe(result_pipe) == 0);
  /* Renderer holds the lock until after the query observation, modeling
   * arbitrarily stalled external I/O without a probabilistic long sleep.
   */
  pthread_mutex_lock(&service->lock);
  service->status.render_sequence = 8;
  strcpy(service->status.pack_id, "unfinished-pack");
  assert(pthread_create(&thread, NULL, query, NULL) == 0);
  struct pollfd wait = { .fd = result_pipe[0], .events = POLLIN };
  int ready = poll(&wait, 1, 1000); /* Harness watchdog, not product latency SLA. */
  if (ready != 1) {
    fprintf(stderr, "query waited for renderer I/O lock\n");
    pthread_mutex_unlock(&service->lock);
    pthread_join(thread, NULL);
    return 1;
  }
  int ret;
  assert(read(result_pipe[0], &ret, sizeof(ret)) == sizeof(ret) && ret == 0);
  pthread_join(thread, NULL);
  assert(observed.render_sequence == 7 && !strcmp(observed.pack_id, "verified-pack"));
  bkdisplay_unlock(service);
  assert(bk7258_display_get_status(&observed) == 0);
  assert(observed.render_sequence == 8 && !strcmp(observed.pack_id, "unfinished-pack"));
  close(result_pipe[0]); close(result_pipe[1]);
  puts("CONTRACT_PASS");
  return 0;
}
