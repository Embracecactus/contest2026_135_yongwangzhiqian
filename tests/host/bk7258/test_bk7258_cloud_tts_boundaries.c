/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_tts.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_MAX (256u * 1024u)
#define PCM_MAX (90u * 24000u * 2u)

static size_t pcm_total;

static int sink(void *context, const void *data, size_t size)
{
  (void)context;
  assert(data != NULL);
  pcm_total += size;
  return 0;
}

static char *event_with_audio(size_t encoded, bool stop)
{
  const char *prefix = "{\"choices\":[{\"index\":0,\"delta\":{\"audio\":{\"data\":\"";
  const char *suffix = stop ? "\"}},\"finish_reason\":\"stop\"}]}" :
                              "\"}}}]}";
  char *event = malloc(strlen(prefix) + encoded + strlen(suffix) + 1);
  assert(event != NULL && encoded % 4 == 0);
  memcpy(event, prefix, strlen(prefix));
  memset(event + strlen(prefix), 'A', encoded);
  memcpy(event + strlen(prefix) + encoded, suffix, strlen(suffix) + 1);
  return event;
}

static int feed(struct bkcloud_tts_s *tts, const char *event, size_t size,
                size_t chunk)
{
  size_t wire_size = size + 8;
  char *wire = malloc(wire_size);
  int ret = 0;
  assert(wire != NULL);
  memcpy(wire, "data: ", 6);
  memcpy(wire + 6, event, size);
  memcpy(wire + 6 + size, "\n\n", 2);
  for (size_t offset = 0; offset < wire_size && !ret; offset += chunk)
    {
      size_t count = wire_size - offset;
      if (count > chunk) count = chunk;
      ret = bkcloud_tts_feed(tts, wire + offset, count);
    }
  free(wire);
  return ret;
}

static void done(struct bkcloud_tts_s *tts)
{
  static const char marker[] = "data: [DONE]\n\n";
  assert(bkcloud_tts_feed(tts, marker, sizeof(marker) - 1) == 0);
  assert(bkcloud_tts_finish(tts) == 0);
}

static void test_large_audio_events(void)
{
  struct bkcloud_tts_s tts;
  char *event;
  pcm_total = 0;
  bkcloud_tts_init(&tts, sink, NULL);
  event = event_with_audio(64000, true);
  assert(feed(&tts, event, strlen(event), 1) == 0);
  free(event);
  done(&tts);
  assert(pcm_total == 48000);

  pcm_total = 0;
  bkcloud_tts_init(&tts, sink, NULL);
  event = event_with_audio(70000, true);
  assert(feed(&tts, event, strlen(event), 1024) == 0);
  free(event);
  done(&tts);
  assert(pcm_total == 52500);

  bkcloud_tts_init(&tts, sink, NULL);
  event = event_with_audio(262000, true);
  assert(strlen(event) <= EVENT_MAX);
  assert(feed(&tts, event, strlen(event), 4093) == 0);
  free(event);
  done(&tts);

  bkcloud_tts_init(&tts, sink, NULL);
  event = event_with_audio(262100, true);
  assert(strlen(event) > EVENT_MAX);
  assert(feed(&tts, event, strlen(event), 1024) == -E2BIG);
  free(event);
}

static void test_json_and_pcm_boundaries(void)
{
  struct bkcloud_tts_s tts;
  char *event;
  char nested[40];
  memset(nested, '[', 17);
  memset(nested + 17, ']', 17);
  nested[34] = 0;
  bkcloud_tts_init(&tts, sink, NULL);
  assert(feed(&tts, nested, strlen(nested), 3) == -EBADMSG);

  event = event_with_audio(4, true);
  event[10] = 0;
  bkcloud_tts_init(&tts, sink, NULL);
  assert(feed(&tts, event, strlen(event) + 1, 1) == -EBADMSG);
  free(event);

  event = event_with_audio(4, true); /* AAA= decodes to two bytes. */
  event[strlen(event) - strlen("\"}},\"finish_reason\":\"stop\"}]}") - 1] = '=';
  bkcloud_tts_init(&tts, sink, NULL);
  tts.total = PCM_MAX - 2;
  assert(feed(&tts, event, strlen(event), 7) == 0);
  done(&tts);
  free(event);

  event = event_with_audio(4, true); /* AAAA decodes to three bytes. */
  bkcloud_tts_init(&tts, sink, NULL);
  tts.total = PCM_MAX - 2;
  assert(feed(&tts, event, strlen(event), 7) == -EBADMSG);
  free(event);
}

int main(void)
{
  test_large_audio_events();
  test_json_and_pcm_boundaries();
  return 0;
}
