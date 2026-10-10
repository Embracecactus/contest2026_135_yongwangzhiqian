#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check capture ownership using production lifecycle functions and safe peers.

Static peers cover error propagation; real pthread cases check heap release
and run the actual capture producer against a local socket Media peer. These
are host ownership contracts, not hardware or historical HardFault proof.
"""

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
AGENT = Path(os.environ.get("AI_AGENT_ROOT", ROOT.parent / "packages/ai_agent"))


def voice_source():
    """Read a pinned historical caller without changing the active checkout."""
    revision = os.environ.get("AI_AGENT_VOICE_CHANNEL_REV")
    if revision:
        return subprocess.run(
            ["git", "-C", str(AGENT), "show", revision + ":src/voice/voice_channel.c"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    return (AGENT / "src/voice/voice_channel.c").read_text()


def function(source, name):
    match = re.search(
        r"(?:static )?(?:int|void)\s*\*?\s*" + re.escape(name) + r"\([^;{}]*\)\s*\{",
        source,
    )
    if match is None:
        raise ValueError("production function missing: " + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start() : end]


class CaptureLifecycleTest(unittest.TestCase):
    def test_detached_close_asr_and_cleanup(self):
        source = voice_source()
        code = r"""
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#define VOICE_IDLE 0
#define VOICE_STARTING 1
#define VOICE_RECORDING 2
#define VOICE_STOPPING 3
#define VOICE_PROCESSING 4
#define VOICE_CHANNEL_EVENT_WAKE_ACK_CANCEL 1
#define VOICE_CHANNEL_EVENT_CAPTURE_QUIESCENT 2
#define VOICE_CHANNEL_EVENT_OUTPUT_FINISHED 3
#define VOICE_CHANNEL_EVENT_TURN_COMPLETE 4
#define AUTO_TURN_TIMEOUT_MS 180000
#define TAG "test"
typedef struct { int unused; } audio_capture_t;
typedef struct { int unused; } voice_asr_stream_t;
static audio_capture_t capture;
static voice_asr_stream_t asr;
static struct {
 pthread_mutex_t lock;
 int state, turn_active, preconnect_active, canceled, wake_ack_pending;
 int wake_ack_result, auto_endpoint, tts_abort, tts_active;
 int capture_error, capture_cleanup_pending, capture_cleanup_result;
 int tts_cleanup_pending, tts_cleanup_result, cleanup_in_progress;
 int reply_stream_active;
 audio_capture_t *cap;
 voice_asr_stream_t *asr_stream;
 void *tts_pb;
 unsigned char *pcm_buf;
 size_t pcm_len;
 pthread_t rec_thread;
 uint64_t request_id, turn_started_ms;
} s_voice = {.lock = PTHREAD_MUTEX_INITIALIZER};
static struct timespec s_asr_done_ts;
static int joined, closes, aborts, stream_aborts, finishes, cleanups;
static int close_result, cleanup_result, cancel_in_asr, completions;
int voice_channel_cancel(void);
static uint64_t voice_now_ms(void) { return 100; }
static void notify_channel_event(int event, int result) {
 (void)result;
 if(event==VOICE_CHANNEL_EVENT_TURN_COMPLETE) completions++;
}
static void reply_wake(void) {}
static int voice_asr_cancel(void) { return 0; }
static int voice_tts_cancel(void) { return 0; }
static int llm_cancel_request(void) { return 0; }
static int audio_playback_stop(void *pb) { (void)pb; return 0; }
static int audio_playback_cleanup(unsigned ms) { (void)ms; return 0; }
static int audio_capture_abort(audio_capture_t *cap) {
 assert(cap==&capture); aborts++; return 0;
}
static int join_recorder(pthread_t thread, void **result) {
 (void)thread; (void)result; joined=1; return 0;
}
#define pthread_join join_recorder
static int audio_capture_close(audio_capture_t *cap) {
 assert(cap==&capture && joined && s_voice.cap==NULL);
 assert(s_voice.state==VOICE_STOPPING);
 assert(pthread_mutex_trylock(&s_voice.lock)==0);
 pthread_mutex_unlock(&s_voice.lock);
 closes++; return close_result;
}
static int audio_capture_cleanup(unsigned ms) {
 assert(ms==100 && s_voice.cap==NULL && s_voice.cleanup_in_progress);
 cleanups++; return cleanup_result;
}
static void voice_asr_stream_abort(voice_asr_stream_t *stream) {
 assert(stream==&asr); stream_aborts++;
}
static int voice_asr_stream_finish(voice_asr_stream_t *stream,
                                  char *text, size_t size) {
 assert(stream==&asr && size>3 && s_voice.cap==NULL && closes==1);
 finishes++;
 if(cancel_in_asr) {
  int previous=aborts;
  assert(voice_channel_cancel()==0 && aborts==previous);
 }
 strcpy(text,"ok"); return 0;
}
static int voice_asr_recognize_checked(const void *pcm, size_t len,
 char *text, size_t size, int (*check)(void *), void *request) {
 assert(pcm && len==4 && size>3 && s_voice.cap==NULL && closes==1);
 assert(check && check(request)==0); finishes++;
 strcpy(text,"ok"); return 0;
}
"""
        for name in (
            "voice_request_status",
            "voice_request_check",
            "voice_request_complete",
            "voice_channel_cancel",
            "voice_channel_recover",
            "voice_channel_stop_with_text",
        ):
            code += "\n" + function(source, name)
        code += r"""
static void setup(int stream) {
 s_voice.state=VOICE_RECORDING; s_voice.turn_active=1;
 s_voice.cap=&capture; s_voice.request_id++;
 s_voice.asr_stream=stream ? &asr : NULL;
 s_voice.pcm_buf=stream ? NULL : calloc(1,4);
 s_voice.pcm_len=stream ? 0 : 4;
 s_voice.canceled=0; s_voice.capture_error=0;
 s_voice.capture_cleanup_pending=0; s_voice.capture_cleanup_result=0;
 joined=closes=aborts=stream_aborts=finishes=cleanups=0;
 close_result=cleanup_result=cancel_in_asr=completions=0;
}
int main(void) {
 char text[32];
 setup(1);
 assert(voice_channel_stop_with_text(text,sizeof(text))==0);
 assert(!strcmp(text,"ok") && finishes==1 && closes==1 && aborts==1);
 assert(s_voice.cap==NULL && s_voice.state==VOICE_PROCESSING);
 voice_request_complete(s_voice.request_id,0);
 assert(s_voice.state==VOICE_IDLE && completions==1);
 setup(0);
 assert(voice_channel_stop_with_text(text,sizeof(text))==0);
 assert(finishes==1 && s_voice.cap==NULL && !strcmp(text,"ok"));
 setup(1); cancel_in_asr=1;
 assert(voice_channel_stop_with_text(text,sizeof(text))==-ECANCELED);
 assert(!text[0] && aborts==1 && finishes==1);
 voice_request_complete(s_voice.request_id,-ECANCELED);
 assert(s_voice.state==VOICE_IDLE && completions==1);
 setup(1); s_voice.canceled=1;
 assert(voice_channel_stop_with_text(text,sizeof(text))==-ECANCELED);
 assert(!text[0] && !finishes && stream_aborts==1 && closes==1);
 setup(1); close_result=-ETIMEDOUT;
 assert(voice_channel_stop_with_text(text,sizeof(text))==-ETIMEDOUT);
 assert(!text[0] && !finishes && stream_aborts==1 && s_voice.cap==NULL);
 assert(s_voice.capture_cleanup_pending && s_voice.state==VOICE_STOPPING);
 voice_request_complete(s_voice.request_id,-ETIMEDOUT);
 assert(s_voice.turn_active && completions==0);
 assert(voice_channel_cancel()==0 && aborts==1);
 cleanup_result=-EBUSY;
 assert(voice_channel_recover()==-EBUSY && s_voice.capture_cleanup_pending);
 cleanup_result=0;
 assert(voice_channel_recover()==0 && !s_voice.capture_cleanup_pending);
 assert(!s_voice.turn_active && s_voice.state==VOICE_IDLE && completions==1);
 assert(cleanups==2 && s_voice.cap==NULL);
 puts("AGENT_CAPTURE_LIFECYCLE_PASS");
 return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="agent-capture-lifecycle-") as path:
            directory = Path(path)
            source_path = directory / "test.c"
            binary = directory / "test"
            source_path.write_text(
                code.replace(
                    "#include <syslog.h>",
                    '#include <syslog.h>\n#include "'
                    + str(AGENT / "src/core/agent_trace.h")
                    + '"',
                )
            )
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    str(source_path),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_start_failure_detaches_before_close(self):
        source = voice_source()
        start = function(source, "voice_channel_start_internal")
        # Both create and start failures publish the same detached ownership.
        branches = start.split("audio_capture_t* cap = s_voice.cap;")[1:]
        self.assertEqual(len(branches), 2)
        for branch in branches:
            before_close = branch.split("audio_capture_close(cap)", 1)[0]
            before_unlock = before_close.split("pthread_mutex_unlock", 1)[0]
            self.assertIn("s_voice.cap = NULL;", before_unlock)

    def test_real_voice_stop_cancel_serializes_capture_abort(self):
        """Run the production voice callers against the real capture producer.

        A first abort is observed while its socket-backed producer is blocked
        in poll.  Cancel begins only after that call entered.  The fixture does
        not require overlap: the desired caller fix serializes it under the
        voice lock, while the old caller lets both reach pthread_join.
        """
        source = voice_source()
        code = r"""
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#define CONFIG_AI_AGENT_AUDIO_CAPTURE_GAIN 1
#define CAP_START_TIMEOUT_MS 100u
int media_recorder_get_socket(void *handle);
int media_recorder_reset(void *handle);
#include "voice/audio_capture.c"
#define VOICE_IDLE 0
#define VOICE_STARTING 1
#define VOICE_RECORDING 2
#define VOICE_STOPPING 3
#define VOICE_PROCESSING 4
#define VOICE_CHANNEL_EVENT_WAKE_ACK_CANCEL 1
#define VOICE_CHANNEL_EVENT_CAPTURE_QUIESCENT 2
#define VOICE_CHANNEL_EVENT_OUTPUT_FINISHED 3
#define VOICE_CHANNEL_EVENT_TURN_COMPLETE 4
#define AUTO_TURN_TIMEOUT_MS 180000
#define TAG "test"
typedef struct { int unused; } voice_asr_stream_t;
static int sockets[2], recorder, abort_inflight, abort_max, abort_entered, producer_poll_entered;
static pthread_mutex_t observer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t observer_ready = PTHREAD_COND_INITIALIZER;
static struct {
 pthread_mutex_t lock;
 int state, turn_active, preconnect_active, canceled, wake_ack_pending;
 int wake_ack_result, auto_endpoint, tts_abort, tts_active;
 int capture_error, capture_cleanup_pending, capture_cleanup_result;
 int tts_cleanup_pending, tts_cleanup_result, cleanup_in_progress;
 int reply_stream_active;
 audio_capture_t *cap;
 voice_asr_stream_t *asr_stream;
 void *tts_pb;
 unsigned char *pcm_buf;
 size_t pcm_len;
 pthread_t rec_thread;
 uint64_t request_id, turn_started_ms;
} s_voice = {.lock = PTHREAD_MUTEX_INITIALIZER};
static struct timespec s_asr_done_ts;
static uint64_t voice_now_ms(void) { return 100; }
static void notify_channel_event(int event, int result) { (void)event; (void)result; }
static void reply_wake(void) {}
static int voice_asr_cancel(void) { return 0; }
static int voice_tts_cancel(void) { return 0; }
static int llm_cancel_request(void) { return 0; }
static int audio_playback_stop(void *pb) { (void)pb; return 0; }
static int audio_playback_cleanup(unsigned ms) { (void)ms; return 0; }
static int voice_test_abort(audio_capture_t *cap) {
 pthread_mutex_lock(&observer_lock);
 abort_inflight++;
 if (abort_inflight > abort_max) abort_max = abort_inflight;
 abort_entered = 1;
 pthread_cond_broadcast(&observer_ready);
 pthread_mutex_unlock(&observer_lock);
 int ret = audio_capture_abort(cap);
 pthread_mutex_lock(&observer_lock);
 abort_inflight--;
 pthread_mutex_unlock(&observer_lock);
 return ret;
}
static void voice_asr_stream_abort(voice_asr_stream_t *s) { (void)s; }
static int voice_asr_stream_finish(voice_asr_stream_t *s, char *t, size_t n)
{ (void)s; (void)t; (void)n; return -ECANCELED; }
static int voice_asr_recognize_checked(const void *p, size_t l, char *t,
 size_t n, int (*c)(void *), void *r)
{ (void)p; (void)l; (void)t; (void)n; (void)c; (void)r; return -ECANCELED; }
int media_recorder_set_event_callback(void *h, void *cookie, media_event_callback cb)
{ (void)h; static void *saved_cookie; static media_event_callback saved_cb; saved_cookie=cookie; saved_cb=cb; saved_cb(saved_cookie, MEDIA_EVENT_STARTED, 0, NULL); return 0; }
void *media_recorder_open(const char *p)
{ (void)p; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0); return &recorder; }
int media_recorder_prepare(void *h, const char *u, const char *o)
{ (void)h; (void)u; (void)o; return 0; }
int media_recorder_start(void *h) { assert(h == &recorder); return 0; }
int media_recorder_get_socket(void *h) {
 assert(h == &recorder);
 pthread_mutex_lock(&observer_lock);
 producer_poll_entered = 1;
 pthread_cond_broadcast(&observer_ready);
 pthread_mutex_unlock(&observer_lock);
 return sockets[0];
}
ssize_t media_recorder_read_data(void *h, void *p, size_t n)
{ assert(h == &recorder); ssize_t r=recv(sockets[0],p,n,0); return r<0 ? -errno : r; }
int media_recorder_stop(void *h) { assert(h == &recorder); return 0; }
int media_recorder_reset(void *h) { assert(h == &recorder); return 0; }
int media_recorder_close(void *h)
{ assert(h == &recorder); assert(close(sockets[0]) == 0); assert(close(sockets[1]) == 0); return 0; }
static void *recording_worker(void *u) { (void)u; return NULL; }
"""
        for name in (
            "voice_request_status", "voice_request_check", "voice_request_complete",
            "voice_channel_cancel", "voice_channel_recover", "voice_channel_stop_with_text",
        ):
            body = function(source, name)
            code += "\n" + body.replace("audio_capture_abort(", "voice_test_abort(")
        code += r"""
static void *stop_worker(void *u) { (void)u; char text[8]; return (void *)(intptr_t)voice_channel_stop_with_text(text, sizeof(text)); }
static void *cancel_worker(void *u) { (void)u; return (void *)(intptr_t)voice_channel_cancel(); }
int main(void) {
 pthread_t stop, cancel; void *stop_result, *cancel_result;
 s_voice.state=VOICE_RECORDING; s_voice.turn_active=1; s_voice.request_id=9;
 s_voice.cap=audio_capture_open_local(NULL,16000,1,16);
 assert(s_voice.cap && audio_capture_start(s_voice.cap)==0);
 pthread_mutex_lock(&observer_lock);
 while (!producer_poll_entered) pthread_cond_wait(&observer_ready,&observer_lock);
 pthread_mutex_unlock(&observer_lock);
 assert(pthread_create(&s_voice.rec_thread,NULL,recording_worker,NULL)==0);
 assert(pthread_create(&stop,NULL,stop_worker,NULL)==0);
 pthread_mutex_lock(&observer_lock);
 while (!abort_entered) pthread_cond_wait(&observer_ready,&observer_lock);
 pthread_mutex_unlock(&observer_lock);
 assert(pthread_create(&cancel,NULL,cancel_worker,NULL)==0);
 assert(pthread_join(stop,&stop_result)==0);
 assert(pthread_join(cancel,&cancel_result)==0);
 assert((intptr_t)stop_result == -ECANCELED && (intptr_t)cancel_result == 0);
 assert(abort_max == 1 && s_voice.cap == NULL);
 puts("VOICE_CAPTURE_SERIAL_ABORT_PASS");
 return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="agent-voice-capture-race-") as path:
            directory = Path(path)
            source_path = directory / "test.c"
            binary = directory / "test"
            source_path.write_text(
                code.replace(
                    "#include <syslog.h>",
                    '#include <syslog.h>\n#include "'
                    + str(AGENT / "src/core/agent_trace.h")
                    + '"',
                )
            )
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                 str(source_path), "-I", str(AGENT / "src"), "-I",
                 str(ROOT / "tests/host/bk7258/mocks"), "-I", str(AGENT / "include"),
                 "-o", str(binary)], check=True
            )
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_stop_cancel_overlap_joins_real_worker_before_heap_release(self):
        """Drive the production stop/cancel window with real pthread joins.

        The old fixture replaced pthread_join with a boolean and could not
        observe a cancel arriving while stop owns the pre-close capture.  This
        peer releases the stop abort after observing it, then permits cancel
        to be serialized by the production owner lock. It still requires the
        worker join and detached owner before heap capture release.
        """
        source = voice_source()
        code = r"""
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#define VOICE_IDLE 0
#define VOICE_STARTING 1
#define VOICE_RECORDING 2
#define VOICE_STOPPING 3
#define VOICE_PROCESSING 4
#define VOICE_CHANNEL_EVENT_WAKE_ACK_CANCEL 1
#define VOICE_CHANNEL_EVENT_CAPTURE_QUIESCENT 2
#define VOICE_CHANNEL_EVENT_OUTPUT_FINISHED 3
#define VOICE_CHANNEL_EVENT_TURN_COMPLETE 4
#define AUTO_TURN_TIMEOUT_MS 180000
#define TAG "test"
typedef struct { unsigned magic; } audio_capture_t;
typedef struct { int unused; } voice_asr_stream_t;
static pthread_mutex_t peer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t peer_ready = PTHREAD_COND_INITIALIZER;
static int stop_abort_entered, cancel_finished, worker_joined;
static int aborts, closes, releases, completions;
static struct {
 pthread_mutex_t lock;
 int state, turn_active, preconnect_active, canceled, wake_ack_pending;
 int wake_ack_result, auto_endpoint, tts_abort, tts_active;
 int capture_error, capture_cleanup_pending, capture_cleanup_result;
 int tts_cleanup_pending, tts_cleanup_result, cleanup_in_progress;
 int reply_stream_active;
 audio_capture_t *cap;
 voice_asr_stream_t *asr_stream;
 void *tts_pb;
 unsigned char *pcm_buf;
 size_t pcm_len;
 pthread_t rec_thread;
 uint64_t request_id, turn_started_ms;
} s_voice = {.lock = PTHREAD_MUTEX_INITIALIZER};
static struct timespec s_asr_done_ts;
int voice_channel_cancel(void);
static uint64_t voice_now_ms(void) { return 100; }
static void notify_channel_event(int event, int result) {
 (void)result; if (event == VOICE_CHANNEL_EVENT_TURN_COMPLETE) completions++;
}
static void reply_wake(void) {}
static int voice_asr_cancel(void) { return 0; }
static int voice_tts_cancel(void) { return 0; }
static int llm_cancel_request(void) { return 0; }
static int audio_playback_stop(void *pb) { (void)pb; return 0; }
static int audio_playback_cleanup(unsigned ms) { (void)ms; return 0; }
static int audio_capture_abort(audio_capture_t *cap) {
 assert(cap && cap->magic == 0xcafef00dU);
 pthread_mutex_lock(&peer_lock);
 aborts++;
 if (aborts == 1) {
  stop_abort_entered = 1;
  pthread_cond_broadcast(&peer_ready);
 }
 pthread_mutex_unlock(&peer_lock);
 return 0;
}
static int audio_capture_close(audio_capture_t *cap) {
 assert(cap && cap->magic == 0xcafef00dU);
 assert(aborts >= 1 && worker_joined && s_voice.cap == NULL);
 cap->magic = 0;
 releases++;
 free(cap);
 closes++;
 return 0;
}
static int audio_capture_cleanup(unsigned ms) { (void)ms; return 0; }
static void voice_asr_stream_abort(voice_asr_stream_t *s) { (void)s; }
static int voice_asr_stream_finish(voice_asr_stream_t *s, char *t, size_t n)
{ (void)s; (void)t; (void)n; return -ECANCELED; }
static int voice_asr_recognize_checked(const void *p, size_t l, char *t,
 size_t n, int (*c)(void *), void *r)
{ (void)p; (void)l; (void)t; (void)n; (void)c; (void)r; return -ECANCELED; }
"""
        for name in (
            "voice_request_status",
            "voice_request_check",
            "voice_request_complete",
            "voice_channel_cancel",
            "voice_channel_recover",
            "voice_channel_stop_with_text",
        ):
            code += "\n" + function(source, name)
        code += r"""
static void *recording_worker(void *unused) {
 (void)unused;
 pthread_mutex_lock(&peer_lock);
 while (!cancel_finished) pthread_cond_wait(&peer_ready, &peer_lock);
 pthread_mutex_unlock(&peer_lock);
 worker_joined = 1;
 return NULL;
}
static void *stop_worker(void *unused) {
 (void)unused;
 char text[8];
 return (void *)(intptr_t)voice_channel_stop_with_text(text, sizeof(text));
}
static void *cancel_worker(void *unused) {
 (void)unused;
 int ret = voice_channel_cancel();
 pthread_mutex_lock(&peer_lock);
 cancel_finished = 1;
 pthread_cond_broadcast(&peer_ready);
 pthread_mutex_unlock(&peer_lock);
 return (void *)(intptr_t)ret;
}
int main(void) {
 pthread_t stop, cancel;
 void *stop_result, *cancel_result;
 audio_capture_t *cap = calloc(1, sizeof(*cap));
 assert(cap); cap->magic = 0xcafef00dU;
 s_voice.state = VOICE_RECORDING;
 s_voice.turn_active = 1;
 s_voice.request_id = 7;
 s_voice.cap = cap;
 assert(pthread_create(&s_voice.rec_thread, NULL, recording_worker, NULL) == 0);
 assert(pthread_create(&stop, NULL, stop_worker, NULL) == 0);
 pthread_mutex_lock(&peer_lock);
 while (!stop_abort_entered) pthread_cond_wait(&peer_ready, &peer_lock);
 pthread_mutex_unlock(&peer_lock);
 assert(pthread_create(&cancel, NULL, cancel_worker, NULL) == 0);
 assert(pthread_join(cancel, &cancel_result) == 0);
 assert(pthread_join(stop, &stop_result) == 0);
 assert((intptr_t)cancel_result == 0 && (intptr_t)stop_result == -ECANCELED);
 assert(aborts >= 1 && closes == 1 && releases == 1 && worker_joined);
 assert(s_voice.cap == NULL && s_voice.capture_cleanup_pending == 0);
 voice_request_complete(7, -ECANCELED);
 assert(s_voice.state == VOICE_IDLE && completions == 1);
 puts("AGENT_CAPTURE_REAL_PTHREAD_PASS");
 return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="agent-capture-real-pthread-") as path:
            directory = Path(path)
            source_path = directory / "test.c"
            binary = directory / "test"
            source_path.write_text(
                code.replace(
                    "#include <syslog.h>",
                    '#include <syslog.h>\n#include "'
                    + str(AGENT / "src/core/agent_trace.h")
                    + '"',
                )
            )
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
                 str(source_path), "-o", str(binary)], check=True
            )
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_stop_epipe_after_owner_abort_is_not_capture_failure(self):
        """Use the production recorder thread at the Media-close read edge.

        The reader blocks in a controlled peer.  stop changes the real caller
        to STOPPING and aborts the peer, which then returns EPIPE to the exact
        production recording loop.  A pipe fault released while RECORDING must
        remain an error; cancellation must still win the terminal result.
        """
        source = voice_source()
        code = r"""
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#define AGENT_ASR_CHUNK_SIZE 640
#define AGENT_VOICE_SAMPLE_RATE 16000
#define AGENT_VOICE_CHANNELS 1
#define AUTO_ENDPOINT_SILENCE_MS 1
#define AUTO_ENDPOINT_MIN_SPEECH_MS 1
#define AUTO_ENDPOINT_WAIT_MS 1000
#define AUTO_ENDPOINT_MEAN_ABS 1
#define AUTO_ENDPOINT_MAX_MS 1000
#define VOICE_IDLE 0
#define VOICE_STARTING 1
#define VOICE_RECORDING 2
#define VOICE_STOPPING 3
#define VOICE_PROCESSING 4
#define VOICE_CHANNEL_EVENT_WAKE_ACK_CANCEL 1
#define VOICE_CHANNEL_EVENT_CAPTURE_QUIESCENT 2
#define VOICE_CHANNEL_EVENT_OUTPUT_FINISHED 3
#define VOICE_CHANNEL_EVENT_TURN_COMPLETE 4
#define AUTO_TURN_TIMEOUT_MS 180000
#define TAG "test"
enum voice_state { test_voice_idle = VOICE_IDLE,
 test_voice_starting = VOICE_STARTING, test_voice_recording = VOICE_RECORDING,
 test_voice_stopping = VOICE_STOPPING, test_voice_processing = VOICE_PROCESSING };
typedef struct { int unused; } audio_capture_t;
typedef struct { int unused; } voice_asr_stream_t;
static audio_capture_t capture;
static pthread_mutex_t peer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t peer_changed = PTHREAD_COND_INITIALIZER;
static int read_entered, release_read, peer_result, aborts, closes, recognizes;
static size_t endpoint_samples, endpoint_sent;
static struct {
 pthread_mutex_t lock;
 int state, turn_active, preconnect_active, canceled, wake_ack_pending;
 int wake_ack_result, auto_endpoint, tts_abort, tts_active;
 int capture_error, capture_cleanup_pending, capture_cleanup_result;
 int tts_cleanup_pending, tts_cleanup_result, cleanup_in_progress;
 int reply_stream_active;
 audio_capture_t *cap;
 voice_asr_stream_t *asr_stream;
 void *tts_pb;
 unsigned char *pcm_buf;
 size_t pcm_len, pcm_cap;
 pthread_t rec_thread;
 uint64_t request_id, turn_started_ms;
 sem_t rec_ready, rec_done;
} s_voice = {.lock = PTHREAD_MUTEX_INITIALIZER};
static struct timespec s_asr_done_ts;
static uint64_t voice_now_ms(void) { return 100; }
static void notify_channel_event(int event, int result)
{ (void)event; (void)result; }
static void reply_wake(void) {}
static int alloc_fallback_buffer_locked(void) {
 s_voice.pcm_buf = calloc(1, 4); s_voice.pcm_len = 4; return s_voice.pcm_buf ? 0 : -ENOMEM;
}
static int wake_ack_gate(audio_capture_t *c, uint64_t id,
 unsigned char **r, size_t *n) { (void)c; (void)id; (void)r; (void)n; return 0; }
static int process_audio_chunk(voice_asr_stream_t **s, const unsigned char *p,
 size_t n, int *fallback, size_t *sent)
{ (void)s; (void)p; (void)fallback; (void)sent; endpoint_sent += n; return 0; }
static int audio_capture_read(audio_capture_t *cap, void *buf, size_t n) {
 (void)cap;
 if (s_voice.auto_endpoint) {
  assert(n == 640 && endpoint_samples < 48000);
  int16_t *pcm = buf;
  for (size_t i = 0; i < n / 2; i++) {
   size_t at = endpoint_samples + i;
   /* Slow speech, a 600 ms internal pause, then a spoken continuation. */
   int speech = (at >= 3200 && at < 12800) || (at >= 22400 && at < 27200);
   pcm[i] = (i & 1 ? 1 : -1) * (speech ? 200 : 8);
  }
  endpoint_samples += n / 2;
  return (int)n;
 }
 pthread_mutex_lock(&peer_lock); read_entered = 1; pthread_cond_broadcast(&peer_changed);
 while (!release_read) pthread_cond_wait(&peer_changed, &peer_lock);
 pthread_mutex_unlock(&peer_lock); return peer_result;
}
static int audio_capture_abort(audio_capture_t *cap) {
 assert(cap == &capture); aborts++; pthread_mutex_lock(&peer_lock);
 release_read = 1; pthread_cond_broadcast(&peer_changed); pthread_mutex_unlock(&peer_lock); return 0;
}
static int audio_capture_close(audio_capture_t *cap) { assert(cap == &capture); closes++; return 0; }
static int audio_capture_cleanup(unsigned ms) { (void)ms; return 0; }
static int audio_playback_stop(void *p) { (void)p; return 0; }
static int audio_playback_cleanup(unsigned ms) { (void)ms; return 0; }
static int voice_asr_cancel(void) { return 0; }
static int voice_tts_cancel(void) { return 0; }
static int llm_cancel_request(void) { return 0; }
static void voice_asr_stream_abort(voice_asr_stream_t *s) { (void)s; }
static int voice_asr_stream_finish(voice_asr_stream_t *s, char *t, size_t n)
{ (void)s; (void)t; (void)n; return -ECANCELED; }
static int voice_asr_recognize_checked(const void *p, size_t l, char *t,
 size_t n, int (*check)(void *), void *r)
{ (void)p; (void)l; (void)check; (void)r; recognizes++; assert(n > 2); strcpy(t, "ok"); return 0; }
"""
        for setting in ("SILENCE_MS", "MIN_SPEECH_MS", "WAIT_MS", "MEAN_ABS", "MAX_MS"):
            name = "AUTO_ENDPOINT_" + setting
            definition = re.search(r"^#define " + name + r" ([^\n]+)", source, re.M)
            self.assertIsNotNone(definition)
            code = re.sub(r"^#define " + name + r" [^\n]+", definition.group(0), code, flags=re.M)
        for name in (
            "voice_request_status", "voice_request_check", "voice_request_complete",
            "voice_channel_cancel", "voice_channel_recover", "voice_channel_stop_with_text",
            "recording_thread",
        ):
            code += "\n" + function(source, name)
        code += r"""
static void reset(void) {
 s_voice.state=VOICE_RECORDING; s_voice.turn_active=1; s_voice.canceled=0;
 s_voice.cap=&capture; s_voice.asr_stream=NULL; s_voice.pcm_buf=NULL; s_voice.pcm_len=0;
 s_voice.capture_error=0; s_voice.capture_cleanup_pending=0; s_voice.request_id++;
 read_entered=release_read=aborts=closes=recognizes=0; peer_result=-EPIPE;
 sem_init(&s_voice.rec_ready,0,0); sem_init(&s_voice.rec_done,0,0);
 assert(pthread_create(&s_voice.rec_thread,NULL,recording_thread,NULL)==0);
 pthread_mutex_lock(&peer_lock); while (!read_entered) pthread_cond_wait(&peer_changed,&peer_lock); pthread_mutex_unlock(&peer_lock);
}
static void release_recording_pipe(void) {
 pthread_mutex_lock(&peer_lock); release_read=1; pthread_cond_broadcast(&peer_changed); pthread_mutex_unlock(&peer_lock);
 assert(pthread_join(s_voice.rec_thread,NULL)==0);
}
int main(int argc, char **argv) {
 assert(argc == 2);
 char text[16];
 if (!strcmp(argv[1], "endpoint-pause")) {
  s_voice.state = VOICE_RECORDING; s_voice.turn_active = 1;
  s_voice.request_id = 9; s_voice.cap = &capture; s_voice.auto_endpoint = 1;
  sem_init(&s_voice.rec_ready, 0, 0); sem_init(&s_voice.rec_done, 0, 0);
  assert(!pthread_create(&s_voice.rec_thread, NULL, recording_thread, NULL));
  assert(!pthread_join(s_voice.rec_thread, NULL));
  assert(!s_voice.capture_error);
  /* Known PCM ends speech at sample 27200; production retains 900 ms tail. */
  assert(endpoint_samples == 41600 && endpoint_sent == 83200);
  free(s_voice.pcm_buf);
  puts("VOICE_ENDPOINT_PAUSE_PASS last_speech_sample=27200 endpoint_sample=41600 rate=16000 tail_ms=900");
  return 0;
 }
 reset();
 if (!strcmp(argv[1], "stop") || !strcmp(argv[1], "stop-eof") ||
     !strcmp(argv[1], "stop-ecanceled")) {
  /* Expected Green: ownership-initiated EPIPE is not an ASR failure. */
  if (!strcmp(argv[1], "stop-eof")) peer_result = 0;
  if (!strcmp(argv[1], "stop-ecanceled")) peer_result = -ECANCELED;
  assert(voice_channel_stop_with_text(text,sizeof(text)) == 0);
  assert(!strcmp(text,"ok") && recognizes == 1 && aborts == 1 && closes == 1);
 } else if (!strcmp(argv[1], "stop-eio")) {
  peer_result = -EIO;
  assert(voice_channel_stop_with_text(text,sizeof(text)) == -EIO);
  assert(!text[0] && recognizes == 0 && aborts == 1 && closes == 1);
 } else if (!strcmp(argv[1], "existing-error")) {
  s_voice.capture_error = -EAGAIN;
  assert(voice_channel_stop_with_text(text,sizeof(text)) == -EAGAIN);
  assert(!text[0] && recognizes == 0 && aborts == 1 && closes == 1);
 } else if (!strcmp(argv[1], "recording-pipe")) {
  release_recording_pipe();
  assert(s_voice.capture_error == -EPIPE);
 } else if (!strcmp(argv[1], "cancel")) {
  assert(voice_channel_cancel() == 0);
  assert(voice_channel_stop_with_text(text,sizeof(text)) == -ECANCELED);
  assert(!text[0] && aborts >= 1);
 } else assert(0);
 puts("VOICE_CAPTURE_STOP_EPIPE_PASS");
 return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="agent-capture-stop-epipe-") as path:
            directory = Path(path)
            source_path = directory / "test.c"
            binary = directory / "test"
            source_path.write_text(
                code.replace(
                    "#include <syslog.h>",
                    '#include <syslog.h>\n#include "'
                    + str(AGENT / "src/core/agent_trace.h")
                    + '"',
                )
            )
            subprocess.run(
                ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                 str(source_path), "-o", str(binary)], check=True
            )
            for mode in ("endpoint-pause", "recording-pipe", "cancel", "stop", "stop-eof",
                         "stop-ecanceled", "stop-eio", "existing-error"):
                subprocess.run([str(binary), mode], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
