/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bk7258_local_content.h"
#include "bk7258_nfc_scene.h"
#include "voice/audio_playback.h"

static unsigned int writes;
static bool cancel_write;
static bool drain_fail;
static bool close_fail;
static int player;
static bool g_trigger_started = true;
static int g_product_error;
static unsigned int pauses;
static unsigned int resumes;
static unsigned int cleanups;
static int bk7258_agent_trigger_pause_local(void) { pauses++; return 0; }
static int bk7258_agent_trigger_rearm(void) { resumes++; return 0; }
#define LOG_INFO 6
#define syslog(...) ((void)0)
#include "bk7258_agent_local_content.inc"

audio_playback_t *audio_playback_open(const char *path, unsigned int rate,
                                     unsigned int channels, unsigned int bits)
{
  assert(path == NULL && rate == 16000 && channels == 1 && bits == 16);
  return (audio_playback_t *)&player;
}

int audio_playback_write(audio_playback_t *p, const void *data, size_t size)
{
  assert(p == (audio_playback_t *)&player && data && size <= 640);
  writes++;
  if (cancel_write)
    {
      uint32_t id;
      assert(bkcontent_submit(6, 0, &id) == 0);
      bkcontent_step(false);
    }

  return size;
}

void audio_playback_stop(audio_playback_t *p)
{
  assert(p == (audio_playback_t *)&player);
}

int audio_playback_drain(audio_playback_t *p, unsigned int timeout)
{
  assert(p == (audio_playback_t *)&player && timeout <= 2000);
  return drain_fail ? -EIO : 0;
}

int audio_playback_close(audio_playback_t *p)
{
  assert(p == (audio_playback_t *)&player);
  return close_fail ? -EIO : 0;
}

int audio_playback_cleanup(unsigned int timeout)
{
  assert(timeout == 100);
  cleanups++;
  return 0;
}

int main(int argc, char **argv)
{
  struct bkcontent_status_s status;
  uint32_t id;
  bool completed;
  struct bknfc_bindings_s bindings = {0};
  struct bknfc_scene_s scene;
  struct bknfc_card_s card = {.size = 4, .uid = {1, 2, 3, 4}};
  char root[] = "/tmp/shaniu-content-scene-XXXXXX";

  assert(argc == 2);
  assert(bkcontent_submit(5, 1, &id) == -ESHUTDOWN);
  bkcontent_step(true);
  assert(bkcontent_submit(5, 2, &id) == -EINVAL);
  if (!strcmp(argv[1], "card"))
    {
      assert(mkdtemp(root));
      assert(bknfc_bindings_open(&bindings, root) == 0);
      assert(bknfc_bindings_set_action(&bindings, 0, 1, 0,
                                      &card, 5, 1) == 0);
      assert(bknfc_scene_init(&scene, 1) == 0);
      assert(bknfc_scene_observe(&scene, &bindings, 1, 1,
                                BKNFC_SCENE_PRESENT, &card, true, &id) == 0);
      assert(id != 0);
      assert(bknfc_scene_observe(&scene, &bindings, 1, 2,
               BKNFC_SCENE_PRESENT, &card, true, &id) == -EALREADY);
    }
  else assert(bkcontent_submit(5, 1, &id) == 0 && id != 0);
  assert(bkcontent_submit(5, 1, &id) == -EBUSY);
  bkcontent_status(&status);
  assert(status.phase == BKCONTENT_PENDING);
  bkcontent_work();
  assert(writes == 0); /* Capture owner has not released the microphone. */
  if (!strcmp(argv[1], "cancel-pending"))
    {
      assert(bkcontent_submit(6, 0, &id) == 0);
      assert(bkcontent_ready(status.id) == -ESTALE);
    }
  else
    {
      assert(product_content_step(1000, true, &completed));
      assert(pauses == 1 && resumes == 0);
      cancel_write = !strcmp(argv[1], "cancel-active") ||
                     !strcmp(argv[1], "cancel-close-error");
      close_fail = !strcmp(argv[1], "cancel-close-error");
      drain_fail = !strcmp(argv[1], "drain-failure");
      bkcontent_work();
    }

  bkcontent_status(&status);
  if (close_fail)
    {
      bkcontent_step(true);
      assert(bkcontent_submit(5, 1, &id) == -EIO);
    }
  assert(!product_content_step(2000, true, &completed));
  if (pauses) assert(resumes == 1);
  if (close_fail) assert(cleanups == 1);
  assert(completed == (status.phase == BKCONTENT_DONE));
  if (!strcmp(argv[1], "success") || !strcmp(argv[1], "card"))
    assert(status.phase == BKCONTENT_DONE && status.error == 0 && writes == 200);
  else if (close_fail)
    assert(status.phase == BKCONTENT_FAILED && status.error == -EIO);
  else if (!strncmp(argv[1], "cancel-", 7))
    assert(status.phase == BKCONTENT_CANCELED && writes <= 1);
  else
    {
      assert(status.phase == BKCONTENT_FAILED && status.error < 0);
      if (!strcmp(argv[1], "missing") || !strcmp(argv[1], "corrupt"))
        assert(writes == 0);
    }
  assert(bkcontent_quiesce() == 0);
  bkcontent_step(true);
  assert(bkcontent_submit(5, 1, &id) == 0); /* A new explicit round is possible. */
  bkcontent_step(false);
  bkcontent_status(&status);
  assert(status.phase == BKCONTENT_CANCELED);
  if (bindings.ready) assert(bknfc_bindings_reset(root) == 0);
  puts("LOCAL_CONTENT_PASS");
  return 0;
}
