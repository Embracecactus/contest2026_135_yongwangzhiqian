/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Compile the production Volcengine receiver into this translation unit so
 * the test can drive its real WebSocket and protocol parsers.  Only the TLS
 * byte source and monotonic clock are controlled below. */
#include "voice/volc_tts_ws.c"

struct event_s {
    unsigned long long at_ms;
    size_t offset;
};

static unsigned char wire[512];
static size_t wire_size;
static size_t wire_offset;
static struct event_s events[16];
static size_t event_count;
static size_t next_event;
static unsigned long long now_ms;
static int chunks;
static int terminal;
static unsigned char samples[32];
static size_t sample_count;

int clock_gettime(clockid_t clock_id, struct timespec* value)
{
    assert(clock_id == CLOCK_MONOTONIC);
    value->tv_sec = (time_t)(now_ms / 1000u);
    value->tv_nsec = (long)((now_ms % 1000u) * 1000000u);
    return 0;
}

int mbedtls_ssl_read(mbedtls_ssl_context* ssl, unsigned char* output,
    size_t length)
{
    (void)ssl;
    if (next_event < event_count && wire_offset == events[next_event].offset) {
        now_ms = events[next_event].at_ms;
        next_event++;
    }
    if (wire_offset == wire_size) {
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    }
    size_t available = wire_size - wire_offset;
    if (length > available) {
        length = available;
    }
    memcpy(output, wire + wire_offset, length);
    wire_offset += length;
    return (int)length;
}

static void append_ws(int opcode, const unsigned char* payload, size_t size,
    unsigned long long at_ms)
{
    assert(size < 126);
    assert(event_count < sizeof(events) / sizeof(events[0]));
    assert(wire_size + size + 2 <= sizeof(wire));
    events[event_count].at_ms = at_ms;
    events[event_count].offset = wire_size;
    event_count++;
    wire[wire_size++] = (unsigned char)(0x80 | opcode);
    wire[wire_size++] = (unsigned char)size;
    if (size > 0) {
        memcpy(wire + wire_size, payload, size);
        wire_size += size;
    }
}

static void append_ping(unsigned long long at_ms)
{
    append_ws(0x09, NULL, 0, at_ms);
}

static void append_ack(unsigned long long at_ms)
{
    static const unsigned char ack[] = { 0x11, 0xb0, 0x00, 0x00 };
    append_ws(WS_OPCODE_BINARY, ack, sizeof(ack), at_ms);
}

static void append_pcm(int32_t sequence, unsigned char sample,
    unsigned long long at_ms)
{
    unsigned char frame[13] = {
        0x11, 0xb1, 0x00, 0x00,
        0, 0, 0, 0,
        0, 0, 0, 1,
        sample,
    };
    uint32_t seq = (uint32_t)sequence;
    frame[4] = (unsigned char)(seq >> 24);
    frame[5] = (unsigned char)(seq >> 16);
    frame[6] = (unsigned char)(seq >> 8);
    frame[7] = (unsigned char)seq;
    append_ws(WS_OPCODE_BINARY, frame, sizeof(frame), at_ms);
}

static void received(const unsigned char* pcm, size_t size, int is_last,
    void* context)
{
    (void)context;
    if (is_last) {
        assert(pcm == NULL && size == 0);
        terminal++;
        return;
    }
    assert(pcm != NULL && size > 0);
    assert(sample_count + size <= sizeof(samples));
    memcpy(samples + sample_count, pcm, size);
    sample_count += size;
    chunks++;
}

static void reset_script(void)
{
    memset(wire, 0, sizeof(wire));
    memset(events, 0, sizeof(events));
    memset(samples, 0, sizeof(samples));
    wire_size = 0;
    wire_offset = 0;
    event_count = 0;
    next_event = 0;
    now_ms = 0;
    chunks = 0;
    terminal = 0;
    sample_count = 0;
}

static int run_receiver(void)
{
    tts_tls_ctx_t context;
    memset(&context, 0, sizeof(context));
    context.net.fd = -1;
    return recv_tts_audio(&context, received, NULL);
}

static void heartbeat_cannot_extend_pcm_deadline(void)
{
    reset_script();
    append_pcm(1, 0x11, 0);
    append_ping(400);
    append_ack(800);
    append_ping(1200);
    append_ack(1600);
    append_ping(2000);

    assert(run_receiver() == -ETIMEDOUT);
    assert(wire_offset < wire_size);
    assert(chunks == 1);
    assert(sample_count == 1 && samples[0] == 0x11);
    assert(terminal == 0);
}

static void heartbeat_cannot_extend_first_pcm_deadline(void)
{
    reset_script();
    append_ping(2000);
    append_ack(4000);
    append_ping(6000);
    append_ack(8000);
    append_ping(10000);
    append_ack(12000);

    assert(run_receiver() == -ETIMEDOUT);
    assert(wire_offset < wire_size);
    assert(chunks == 0);
    assert(sample_count == 0);
    assert(terminal == 0);
}

static void valid_pcm_refreshes_progress(void)
{
    reset_script();
    append_pcm(1, 0x21, 0);
    append_ping(400);
    append_pcm(2, 0x22, 800);
    append_ack(1200);
    append_pcm(-1, 0x23, 1600);

    assert(run_receiver() == 0);
    assert(wire_offset == wire_size);
    assert(chunks == 3);
    assert(sample_count == 3);
    assert(samples[0] == 0x21 && samples[1] == 0x22 && samples[2] == 0x23);
    assert(terminal == 1);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "heartbeat-deadline") == 0) {
        heartbeat_cannot_extend_pcm_deadline();
    } else if (strcmp(argv[1], "first-pcm-deadline") == 0) {
        heartbeat_cannot_extend_first_pcm_deadline();
    } else if (strcmp(argv[1], "pcm-progress") == 0) {
        valid_pcm_refreshes_progress();
    } else {
        return 2;
    }
    printf("CONTRACT_PASS AUD-02.%s\n", argv[1]);
    return 0;
}
