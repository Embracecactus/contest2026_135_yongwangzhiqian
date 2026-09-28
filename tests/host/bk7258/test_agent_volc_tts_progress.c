/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "infra/vela_tls.h"
#include "voice/voice_tts.h"

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
static enum {
    READ_SCRIPT,
    READ_CANCEL_BLOCKED,
    READ_NEXT_SUCCESS,
} read_mode;
static int upgrade_sent;
static int client_fd = -1;
static int server_fd = -1;
static pthread_mutex_t cancel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cancel_changed = PTHREAD_COND_INITIALIZER;
static int read_blocked;
static int worker_done;
static int worker_result;

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
    if (read_mode != READ_SCRIPT && !upgrade_sent) {
        static const char response[] =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n\r\n";
        assert(length >= sizeof(response) - 1);
        memcpy(output, response, sizeof(response) - 1);
        upgrade_sent = 1;
        return (int)(sizeof(response) - 1);
    }
    if (next_event < event_count && wire_offset == events[next_event].offset) {
        now_ms = events[next_event].at_ms;
        next_event++;
    }
    if (wire_offset == wire_size) {
        if (read_mode == READ_CANCEL_BLOCKED) {
            pthread_mutex_lock(&cancel_lock);
            read_blocked = 1;
            pthread_cond_broadcast(&cancel_changed);
            pthread_mutex_unlock(&cancel_lock);
            return (int)recv(client_fd, output, length, 0);
        }
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
    read_mode = READ_SCRIPT;
    upgrade_sent = 0;
    read_blocked = 0;
    worker_done = 0;
    worker_result = 0;
}

/* The cancellation contract runs the real registry, Volc backend and WS
 * receiver.  These shims replace only the external TLS peer and credentials.
 * A real socket is kept so the production interrupt can wake a blocked read. */
int claw_config_get(const char* key, char* buf, size_t size)
{
    (void)key;
    assert(buf != NULL && size > 5);
    snprintf(buf, size, "test");
    return 0;
}

bool http_proxy_is_enabled(void)
{
    return false;
}

int proxy_open_tunnel(const char* host, int port, int timeout_ms)
{
    (void)host;
    (void)port;
    (void)timeout_ms;
    return -ENOTSUP;
}

void mbedtls_ssl_init(mbedtls_ssl_context* ssl)
{
    memset(ssl, 0, sizeof(*ssl));
}

void mbedtls_ssl_free(mbedtls_ssl_context* ssl)
{
    (void)ssl;
}

void mbedtls_ssl_config_init(mbedtls_ssl_config* config)
{
    memset(config, 0, sizeof(*config));
}

void mbedtls_ssl_config_free(mbedtls_ssl_config* config)
{
    (void)config;
}

void mbedtls_net_init(mbedtls_net_context* net)
{
    net->fd = -1;
}

int mbedtls_net_connect(mbedtls_net_context* net, const char* host,
    const char* port, int protocol)
{
    (void)host;
    (void)port;
    (void)protocol;
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    client_fd = pair[0];
    server_fd = pair[1];
    net->fd = client_fd;
    return 0;
}

int mbedtls_net_set_block(mbedtls_net_context* net)
{
    (void)net;
    return 0;
}

int mbedtls_net_send(void* context, const unsigned char* data, size_t size)
{
    (void)context;
    (void)data;
    return (int)size;
}

int mbedtls_net_recv(void* context, unsigned char* data, size_t size)
{
    mbedtls_net_context* net = context;
    return (int)recv(net->fd, data, size, 0);
}

void mbedtls_net_free(mbedtls_net_context* net)
{
    if (net->fd >= 0) close(net->fd);
    net->fd = -1;
    client_fd = -1;
    if (server_fd >= 0) close(server_fd);
    server_fd = -1;
}

void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context* context)
{
    memset(context, 0, sizeof(*context));
}

void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context* context)
{
    (void)context;
}

int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context* context,
    int (*entropy)(void*, unsigned char*, size_t), void* entropy_context,
    const unsigned char* custom, size_t custom_size)
{
    (void)context;
    (void)entropy;
    (void)entropy_context;
    (void)custom;
    (void)custom_size;
    return 0;
}

int mbedtls_ctr_drbg_random(void* context, unsigned char* output, size_t size)
{
    (void)context;
    memset(output, 0x5a, size);
    return 0;
}

int mbedtls_ssl_config_defaults(mbedtls_ssl_config* config, int endpoint,
    int transport, int preset)
{
    (void)config;
    (void)endpoint;
    (void)transport;
    (void)preset;
    return 0;
}

void mbedtls_ssl_conf_authmode(mbedtls_ssl_config* config, int mode)
{
    (void)config;
    (void)mode;
}

void mbedtls_ssl_conf_rng(mbedtls_ssl_config* config,
    int (*random)(void*, unsigned char*, size_t), void* context)
{
    (void)config;
    (void)random;
    (void)context;
}

int mbedtls_ssl_setup(mbedtls_ssl_context* ssl,
    const mbedtls_ssl_config* config)
{
    (void)ssl;
    (void)config;
    return 0;
}

int mbedtls_ssl_set_hostname(mbedtls_ssl_context* ssl, const char* hostname)
{
    (void)ssl;
    (void)hostname;
    return 0;
}

void mbedtls_ssl_set_bio(mbedtls_ssl_context* ssl, void* context,
    mbedtls_ssl_send_t* send, mbedtls_ssl_recv_t* receive,
    mbedtls_ssl_recv_timeout_t* receive_timeout)
{
    (void)ssl;
    (void)context;
    (void)send;
    (void)receive;
    (void)receive_timeout;
}

int mbedtls_ssl_handshake(mbedtls_ssl_context* ssl)
{
    (void)ssl;
    return 0;
}

int mbedtls_ssl_write(mbedtls_ssl_context* ssl, const unsigned char* data,
    size_t size)
{
    (void)ssl;
    (void)data;
    return (int)size;
}

int mbedtls_ssl_close_notify(mbedtls_ssl_context* ssl)
{
    (void)ssl;
    return 0;
}

int mbedtls_base64_encode(unsigned char* output, size_t capacity,
    size_t* output_size, const unsigned char* input, size_t input_size)
{
    (void)input;
    (void)input_size;
    static const char encoded[] = "QUFBQUFBQUFBQUFBQUFBQQ==";
    assert(capacity >= sizeof(encoded) - 1);
    memcpy(output, encoded, sizeof(encoded) - 1);
    *output_size = sizeof(encoded) - 1;
    return 0;
}

int mbedtls_base64_decode(unsigned char* output, size_t capacity,
    size_t* output_size, const unsigned char* input, size_t input_size)
{
    (void)output;
    (void)capacity;
    (void)input;
    (void)input_size;
    *output_size = 0;
    return 0;
}

int vela_https_request(const char* host, const char* port,
    const char* method, const char* path, const vela_header_t* headers,
    const char* body, size_t body_size, char* response, size_t response_size,
    size_t* response_body_size)
{
    (void)host;
    (void)port;
    (void)method;
    (void)path;
    (void)headers;
    (void)body;
    (void)body_size;
    (void)response;
    (void)response_size;
    (void)response_body_size;
    return -EIO;
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

static void connection_close_before_terminal_sequence_is_error(void)
{
    reset_script();
    append_pcm(1, 0x31, 0);

    assert(run_receiver() == -ECONNRESET);
    assert(wire_offset == wire_size);
    assert(chunks == 1);
    assert(sample_count == 1 && samples[0] == 0x31);
    assert(terminal == 0);
}

static void* speak_worker(void* unused)
{
    (void)unused;
    int result = voice_tts_speak_stream("cancel me", received, NULL);
    pthread_mutex_lock(&cancel_lock);
    worker_result = result;
    worker_done = 1;
    pthread_cond_broadcast(&cancel_changed);
    pthread_mutex_unlock(&cancel_lock);
    return NULL;
}

static int wait_for_worker(unsigned int milliseconds)
{
    struct timespec deadline;
    assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
    deadline.tv_nsec += (long)milliseconds * 1000000L;
    deadline.tv_sec += deadline.tv_nsec / 1000000000L;
    deadline.tv_nsec %= 1000000000L;
    pthread_mutex_lock(&cancel_lock);
    while (!worker_done) {
        int result = pthread_cond_timedwait(&cancel_changed, &cancel_lock,
            &deadline);
        if (result == ETIMEDOUT) break;
        assert(result == 0);
    }
    int done = worker_done;
    pthread_mutex_unlock(&cancel_lock);
    return done;
}

static void blocked_cancel_ends_old_request_and_next_request_succeeds(void)
{
    reset_script();
    read_mode = READ_CANCEL_BLOCKED;
    append_pcm(1, 0x41, 0);
    assert(volc_tts_register() == 0);

    pthread_t worker;
    assert(pthread_create(&worker, NULL, speak_worker, NULL) == 0);
    pthread_mutex_lock(&cancel_lock);
    while (!read_blocked) {
        assert(pthread_cond_wait(&cancel_changed, &cancel_lock) == 0);
    }
    pthread_mutex_unlock(&cancel_lock);

    int cancel_result = voice_tts_cancel();
    int forced_cleanup = 0;
    if (cancel_result != 0 || !wait_for_worker(500)) {
        forced_cleanup = 1;
        if (client_fd >= 0) shutdown(client_fd, SHUT_RDWR);
    }
    assert(pthread_join(worker, NULL) == 0);
    int first_result = worker_result;
    int first_chunks = chunks;
    int first_terminal = terminal;

    reset_script();
    read_mode = READ_NEXT_SUCCESS;
    append_pcm(-1, 0x42, 0);
    int next_result = voice_tts_speak_stream("next request", received, NULL);

    fprintf(stderr,
        "cancel_result=%d forced_cleanup=%d first_result=%d "
        "first_chunks=%d first_terminal=%d next_result=%d "
        "next_chunks=%d next_terminal=%d\n",
        cancel_result, forced_cleanup, first_result, first_chunks,
        first_terminal, next_result, chunks, terminal);

    assert(cancel_result == 0);
    assert(forced_cleanup == 0);
    assert(first_result == -ECANCELED);
    assert(first_chunks == 1);
    assert(first_terminal == 0);
    assert(next_result == 0);
    assert(chunks == 1);
    assert(sample_count == 1 && samples[0] == 0x42);
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
    } else if (strcmp(argv[1], "truncated-close") == 0) {
        connection_close_before_terminal_sequence_is_error();
    } else if (strcmp(argv[1], "cancel-blocked-next") == 0) {
        blocked_cancel_ends_old_request_and_next_request_succeeds();
    } else {
        return 2;
    }
    if (strcmp(argv[1], "truncated-close") == 0) {
        printf("CONTRACT_PASS AUD-03.agent-truncated-close\n");
    } else if (strcmp(argv[1], "cancel-blocked-next") == 0) {
        printf("CONTRACT_PASS AUD-03.agent-cancel-blocked-next\n");
    } else {
        printf("CONTRACT_PASS AUD-02.%s\n", argv[1]);
    }
    return 0;
}
