#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check capture ownership using production lifecycle functions and safe peers.

The capture peer is a static object and is never freed. The close boundary
asserts ownership before any ASR work. This is a host lifecycle contract, not
an audio backend, network, memory-error reproduction, or hardware test.
"""

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
AGENT = Path(os.environ.get("AI_AGENT_ROOT", ROOT.parent / "packages/ai_agent"))


def function(source, name):
    match = re.search(
        r"(?:static )?(?:int|void) " + re.escape(name) + r"\([^;{}]*\)\s*\{",
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
        source = (AGENT / "src/voice/voice_channel.c").read_text()
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
            source_path.write_text(code)
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
        source = (AGENT / "src/voice/voice_channel.c").read_text()
        start = function(source, "voice_channel_start_internal")
        # Both create and start failures publish the same detached ownership.
        branches = start.split("audio_capture_t* cap = s_voice.cap;")[1:]
        self.assertEqual(len(branches), 2)
        for branch in branches:
            before_close = branch.split("audio_capture_close(cap)", 1)[0]
            before_unlock = before_close.split("pthread_mutex_unlock", 1)[0]
            self.assertIn("s_voice.cap = NULL;", before_unlock)


if __name__ == "__main__":
    unittest.main()
