/* SPDX-License-Identifier: Apache-2.0 */
/* L1: link the deployed Agent audio_playback.c. Only Media, socket and clock
 * boundaries are controlled; this is not FFmpeg/DAC or acoustic acceptance. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <media_player.h>
#include "voice/audio_playback.h"

static struct {
  media_event_callback callback;
  void *cookie;
  int live, eof, stops, closes, failed_close, fail_eof;
  unsigned completions;
  size_t bytes;
  unsigned char pcm[10000];
} peer;
static uint64_t now_ms;
static const int socket_id = 173;

void *media_player_open(const char *stream)
{
  assert(!strcmp(stream, MEDIA_STREAM_MUSIC) && !peer.live);
  memset(&peer, 0, sizeof(peer)); peer.live = 1; return &peer;
}
int media_player_set_event_callback(void *p, void *cookie, media_event_callback cb)
{ assert(p == &peer && peer.live); peer.callback = cb; peer.cookie = cookie; return 0; }
int media_player_prepare(void *p, const char *url, const char *options)
{ assert(p == &peer && !url && strstr(options, "sample_rate=16000")); return 0; }
int media_player_start(void *p)
{ assert(p == &peer && peer.live); return 0; }
int media_player_get_socket(void *p)
{ assert(p == &peer && peer.live); return peer.eof ? -EBADF : socket_id; }
void media_player_close_socket(void *p)
{
  assert(p == &peer && peer.live); peer.eof = 1;
  if (peer.fail_eof) peer.callback(peer.cookie, MEDIA_EVENT_COMPLETED, -EIO, NULL);
}
int media_player_stop(void *p)
{ assert(p == &peer && peer.live); peer.stops++; return 0; }
int media_player_reset(void *p)
{ assert(p == &peer && peer.live); return 0; }
int media_player_close(void *p, int pending)
{
  assert(p == &peer && !pending && peer.live);
  peer.closes++;
  if (peer.failed_close) return -EIO;
  peer.live = 0; peer.callback = NULL; peer.cookie = NULL; return 0;
}
int __wrap_fcntl(int fd, int cmd, ...)
{ assert(fd == socket_id && (cmd == F_GETFL || cmd == F_SETFL)); return 0; }
ssize_t __wrap_write(int fd, const void *p, size_t size)
{
  assert(fd == socket_id && peer.live && !peer.eof && !peer.stops);
  size_t n = size > 37 ? 37 : size;
  assert(peer.bytes + n <= sizeof(peer.pcm));
  memcpy(peer.pcm + peer.bytes, p, n); peer.bytes += n; return n;
}
int __wrap_clock_gettime(clockid_t clock, struct timespec *ts)
{ assert(clock == CLOCK_MONOTONIC); ts->tv_sec = now_ms / 1000; ts->tv_nsec = (now_ms % 1000) * 1000000; return 0; }
int __wrap_usleep(useconds_t us)
{ now_ms += (us + 999) / 1000; return 0; }
int __wrap_sem_clockwait(sem_t *sem, clockid_t clock, const struct timespec *ts)
{
  (void)sem; (void)ts;
  assert(clock == CLOCK_MONOTONIC && peer.eof && peer.live && peer.bytes);
  /* An EOF request alone has not completed playback. The controlled Media
   * boundary delivers its completion only when the owner waits for it. */
  peer.completions++;
  peer.callback(peer.cookie, MEDIA_EVENT_COMPLETED, 0, NULL); return 0;
}
static audio_playback_t *open_player(void)
{
  audio_playback_t *p = audio_playback_open(NULL, 16000, 1, 16);
  assert(p); return p;
}
int main(int argc, char **argv)
{
  assert(argc == 2);
  unsigned char input[8002];
  for (unsigned i = 0; i < sizeof(input); i++) input[i] = (i * 17u + 31u) & 255;
  audio_playback_t *p = open_player();
  assert(audio_playback_write(p, input, sizeof(input)) == sizeof(input));
  assert(peer.bytes == sizeof(input) && !memcmp(input, peer.pcm, sizeof(input)));
  assert(!peer.eof && !peer.completions);
  if (!strcmp(argv[1], "tail"))
    {
      assert(audio_playback_drain(p, 2000) == 0);
      assert(peer.completions == 1 && peer.live);
      assert(audio_playback_close(p) == 0 && !peer.live);
    }
  else if (!strcmp(argv[1], "cancel-next"))
    {
      audio_playback_stop(p);
      peer.callback(peer.cookie, MEDIA_EVENT_COMPLETED, 0, NULL);
      assert(audio_playback_write(p, input, 2) == -ECANCELED);
      assert(peer.bytes == sizeof(input));
      assert(audio_playback_drain(p, 2000) == -ECANCELED);
      assert(audio_playback_close(p) == 0);
      p = open_player();
      assert(audio_playback_write(p, input + 20, 642) == 642);
      assert(peer.bytes == 642 && !memcmp(peer.pcm, input + 20, 642));
      assert(audio_playback_drain(p, 2000) == 0 && peer.completions == 1);
      assert(audio_playback_close(p) == 0);
    }
  else if (!strcmp(argv[1], "close-failure"))
    {
      peer.failed_close = 1;
      assert(audio_playback_close(p) == -EIO && peer.live);
      assert(!audio_playback_open(NULL, 16000, 1, 16) && peer.live);
      peer.callback(peer.cookie, MEDIA_EVENT_COMPLETED, 0, NULL);
      peer.failed_close = 0;
      assert(audio_playback_cleanup(2000) == 0 && !peer.live);
      p = open_player();
      assert(audio_playback_close(p) == 0);
    }
  else
    {
      assert(!strcmp(argv[1], "eof-failure")); peer.fail_eof = 1;
      assert(audio_playback_drain(p, 2000) == -EIO);
      peer.callback(peer.cookie, MEDIA_EVENT_COMPLETED, 0, NULL);
      assert(audio_playback_drain(p, 2000) == -EIO);
      assert(audio_playback_close(p) == 0);
    }
  assert(!peer.live); puts("CONTRACT_PASS"); return 0;
}
