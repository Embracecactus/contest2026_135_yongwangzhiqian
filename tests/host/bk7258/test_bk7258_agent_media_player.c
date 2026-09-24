/* SPDX-License-Identifier: Apache-2.0 */
#ifdef TEST_AGENT_FUNASR
/* A controlled verified byte-stream peer for the real FunASR WS backend. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include "cJSON.h"

/* Observe parsed server messages without changing the production ASR protocol. */
static cJSON* observed_parse(const char* text, const char** end, cJSON_bool require_end);
#define cJSON_ParseWithOpts observed_parse
#include "voice/funasr_asr.c"
#undef cJSON_ParseWithOpts

static int live_metrics_enabled;
static uint64_t live_started_ms, live_online_ms, live_offline_ms, live_end_ms;
static unsigned live_online_count, live_offline_count;

static uint64_t live_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static cJSON* observed_parse(const char* text, const char** end, cJSON_bool require_end)
{
    cJSON* root = cJSON_ParseWithOpts(text, end, require_end);
    if (live_metrics_enabled && root) {
        cJSON* mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
        cJSON* result = cJSON_GetObjectItemCaseSensitive(root, "text");
        cJSON* ending = cJSON_GetObjectItemCaseSensitive(root, "is_end");
        if (cJSON_IsString(mode) && cJSON_IsString(result) && result->valuestring[0]) {
            if (!strcmp(mode->valuestring, "2pass-online")) {
                if (!live_online_count) live_online_ms = live_now_ms();
                live_online_count++;
            } else if (!strcmp(mode->valuestring, "2pass-offline")) {
                if (!live_offline_count) live_offline_ms = live_now_ms();
                live_offline_count++;
            }
        }
        if (cJSON_IsTrue(ending)) live_end_ms = live_now_ms();
    }
    return root;
}

int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen,
    const unsigned char* src, size_t slen)
{
    size_t needed = 4 * ((slen + 2) / 3);
    if (dlen < needed + 1) return -1;
    int count = EVP_EncodeBlock(dst, src, (int)slen);
    if (count < 0) return -1;
    *olen = (size_t)count;
    return 0;
}

typedef struct {
    const char* ca_file;
    SSL_CTX* tls_context;
    SSL* tls;
    int fd;
    pthread_mutex_t state_lock;
    pthread_mutex_t tls_lock;
    int interrupted;
} live_transport_t;

static void live_release(live_transport_t* io)
{
    if (io->tls) SSL_free(io->tls);
    if (io->tls_context) SSL_CTX_free(io->tls_context);
    pthread_mutex_lock(&io->state_lock);
    if (io->fd >= 0) close(io->fd);
    io->fd = -1;
    pthread_mutex_unlock(&io->state_lock);
    io->tls = NULL;
    io->tls_context = NULL;
}

static int live_wait(live_transport_t* io, short events, uint64_t deadline)
{
    while (!__atomic_load_n(&io->interrupted, __ATOMIC_ACQUIRE)) {
        uint64_t now = live_now_ms();
        if (now >= deadline) return -ETIMEDOUT;
        int ms = (int)(deadline - now);
        if (ms > 100) ms = 100;
        struct pollfd pfd = { .fd = io->fd, .events = events };
        int ret = poll(&pfd, 1, ms);
        if (ret > 0) {
            if (pfd.revents & events) return 0;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -ECONNRESET;
        } else if (ret < 0 && errno != EINTR) return -errno;
    }
    return -ECANCELED;
}

static int live_prepare(void* context)
{
    live_transport_t* io = context;
    __atomic_store_n(&io->interrupted, 0, __ATOMIC_RELEASE);
    return 0;
}

static int live_open(void* context, const char* host, uint16_t port,
    uint64_t deadline)
{
    live_transport_t* io = context;
    if (strcmp(host, "localhost") || time(NULL) < 1577836800)
        return -EINVAL;
    int ret = -EIO;
    io->tls_context = SSL_CTX_new(TLS_client_method());
    if (!io->tls_context) goto failed;
    if (SSL_CTX_set_min_proto_version(io->tls_context, TLS1_2_VERSION) != 1)
        goto failed;
    SSL_CTX_set_verify(io->tls_context, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_load_verify_locations(io->tls_context, io->ca_file, NULL) != 1)
        { ret = -EACCES; goto failed; }
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { ret = -errno; goto failed; }
    pthread_mutex_lock(&io->state_lock);
    io->fd = fd;
    if (__atomic_load_n(&io->interrupted, __ATOMIC_ACQUIRE))
        shutdown(fd, SHUT_RDWR);
    pthread_mutex_unlock(&io->state_lock);
    struct sockaddr_in addr = { .sin_family = AF_INET,
        .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (connect(io->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) { ret = -errno; goto failed; }
        ret = live_wait(io, POLLOUT, deadline);
        if (ret) goto failed;
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(io->fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0)
            { ret = -errno; goto failed; }
        if (error) { ret = -error; goto failed; }
    }
    io->tls = SSL_new(io->tls_context);
    if (!io->tls || SSL_set_fd(io->tls, io->fd) != 1 ||
        SSL_set_tlsext_host_name(io->tls, host) != 1 ||
        SSL_set1_host(io->tls, host) != 1) goto failed;
    for (;;) {
        if (__atomic_load_n(&io->interrupted, __ATOMIC_ACQUIRE))
            { ret = -ECANCELED; goto failed; }
        int n = SSL_connect(io->tls);
        if (n == 1) break;
        int error = SSL_get_error(io->tls, n);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
            { ret = -EACCES; goto failed; }
        ret = live_wait(io, error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT,
            deadline);
        if (ret) goto failed;
    }
    if (SSL_get_verify_result(io->tls) != X509_V_OK)
        { ret = -EACCES; goto failed; }
    X509* certificate = SSL_get1_peer_certificate(io->tls);
    if (!certificate) { ret = -EACCES; goto failed; }
    X509_free(certificate);
    return 0;
failed:
    live_release(io);
    return ret;
}

static ssize_t live_transfer(void* context, uint8_t* bytes, size_t len,
    uint64_t deadline, int writing)
{
    live_transport_t* io = context;
    for (;;) {
        if (__atomic_load_n(&io->interrupted, __ATOMIC_ACQUIRE))
            return -ECANCELED;
        pthread_mutex_lock(&io->tls_lock);
        int n = writing ? SSL_write(io->tls, bytes, (int)len) :
                          SSL_read(io->tls, bytes, (int)len);
        int error = n > 0 ? SSL_ERROR_NONE : SSL_get_error(io->tls, n);
        int saved_errno = errno;
        pthread_mutex_unlock(&io->tls_lock);
        if (n > 0) return n;
        if (error == SSL_ERROR_ZERO_RETURN) return 0;
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            int ret = live_wait(io,
                error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline);
            if (ret) return ret;
            continue;
        }
        return error == SSL_ERROR_SYSCALL && saved_errno ? -saved_errno : -EIO;
    }
}

static ssize_t live_send(void* context, const uint8_t* bytes, size_t len,
    uint64_t deadline)
{ return live_transfer(context, (uint8_t*)bytes, len, deadline, 1); }

static ssize_t live_recv(void* context, uint8_t* bytes, size_t len,
    uint64_t deadline)
{ return live_transfer(context, bytes, len, deadline, 0); }

static int live_interrupt(void* context)
{
    live_transport_t* io = context;
    pthread_mutex_lock(&io->state_lock);
    __atomic_store_n(&io->interrupted, 1, __ATOMIC_RELEASE);
    if (io->fd >= 0) shutdown(io->fd, SHUT_RDWR);
    pthread_mutex_unlock(&io->state_lock);
    return 0;
}

static int live_close(void* context)
{ live_release(context); return 0; }

static int live_random(void* context, uint8_t* bytes, size_t len)
{ (void)context; return RAND_bytes(bytes, (int)len) == 1 ? 0 : -EIO; }

static int live_sha1(void* context, const uint8_t* bytes, size_t len,
    uint8_t digest[20])
{ (void)context; return SHA1(bytes, len, digest) ? 0 : -EIO; }

static uint64_t live_clock(void* context)
{ (void)context; return live_now_ms(); }

static funasr_asr_transport_t live_io = {
    .prepare_request = live_prepare, .open_verified = live_open,
    .send = live_send, .recv = live_recv, .interrupt = live_interrupt,
    .close = live_close, .random = live_random, .sha1 = live_sha1,
    .now_ms = live_clock,
};

static pthread_mutex_t peer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t peer_changed = PTHREAD_COND_INITIALIZER;
static uint8_t peer_rx[8192], peer_tx[8192];
static size_t peer_rx_size, peer_tx_size;
static int peer_interrupted, peer_open_blocked, peer_open_entered;
static int peer_close_count, peer_mode;
static int peer_close_fail_once;
static int peer_open_error;
static int peer_frame_count, peer_opcodes[5];
static char peer_text[5][256];

int voice_asr_register(const voice_asr_ops_t* ops)
{ assert(ops && strcmp(ops->name, "funasr") == 0); return 0; }

static uint64_t peer_now(void* context)
{
    (void)context;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int peer_random(void* context, uint8_t* bytes, size_t len)
{ (void)context; memset(bytes, 0x11, len); return 0; }
static int peer_sha1(void* context, const uint8_t* bytes, size_t len,
    uint8_t digest[20])
{ (void)context; return SHA1(bytes, len, digest) ? 0 : -EIO; }

static void peer_queue(const uint8_t* bytes, size_t len)
{
    assert(peer_rx_size + len <= sizeof(peer_rx));
    memcpy(peer_rx + peer_rx_size, bytes, len);
    peer_rx_size += len;
    pthread_cond_broadcast(&peer_changed);
}

static void peer_text_frame(const char* json)
{
    size_t len = strlen(json);
    assert(len < 126);
    uint8_t hdr[2] = { 0x81, (uint8_t)len };
    peer_queue(hdr, sizeof(hdr));
    peer_queue((const uint8_t*)json, len);
}

static void peer_parse_frames(void)
{
    while (peer_tx_size >= 2) {
        uint8_t opcode = peer_tx[0] & 0x0f;
        size_t plen = peer_tx[1] & 0x7f, header = 2;
        if (plen == 126) {
            if (peer_tx_size < 4) return;
            plen = (size_t)peer_tx[2] * 256 + peer_tx[3];
            header = 4;
        }
        assert(peer_tx[0] & 0x80);
        assert(peer_tx[1] & 0x80);
        if (peer_tx_size < header + 4 + plen) return;
        assert(peer_frame_count < 5);
        int index = peer_frame_count++;
        peer_opcodes[index] = opcode;
        if (opcode == 1) {
            assert(plen < sizeof(peer_text[0]));
            for (size_t i = 0; i < plen; i++)
                peer_text[index][i] = peer_tx[header + 4 + i] ^
                    peer_tx[header + (i & 3)];
            peer_text[index][plen] = 0;
        } else if (opcode == 2) {
            assert(plen == 4);
            for (size_t i = 0; i < plen; i++)
                assert((uint8_t)(peer_tx[header + 4 + i] ^
                    peer_tx[header + (i & 3)]) == i + 1);
        } else if (opcode == 10) {
            assert(plen == 1 && (peer_tx[header + 4] ^
                peer_tx[header]) == 'x');
        }
        memmove(peer_tx, peer_tx + header + 4 + plen,
            peer_tx_size - header - 4 - plen);
        peer_tx_size -= header + 4 + plen;
        pthread_cond_broadcast(&peer_changed);
        if (opcode == 1 && peer_mode == 2 && index == 0) {
            static const uint8_t ping[] = { 0x89, 1, 'x' };
            peer_queue(ping, sizeof(ping));
            static const char online[] =
                "{\"mode\":\"2pass-online\",\"text\":\"partial\",\"is_final\":false}";
            size_t half = strlen(online) / 2;
            uint8_t first[2] = { 0x01, (uint8_t)half };
            uint8_t last[2] = { 0x80, (uint8_t)(strlen(online) - half) };
            peer_queue(first, sizeof(first));
            peer_queue((const uint8_t*)online, half);
            peer_queue(last, sizeof(last));
            peer_queue((const uint8_t*)online + half, strlen(online) - half);
        }
        if (opcode == 1 && strstr(peer_text[index], "false")) {
            if (peer_mode == 1) peer_text_frame("{\"error\":\"failed\"}");
            else if (peer_mode == 6) {
                peer_text_frame("{\"mode\":\"2pass-offline\",\"text\":\"partial\",\"is_final\":true}");
                peer_text_frame("{\"error\":\"failed\",\"is_end\":true}");
            } else if (peer_mode == 7) peer_text_frame("{\"is_end\":true}");
            else if (peer_mode == 4) {
                static const uint8_t oversized[] = { 0x81, 126, 0x10, 0x01 };
                peer_queue(oversized, sizeof(oversized));
            } else if (peer_mode == 5) {
                static const uint8_t close_frame[] = { 0x88, 2, 3, 232 };
                peer_queue(close_frame, sizeof(close_frame));
            }
            else {
                assert(strstr(peer_text[index], "\"is_end\":true"));
                peer_text_frame("{\"mode\":\"2pass-offline\",\"text\":\"do\",\"is_final\":true}");
                peer_text_frame("{\"mode\":\"2pass-offline\",\"text\":\"ne\",\"is_final\":true}");
                peer_text_frame("{\"is_end\":true}");
            }
        }
    }
}

static int peer_open(void* context, const char* host, uint16_t port,
    uint64_t deadline)
{
    (void)context; (void)deadline;
    assert(strcmp(host, "asr.example.test") == 0 && port == 443);
    pthread_mutex_lock(&peer_lock);
    peer_open_entered = 1;
    pthread_cond_broadcast(&peer_changed);
    while (peer_open_blocked && !peer_interrupted)
        pthread_cond_wait(&peer_changed, &peer_lock);
    int ret = peer_interrupted ? -ECANCELED : 0;
    pthread_mutex_unlock(&peer_lock);
    return ret;
}

static ssize_t peer_send(void* context, const uint8_t* bytes, size_t len,
    uint64_t deadline)
{
    (void)context; (void)deadline;
    pthread_mutex_lock(&peer_lock);
    if (peer_interrupted) { pthread_mutex_unlock(&peer_lock); return -ECANCELED; }
    if (len >= 4 && memcmp(bytes, "GET ", 4) == 0) {
        char request[2048];
        assert(len < sizeof(request));
        memcpy(request, bytes, len);
        request[len] = 0;
        assert(strstr(request, "GET /asr HTTP/1.1\r\n"));
        char* key = strstr(request, "Sec-WebSocket-Key: ");
        assert(key);
        key += strlen("Sec-WebSocket-Key: ");
        char* end = strstr(key, "\r\n");
        assert(end);
        char challenge[128];
        size_t keylen = (size_t)(end - key);
        memcpy(challenge, key, keylen);
        memcpy(challenge + keylen, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
        uint8_t digest[20], encoded[32];
        size_t encoded_len;
        SHA1((uint8_t*)challenge, keylen + 36, digest);
        assert(mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_len,
            digest, sizeof(digest)) == 0);
        char response[256];
        int n = snprintf(response, sizeof(response),
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %.*s\r\nSec-WebSocket-Protocol: binary\r\n\r\n",
            (int)encoded_len, encoded);
        if (peer_mode == 3) {
            char* accept = strstr(response, "Sec-WebSocket-Accept: ");
            assert(accept);
            accept[strlen("Sec-WebSocket-Accept: ")] = 'X';
        }
        peer_queue((const uint8_t*)response, (size_t)n);
    } else {
        assert(peer_tx_size + len <= sizeof(peer_tx));
        memcpy(peer_tx + peer_tx_size, bytes, len);
        peer_tx_size += len;
        peer_parse_frames();
    }
    pthread_mutex_unlock(&peer_lock);
    return (ssize_t)len;
}

static ssize_t peer_recv(void* context, uint8_t* bytes, size_t len,
    uint64_t deadline)
{
    (void)context; (void)deadline;
    pthread_mutex_lock(&peer_lock);
    while (!peer_rx_size && !peer_interrupted) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        if (pthread_cond_timedwait(&peer_changed, &peer_lock, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&peer_lock);
            return -ETIMEDOUT;
        }
    }
    if (peer_interrupted) { pthread_mutex_unlock(&peer_lock); return -ECANCELED; }
    if (len > peer_rx_size) len = peer_rx_size;
    memcpy(bytes, peer_rx, len);
    memmove(peer_rx, peer_rx + len, peer_rx_size - len);
    peer_rx_size -= len;
    pthread_mutex_unlock(&peer_lock);
    return (ssize_t)len;
}

static int peer_interrupt(void* context)
{
    (void)context;
    pthread_mutex_lock(&peer_lock);
    peer_interrupted = 1;
    pthread_cond_broadcast(&peer_changed);
    pthread_mutex_unlock(&peer_lock);
    return 0;
}
static int peer_close(void* context)
{
    (void)context;
    peer_close_count++;
    if (peer_close_fail_once) { peer_close_fail_once = 0; return -EIO; }
    return 0;
}

static funasr_asr_transport_t peer_io = {
    .open_verified = peer_open, .send = peer_send, .recv = peer_recv,
    .interrupt = peer_interrupt, .close = peer_close,
    .random = peer_random, .sha1 = peer_sha1, .now_ms = peer_now,
};

static void peer_reset(int mode)
{
    pthread_mutex_lock(&peer_lock);
    peer_rx_size = peer_tx_size = 0;
    peer_interrupted = peer_open_entered = 0;
    peer_frame_count = peer_close_count = 0;
    peer_close_fail_once = 0;
    peer_open_blocked = 0;
    peer_mode = mode;
    memset(peer_text, 0, sizeof(peer_text));
    pthread_mutex_unlock(&peer_lock);
    assert(backend_prepare_request() == 0);
}

static void* peer_open_worker(void* unused)
{
    (void)unused;
    void* stream = stream_open();
    peer_open_error = errno;
    return stream;
}

static int run_live(int argc, char** argv)
{
    if (argc != 7 || strcmp(argv[2], "localhost")) {
        fprintf(stderr, "usage: %s --live localhost PORT PATH CA_PEM PCM_S16LE_16K_MONO\n",
            argv[0]);
        return 2;
    }
    char* port_end = NULL;
    unsigned long port = strtoul(argv[3], &port_end, 10);
    if (!argv[3][0] || *port_end || !port || port > UINT16_MAX)
        { fprintf(stderr, "invalid port\n"); return 2; }
    FILE* pcm = fopen(argv[6], "rb");
    if (!pcm) { perror("PCM open"); return 2; }
    struct stat info;
    if (fstat(fileno(pcm), &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || info.st_size > 640000 || (info.st_size & 1)) {
        fprintf(stderr, "PCM must be a nonempty regular 16-bit file, at most 20 seconds\n");
        fclose(pcm);
        return 2;
    }
    live_transport_t io = { .ca_file = argv[5], .fd = -1,
        .state_lock = PTHREAD_MUTEX_INITIALIZER,
        .tls_lock = PTHREAD_MUTEX_INITIALIZER };
    int ret = funasr_asr_configure(&live_io, &io, argv[2], (uint16_t)port,
        argv[4]);
    if (!ret) ret = backend_init();
    if (!ret) ret = backend_prepare_request();
    live_started_ms = live_now_ms();
    live_metrics_enabled = 1;
    voice_asr_stream_handle_t stream = NULL;
    if (!ret) {
        stream = stream_open();
        if (!stream) ret = -errno;
    }
    int tls_verified = stream != NULL;
    uint64_t opened_ms = live_now_ms();
    uint8_t chunk[3200]; /* 100 ms of 16 kHz, mono, signed 16-bit PCM. */
    size_t sent = 0;
    uint64_t next_ms = opened_ms;
    while (!ret && sent < (size_t)info.st_size) {
        size_t count = sizeof(chunk);
        if (count > (size_t)info.st_size - sent)
            count = (size_t)info.st_size - sent;
        struct timespec ts = { .tv_sec = (time_t)(next_ms / 1000),
            .tv_nsec = (long)((next_ms % 1000) * 1000000) };
        int sleep_error;
        while ((sleep_error = clock_nanosleep(CLOCK_MONOTONIC,
            TIMER_ABSTIME, &ts, NULL)) == EINTR) {}
        if (sleep_error) { ret = -sleep_error; break; }
        if (fread(chunk, 1, count, pcm) != count) { ret = -EIO; break; }
        ret = stream_send(stream, chunk, count);
        if (!ret) sent += count;
        next_ms += (uint64_t)count * 1000 / 32000;
    }
    char transcript[FUNASR_RESULT_MAX] = {0};
    if (stream) {
        if (ret) stream_abort(stream);
        else ret = stream_finish(stream, transcript, sizeof(transcript));
    }
    uint64_t finished_ms = live_now_ms();
    live_metrics_enabled = 0;
    fclose(pcm);
    size_t transcript_bytes = strlen(transcript);
    char transcript_sha256[SHA256_DIGEST_LENGTH * 2 + 1] = "-";
    if (!ret) {
        uint8_t digest[SHA256_DIGEST_LENGTH];
        if (!SHA256((const uint8_t*)transcript, transcript_bytes, digest))
            ret = -EIO;
        else {
            static const char hex[] = "0123456789abcdef";
            for (size_t i = 0; i < sizeof(digest); i++) {
                transcript_sha256[i * 2] = hex[digest[i] >> 4];
                transcript_sha256[i * 2 + 1] = hex[digest[i] & 15];
            }
            transcript_sha256[sizeof(digest) * 2] = 0;
        }
        OPENSSL_cleanse(digest, sizeof(digest));
    }
    OPENSSL_cleanse(transcript, sizeof(transcript));
    pthread_mutex_destroy(&io.state_lock);
    pthread_mutex_destroy(&io.tls_lock);
    printf("FUNASR_LIVE %s error=%d tls=%s host=localhost "
           "pcm_bytes=%zu audio_ms=%zu open_ms=%llu "
           "online_count=%u first_online_ms=%lld "
           "offline_count=%u first_offline_ms=%lld "
           "end_ack_ms=%lld finish_ms=%llu transcript_bytes=%zu "
           "transcript_sha256=%s\n",
        ret ? "FAIL" : "PASS", ret,
        tls_verified ? "verified" : "unavailable", sent, sent / 32,
        (unsigned long long)(opened_ms - live_started_ms),
        live_online_count,
        live_online_count ? (long long)(live_online_ms - live_started_ms) : -1,
        live_offline_count,
        live_offline_count ? (long long)(live_offline_ms - live_started_ms) : -1,
        live_end_ms ? (long long)(live_end_ms - live_started_ms) : -1,
        (unsigned long long)(finished_ms - live_started_ms),
        transcript_bytes, transcript_sha256);
    return ret ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "--live")) return run_live(0, argv);
        assert(funasr_asr_register() == 0);
        return run_live(argc, argv);
    }
    assert(funasr_asr_register() == 0);
    assert(funasr_asr_configure(&peer_io, NULL, "bad\r\nhost", 443,
        "/asr") == -EINVAL);
    assert(funasr_asr_configure(&peer_io, NULL, "asr.example.test", 443,
        "/asr\r\n") == -EINVAL);
    assert(funasr_asr_configure(&peer_io, NULL, "asr.example.test", 443,
        "/asr") == 0);
    for (int mode = 0; mode < 3; mode++) {
        peer_reset(mode);
        voice_asr_stream_handle_t stream = stream_open();
        assert(stream);
        if (mode == 2) {
            pthread_mutex_lock(&peer_lock);
            while (peer_frame_count < 2)
                pthread_cond_wait(&peer_changed, &peer_lock);
            pthread_mutex_unlock(&peer_lock);
            assert(peer_opcodes[1] == 10);
        }
        uint8_t pcm[] = { 1, 2, 3, 4 };
        assert(stream_send(stream, pcm, sizeof(pcm)) == 0);
        char text[32];
        int ret = stream_finish(stream, text, sizeof(text));
        assert(ret == (mode == 1 ? -EPROTO : 0));
        if (mode != 1) assert(strcmp(text, "done") == 0);
        int offset = mode == 2;
        assert(peer_frame_count == 3 + offset && peer_opcodes[0] == 1 &&
            peer_opcodes[1 + offset] == 2 && peer_opcodes[2 + offset] == 1);
        assert(strstr(peer_text[0], "\"mode\":\"2pass\""));
        assert(strstr(peer_text[2 + offset], "\"is_speaking\":false"));
        assert(peer_close_count == 1 && !s_active);
    }
    peer_reset(3);
    assert(stream_open() == NULL && errno == EPROTO);
    assert(peer_close_count == 1 && !s_active);
    peer_reset(4);
    voice_asr_stream_handle_t stream = stream_open();
    assert(stream);
    char text[32];
    assert(stream_finish(stream, text, sizeof(text)) == -EOVERFLOW);
    assert(peer_close_count == 1 && !s_active);
    peer_reset(6);
    stream = stream_open();
    assert(stream);
    assert(stream_finish(stream, text, sizeof(text)) == -EPROTO);
    peer_reset(7);
    stream = stream_open();
    assert(stream);
    assert(stream_finish(stream, text, sizeof(text)) == -ENODATA);
    peer_reset(0);
    stream = stream_open();
    assert(stream);
    peer_close_fail_once = 1;
    assert(stream_finish(stream, text, sizeof(text)) == -EIO);
    assert(s_retained && !s_active);
    assert(funasr_asr_configure(&peer_io, NULL, "asr.example.test", 443,
        "/asr") == -EBUSY);
    assert(funasr_asr_recover() == 0 && !s_retained);
    peer_reset(5);
    stream = stream_open();
    assert(stream);
    assert(stream_finish(stream, text, sizeof(text)) == -ECONNRESET);
    assert(peer_frame_count == 3 && peer_opcodes[2] == 8);
    peer_reset(0);
    peer_open_blocked = 1;
    pthread_t thread;
    assert(pthread_create(&thread, NULL, peer_open_worker, NULL) == 0);
    pthread_mutex_lock(&peer_lock);
    while (!peer_open_entered) pthread_cond_wait(&peer_changed, &peer_lock);
    pthread_mutex_unlock(&peer_lock);
    assert(backend_cancel() == 0);
    void* result;
    assert(pthread_join(thread, &result) == 0 && result == NULL);
    assert(peer_open_error == ECANCELED && !s_active);
    peer_reset(0);
    assert(backend_cancel() == 0); /* Before entering provider or publishing active. */
    assert(stream_open() == NULL && errno == ECANCELED);
    assert(!peer_open_entered && !s_active);
    peer_reset(0);
    stream = stream_open();
    assert(stream);
    assert(stream_finish(stream, text, sizeof(text)) == 0);
    puts("BKVOICE_FUNASR_HOST_PASS frames final error cancel endpoint-validation");
    return 0;
}
#elif defined(TEST_AGENT_TTS_QUEUE)
/* 同一播放器验收入口，使用真实 Agent 队列及受控 Media 消费端。 */
#include <assert.h>
#include <stddef.h>
size_t strlcpy(char *, const char *, size_t);
#include "voice/voice_channel.c"

static atomic_int test_canceled;
static int test_mode;
static unsigned int test_fragment_seed;
static unsigned int test_sse_seed;
static size_t test_written;
static unsigned int test_opens;
static pthread_mutex_t test_connect_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_connect_changed = PTHREAD_COND_INITIALIZER;
static int test_connect_entered;
static int test_connect_release;
static int test_connect_aborts;
static int test_cancel_done;
static int test_cancel_result;
static atomic_int test_first_pcm;
static atomic_int test_drains, test_closes;
static int test_stops, test_synth_calls;
static char test_spoken[4][128];

bool voice_asr_stream_supported(void) { return true; }
voice_asr_stream_t* voice_asr_stream_open(void)
{
    pthread_mutex_lock(&test_connect_lock);
    test_connect_entered = 1;
    pthread_cond_broadcast(&test_connect_changed);
    while (!test_connect_release)
        pthread_cond_wait(&test_connect_changed, &test_connect_lock);
    pthread_mutex_unlock(&test_connect_lock);
    return (voice_asr_stream_t*)&test_connect_aborts;
}
void voice_asr_stream_abort(voice_asr_stream_t* stream)
{
    assert(stream == (voice_asr_stream_t*)&test_connect_aborts);
    test_connect_aborts++;
}
int voice_asr_cancel(void) { return 0; }
int llm_cancel_request(void) { return 0; }
int audio_capture_abort(audio_capture_t* cap) { assert(cap); return 0; }

static int16_t test_wake_pcm[16000];
static size_t test_wake_count, test_wake_offset;
static int test_wake_discard, test_ack_requests, test_ack_skips;
static int test_ack_cancels, test_ack_cancel_during_playback;
static audio_capture_t *const test_wake_cap =
    (audio_capture_t *)&test_wake_pcm[0];

int audio_capture_get_handoff_sample(audio_capture_t *cap, uint64_t *sample)
{
    assert(cap == test_wake_cap && sample);
    *sample = 6400;
    return 0;
}

int audio_capture_read_at(audio_capture_t *cap, void *buf, size_t len,
    uint64_t *first_sample)
{
    assert(cap == test_wake_cap && !test_wake_discard);
    if (test_wake_offset == test_wake_count) return -EAGAIN;
    size_t samples = len / 2;
    if (samples > test_wake_count - test_wake_offset)
        samples = test_wake_count - test_wake_offset;
    *first_sample = test_wake_offset;
    memcpy(buf, test_wake_pcm + test_wake_offset, samples * 2);
    test_wake_offset += samples;
    return (int)(samples * 2);
}

int audio_capture_set_discard(audio_capture_t *cap, int discard)
{
    assert(cap == test_wake_cap);
    test_wake_discard = discard;
    return 0;
}

static void test_wake_event(int event, int result)
{
    if (event == VOICE_CHANNEL_EVENT_WAKE_ACK_REQUEST) {
        assert(result == 0);
        assert(test_wake_discard);
        test_ack_requests++;
        if (test_ack_cancel_during_playback == 1) {
            assert(voice_channel_cancel() == 0);
            voice_channel_wake_ack_result(-ECANCELED);
        } else if (test_ack_cancel_during_playback == 2) {
            voice_channel_wake_ack_result(-EIO);
        } else {
            voice_channel_wake_ack_result(0);
        }
    } else if (event == VOICE_CHANNEL_EVENT_WAKE_ACK_SKIP) {
        assert(result == 0);
        assert(!test_wake_discard);
        test_ack_skips++;
    } else if (event == VOICE_CHANNEL_EVENT_WAKE_ACK_CANCEL) {
        assert(result == 0);
        test_ack_cancels++;
    }
}

static void test_wake_gate_case(int immediate_command, int cancel)
{
    memset(test_wake_pcm, 0, sizeof(test_wake_pcm));
    for (size_t i = 0; i < 6400; i++) test_wake_pcm[i] = 1000;
    size_t post = immediate_command ? 3200 : 4800;
    if (immediate_command)
        for (size_t i = 0; i < post; i++) test_wake_pcm[6400 + i] = 1000;
    test_wake_count = 6400 + post;
    test_wake_offset = 0;
    test_wake_discard = 0;
    test_ack_cancel_during_playback = cancel;
    int before_requests = test_ack_requests, before_skips = test_ack_skips;
    pthread_mutex_lock(&s_voice.lock);
    s_voice.initialized = 1;
    s_voice.state = VOICE_RECORDING;
    s_voice.turn_active = 1;
    s_voice.canceled = 0;
    s_voice.cap = test_wake_cap;
    s_voice.request_id++;
    s_voice.turn_started_ms = voice_now_ms();
    s_voice.event_cb = test_wake_event;
    uint64_t id = s_voice.request_id;
    pthread_mutex_unlock(&s_voice.lock);
    unsigned char *replay = NULL;
    size_t replay_len = 0;
    int ret = wake_ack_gate(test_wake_cap, id, &replay, &replay_len);
    if (immediate_command) {
        assert(ret == 1 && replay && replay_len >= 6400 * 2);
        assert(test_ack_requests == before_requests);
        assert(test_ack_skips == before_skips + 1);
        memset(replay, 0, replay_len);
        free(replay);
    } else {
        assert(ret == (cancel == 1 ? -ECANCELED :
            cancel == 2 ? -EIO : 0));
        assert(!replay && !replay_len);
        assert(test_ack_requests == before_requests + 1);
        assert(test_ack_skips == before_skips);
        assert(test_wake_discard == !!cancel);
    }
    pthread_mutex_lock(&s_voice.lock);
    s_voice.cap = NULL;
    pthread_mutex_unlock(&s_voice.lock);
    voice_request_complete(id, ret < 0 ? ret : 0);
    assert(voice_channel_is_idle());
    test_wake_discard = 0;
}

static void* test_preconnect_owner(void* context)
{
    int automatic = *(int*)context;
    pthread_mutex_lock(&s_voice.lock);
    voice_asr_stream_t* stream = NULL;
    int ret = voice_preconnect_locked(automatic, s_voice.start_generation,
        s_voice.request_id, &stream);
    assert(!stream);
    pthread_mutex_unlock(&s_voice.lock);
    return (void*)(intptr_t)ret;
}

static void* test_cancel_worker(void* unused)
{
    (void)unused;
    int ret = voice_channel_cancel();
    pthread_mutex_lock(&test_connect_lock);
    test_cancel_result = ret;
    test_cancel_done = 1;
    pthread_cond_broadcast(&test_connect_changed);
    pthread_mutex_unlock(&test_connect_lock);
    return NULL;
}

static void test_preconnect_cancel(int automatic)
{
    pthread_mutex_lock(&s_voice.lock);
    s_voice.initialized = 1;
    s_voice.state = VOICE_STARTING;
    s_voice.turn_active = automatic;
    s_voice.canceled = 0;
    s_voice.cap = NULL;
    s_voice.tts_pb = NULL;
    s_voice.tts_active = 0;
    s_voice.preconnect_active = 0;
    s_voice.start_generation++;
    s_voice.request_id++;
    uint64_t id = s_voice.request_id;
    pthread_mutex_unlock(&s_voice.lock);

    pthread_mutex_lock(&test_connect_lock);
    test_connect_entered = 0;
    test_connect_release = 0;
    test_connect_aborts = 0;
    test_cancel_done = 0;
    pthread_mutex_unlock(&test_connect_lock);
    pthread_t owner;
    assert(pthread_create(&owner, NULL, test_preconnect_owner, &automatic) == 0);
    pthread_mutex_lock(&test_connect_lock);
    while (!test_connect_entered)
        pthread_cond_wait(&test_connect_changed, &test_connect_lock);
    pthread_mutex_unlock(&test_connect_lock);

    /* Require cancel to finish before releasing the blocked network open. */
    pthread_t cancel_worker;
    assert(pthread_create(&cancel_worker, NULL, test_cancel_worker, NULL) == 0);
    pthread_mutex_lock(&test_connect_lock);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    while (!test_cancel_done) {
        int wait_result = pthread_cond_timedwait(&test_connect_changed,
            &test_connect_lock, &deadline);
        if (wait_result == ETIMEDOUT) break;
        assert(wait_result == 0);
    }
    int canceled_before_release = test_cancel_done;
    pthread_mutex_unlock(&test_connect_lock);
    if (canceled_before_release) {
        if (automatic) voice_request_complete(id, -ECANCELED);
        pthread_mutex_lock(&s_voice.lock);
        assert(s_voice.canceled && s_voice.preconnect_active);
        assert(s_voice.state == VOICE_STARTING);
        assert(s_voice.turn_active == automatic);
        pthread_mutex_unlock(&s_voice.lock);
    }
    pthread_mutex_lock(&test_connect_lock);
    test_connect_release = 1;
    pthread_cond_broadcast(&test_connect_changed);
    pthread_mutex_unlock(&test_connect_lock);
    assert(pthread_join(cancel_worker, NULL) == 0);
    void* result = NULL;
    assert(pthread_join(owner, &result) == 0);
    assert(canceled_before_release && test_cancel_result == 0);
    assert((intptr_t)result == -ECANCELED);
    assert(test_connect_aborts == 1);
    if (automatic) voice_request_complete(id, -ECANCELED);
    assert(voice_channel_is_idle());
}

static atomic_int test_next_sentence_started;

audio_playback_t* audio_playback_open(const char* path, unsigned int rate,
    unsigned int channels, unsigned int bits)
{
    (void)path;
    assert(rate == 24000 && channels == 1 && bits == 16);
    test_opens++;
    assert(test_opens == 1);
    return (audio_playback_t*)&test_opens;
}

int audio_playback_write(audio_playback_t* pb, const void* bytes, size_t size)
{
    assert(pb && size && size % 2 == 0);
    if (test_mode == 7) {
        for (int i = 0; i < 2000 &&
            !atomic_load(&test_next_sentence_started); i++) usleep(1000);
        assert(atomic_load(&test_next_sentence_started));
    }
    if (test_mode == 2) return -EIO;
    if (test_mode == 3) {
        atomic_store(&s_voice.tts_abort, 1);
        return -ECANCELED;
    }
    const unsigned char* pcm = bytes;
    if (test_mode < 5)
        for (size_t i = 0; i < size; i++)
            assert(pcm[i] == (unsigned char)((test_written + i) % 251));
    test_written += size;
    atomic_store(&test_first_pcm, 1);
    usleep(1000); /* 有界慢消费者，强制覆盖队列满和环回。 */
    return (int)size;
}

void audio_playback_stop(audio_playback_t* pb) { (void)pb; test_stops++; }
int audio_playback_drain(audio_playback_t* pb, unsigned int timeout_ms)
{ assert(pb && timeout_ms > 0); test_drains++; return 0; }
int audio_playback_close(audio_playback_t* pb)
{ (void)pb; test_closes++; return 0; }
int audio_playback_cleanup(unsigned int timeout_ms)
{ assert(timeout_ms > 0); return 0; }
int voice_tts_cancel(void) { atomic_store(&test_canceled, 1); return 0; }
bool voice_tts_stream_supported(void) { return true; }
int voice_tts_get_capabilities(voice_tts_capabilities_t* caps)
{
    *caps = (voice_tts_capabilities_t){ .sample_rate = 24000,
        .channels = 1, .bits = 16 };
    return 0;
}

int voice_tts_speak_stream_checked(const char* text, voice_tts_chunk_cb cb,
    void* context, int (*check)(void*), void* request)
{
    (void)check; (void)request;
    if (test_mode >= 5) {
        assert(test_synth_calls < 4);
        snprintf(test_spoken[test_synth_calls], sizeof(test_spoken[0]),
            "%s", text);
        test_synth_calls++;
        if (test_mode == 7 && test_synth_calls == 2)
            atomic_store(&test_next_sentence_started, 1);
    }
    unsigned char chunk[1021]; /* 故意在 PCM 半帧处分块。 */
    size_t total = test_mode == 1 ? 200001 :
        test_mode >= 5 ? 8192 : 200000;
    for (size_t pos = 0; pos < total;) {
        size_t maximum = sizeof(chunk);
        if (test_fragment_seed) {
            test_fragment_seed = test_fragment_seed * 1664525u + 1013904223u;
            maximum = 1u + test_fragment_seed % sizeof(chunk);
        }
        size_t n = total - pos < maximum ? total - pos : maximum;
        for (size_t i = 0; i < n; i++) chunk[i] = (unsigned char)((pos + i) % 251);
        cb(chunk, n, 0, context);
        if (atomic_load(&test_canceled)) return -ECANCELED;
        pos += n;
    }
    if (test_mode == 6) return -EIO;
    cb(NULL, 0, 1, context);
    if (test_mode == 4) cb(NULL, 0, 1, context);
    return 0;
}

static uint64_t test_reply_prepare(int mode)
{
    test_mode = mode;
    atomic_store(&test_next_sentence_started, 0);
    test_written = test_opens = 0;
    test_drains = test_closes = test_stops = test_synth_calls = 0;
    memset(test_spoken, 0, sizeof(test_spoken));
    atomic_store(&test_first_pcm, 0);
    atomic_store(&test_canceled, 0);
    atomic_store(&s_voice.tts_abort, 0);
    pthread_mutex_lock(&s_voice.lock);
    s_voice.initialized = 1; /* Fixture starts after capture initialization. */
    s_voice.state = VOICE_PROCESSING;
    s_voice.turn_active = 1;
    s_voice.canceled = 0;
    s_voice.reply_committed = 0;
    s_voice.tts_active = 0;
    s_voice.reply_stream_active = 0;
    s_voice.tts_pb = NULL;
    s_voice.request_id++;
    s_voice.turn_started_ms = voice_now_ms();
    uint64_t id = s_voice.request_id;
    pthread_mutex_unlock(&s_voice.lock);
    return id;
}

static void test_wait_first_pcm(void)
{
    for (int i = 0; i < 2000 && !atomic_load(&test_first_pcm); i++)
        usleep(1000);
    assert(atomic_load(&test_first_pcm));
}

static int test_sse_delta(void *context, const char *text, size_t length)
{
    uint64_t id = *(uint64_t *)context;
    return voice_channel_reply_stream(id, AGENT_REPLY_DELTA, text, length);
}

static int test_history_count, test_cancel_at_history;
static void *test_cancel_commit_thread(void *unused)
{
    (void)unused;
    assert(voice_channel_cancel() == 0);
    return NULL;
}

static int test_cancel_before_commit(uint64_t id)
{
    /* The bus status check already passed. Schedule cancellation on a second
     * thread exactly before the owner takes the final acceptance lock. */
    pthread_t worker;
    assert(pthread_create(&worker, NULL, test_cancel_commit_thread, NULL) == 0);
    assert(pthread_join(worker, NULL) == 0);
    return voice_request_commit(id);
}

static void test_history(const agent_msg_t *msg, const char *text)
{
    assert(msg->request_id == s_voice.request_id && text && text[0]);
    assert(s_voice.reply_committed);
    test_history_count++;
    if (test_cancel_at_history) assert(voice_channel_cancel() == 0);
}

static void test_body_commit(void)
{
    assert(message_bus_init() == 0);
    uint64_t id = test_reply_prepare(5);
    agent_msg_t msg = { .request_id = id, .request_status = voice_request_status,
        .request_commit = voice_request_commit,
        .request_complete = voice_request_complete };
    test_history_count = test_cancel_at_history = 0;
    assert(voice_channel_cancel() == 0);
    assert(message_bus_reply_with_history(&msg, strdup("before"), 0,
        test_history) == -ECANCELED);
    assert(!test_history_count && !s_voice.reply_committed);

    msg.request_id = test_reply_prepare(5);
    msg.request_commit = test_cancel_before_commit;
    assert(message_bus_reply_with_history(&msg, strdup("raced"), 0,
        test_history) == -ECANCELED);
    assert(!test_history_count && !s_voice.reply_committed);
    msg.request_commit = voice_request_commit;

    msg.request_id = test_reply_prepare(5);
    test_cancel_at_history = 1;
    assert(message_bus_reply_with_history(&msg, strdup("accepted"), 0,
        test_history) == 0);
    assert(test_history_count == 1 && s_voice.reply_committed);
    agent_msg_t delivered;
    assert(message_bus_pop_outbound(&delivered, 0) == 0);
    assert(!strcmp(delivered.content, "accepted"));
    assert(delivered.request_status(delivered.request_id) == -ECANCELED);
    message_bus_msg_free(&delivered);
    voice_request_complete(msg.request_id, -ECANCELED);
    assert(message_bus_reply_with_history(&msg, strdup("duplicate"), 0,
        test_history) == -ESTALE);
    assert(test_history_count == 1);

    /* A duplicate body during the active turn cannot enqueue or write twice. */
    msg.request_id = test_reply_prepare(5);
    test_cancel_at_history = 0;
    assert(message_bus_reply_with_history(&msg, strdup("once"), 0,
        test_history) == 0);
    assert(message_bus_reply_with_history(&msg, strdup("twice"), 0,
        test_history) == -EALREADY);
    assert(s_voice.turn_active && test_history_count == 2);
    assert(message_bus_pop_outbound(&delivered, 0) == 0);
    message_bus_msg_free(&delivered);
    voice_request_complete(msg.request_id, 0);

    /* Untracked synchronous/non-voice replies retain their existing contract. */
    memset(&msg, 0, sizeof(msg));
    assert(message_bus_reply(&msg, strdup("ordinary"), 0) == 0);
    assert(message_bus_pop_outbound(&delivered, 0) == 0);
    assert(!strcmp(delivered.content, "ordinary"));
    message_bus_msg_free(&delivered);
    message_bus_destroy();
}

static void test_sse_feed(llm_final_stream_t *parser, const char *text, size_t length)
{
    for (size_t offset = 0; offset < length;) {
        size_t count = 1;
        if (test_sse_seed) {
            test_sse_seed = test_sse_seed * 1664525u + 1013904223u;
            count = 1 + test_sse_seed % 31;
        }
        if (count > length - offset) count = length - offset;
        assert(llm_final_stream_feed(parser, text + offset, count) == 0);
        offset += count;
    }
}

static void test_sse_pipeline(void)
{
    uint64_t id = test_reply_prepare(5);
    agent_msg_t msg = { .request_id = id, .reply_stream = voice_channel_reply_stream,
        .reply_stream_started = 1, .request_status = voice_request_status,
        .request_commit = voice_request_commit,
        .request_complete = voice_request_complete };
    assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    llm_final_stream_t *parser = llm_final_stream_new(test_sse_delta, &id, 1024);
    assert(parser);
    const char first[] = "data: {\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"DO_NOT_SPEAK\",\"content\":\"你好，这是第一句。\"}}]}\n\n";
    test_sse_feed(parser, first, sizeof(first) - 1);
    test_wait_first_pcm();
    assert(test_drains == 0 && test_closes == 0);
    const char tail[] = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"这是尾句\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
    test_sse_feed(parser, tail, sizeof(tail) - 1);
    char *whole = NULL;
    assert(llm_final_stream_finish(parser, &whole) == 0);
    llm_final_stream_free(parser);
    test_history_count = test_cancel_at_history = 0;
    assert(message_bus_reply_with_history(&msg, whole, 0, test_history) == 0);
    assert(test_history_count == 1 && s_voice.reply_committed);
    assert(test_synth_calls == 2 && test_opens == 1);
    assert(test_drains == 1 && test_closes == 1 && voice_channel_is_idle());
    assert(!strcmp(test_spoken[0], "你好，这是第一句。"));
    assert(!strcmp(test_spoken[1], "这是尾句"));

    msg.request_id = test_reply_prepare(6); /* TTS reports failure at EOF. */
    assert(voice_channel_reply_stream(msg.request_id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(msg.request_id, AGENT_REPLY_DELTA,
        "body", 4) == 0);
    assert(message_bus_reply_with_history(&msg, strdup("body"), 0,
        test_history) == -EIO);
    assert(test_history_count == 1 && !s_voice.reply_committed);

    msg.request_id = test_reply_prepare(5);
    msg.request_commit = test_cancel_before_commit;
    assert(voice_channel_reply_stream(msg.request_id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(msg.request_id, AGENT_REPLY_DELTA,
        "body", 4) == 0);
    assert(message_bus_reply_with_history(&msg, strdup("body"), 0,
        test_history) == -ECANCELED);
    assert(test_history_count == 1 && !s_voice.reply_committed);
    assert(test_drains == 1 && test_closes == 1);
}

static void test_reply_pipeline(void)
{
    uint64_t overlap_id = test_reply_prepare(7);
    const char overlap[] = "第一句话已经完整。第二句话也完整。";
    assert(voice_channel_reply_stream(overlap_id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(overlap_id, AGENT_REPLY_DELTA,
        overlap, sizeof(overlap) - 1) == 0);
    assert(voice_channel_reply_stream(overlap_id, AGENT_REPLY_END, NULL, 0) == 0);
    assert(test_synth_calls == 2 && test_written == 16384);
    assert(test_opens == 1 && test_closes == 1 && test_drains == 1);
    voice_request_complete(overlap_id, 0);

    uint64_t id = test_reply_prepare(5);
    const char first[] = "你好，这是第一句。";
    assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA, first, 2) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA,
        first + 2, sizeof(first) - 3) == 0);
    test_wait_first_pcm();
    assert(test_drains == 0 && test_closes == 0);
    assert(s_voice.turn_active && s_voice.reply_stream_active);
    const char tail[] = "第二句没有标点";
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA,
        tail, sizeof(tail) - 1) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_END, NULL, 0) == 0);
    assert(test_synth_calls == 2 && strcmp(test_spoken[0], first) == 0);
    assert(strcmp(test_spoken[1], tail) == 0);
    assert(test_opens == 1 && test_drains == 1 && test_closes == 1);
    assert(s_voice.turn_active && !s_voice.reply_stream_active);
    voice_request_complete(id, 0);
    assert(voice_channel_is_idle());

    id = test_reply_prepare(6);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA,
        first, sizeof(first) - 1) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_END, NULL, 0) == -EIO);
    /* Producer failure may precede the first Media write. */
    assert(test_opens <= 1 && test_closes == 1);
    voice_request_complete(id, -EIO);
    assert(voice_channel_is_idle());

    id = test_reply_prepare(5);
    const char malformed[] = { (char)0xed, (char)0xa0, (char)0x80 };
    assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA,
        malformed, sizeof(malformed)) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_END, NULL, 0) == -EPROTO);
    assert(test_opens == 0);
    voice_request_complete(id, -EPROTO);
    assert(voice_channel_is_idle());

    id = test_reply_prepare(5);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA,
        first, sizeof(first) - 1) == 0);
    test_wait_first_pcm();
    assert(voice_channel_cancel() == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_ABORT, NULL, 0) == 0);
    assert(test_stops >= 1 && test_closes == 1 && test_drains == 0);
    assert(test_synth_calls == 1);
    voice_request_complete(id, -ECANCELED);
    assert(voice_channel_is_idle());

    id = test_reply_prepare(5);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA,
        first, sizeof(first) - 1) == 0);
    assert(voice_channel_reply_stream(id, AGENT_REPLY_END, NULL, 0) == 0);
    assert(test_opens == 1 && test_drains == 1 && test_closes == 1);
    voice_request_complete(id, 0);
    assert(voice_channel_is_idle());
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        if (!strncmp(argv[1], "pcm-", 4)) {
            test_fragment_seed = (unsigned int)strtoul(argv[1] + 4, NULL, 10);
            assert(test_fragment_seed != 0);
            uint64_t id = test_reply_prepare(0);
            assert(voice_channel_reply_stream(id, AGENT_REPLY_BEGIN, NULL, 0) == 0);
            assert(voice_channel_reply_stream(id, AGENT_REPLY_DELTA, "fixture.", 8) == 0);
            assert(voice_channel_reply_stream(id, AGENT_REPLY_END, NULL, 0) == 0);
            assert(test_written == 200000 && test_drains == 1 && test_closes == 1);
            voice_request_complete(id, 0);
            assert(voice_channel_is_idle());
        } else if (!strcmp(argv[1], "cancel-late")) {
            uint64_t old = test_reply_prepare(5);
            assert(voice_channel_reply_stream(old, AGENT_REPLY_BEGIN, NULL, 0) == 0);
            assert(voice_channel_reply_stream(old, AGENT_REPLY_DELTA, "你好，这是第一句。", strlen("你好，这是第一句。")) == 0);
            test_wait_first_pcm();
            assert(voice_channel_cancel() == 0);
            assert(voice_channel_reply_stream(old, AGENT_REPLY_ABORT, NULL, 0) == 0);
            voice_request_complete(old, -ECANCELED);
            uint64_t current = test_reply_prepare(5);
            assert(voice_channel_reply_stream(current, AGENT_REPLY_BEGIN, NULL, 0) == 0);
            assert(voice_channel_reply_stream(old, AGENT_REPLY_DELTA, "stale.", 6) < 0);
            assert(voice_channel_reply_stream(old, AGENT_REPLY_END, NULL, 0) < 0);
            assert(test_synth_calls == 0);
            assert(voice_channel_reply_stream(current, AGENT_REPLY_DELTA, "这是新的回答。", strlen("这是新的回答。")) == 0);
            assert(voice_channel_reply_stream(current, AGENT_REPLY_END, NULL, 0) == 0);
            assert(test_synth_calls == 1 && !strcmp(test_spoken[0], "这是新的回答。"));
            assert(test_drains == 1 && test_closes == 1);
            voice_request_complete(current, 0);
        } else if (!strncmp(argv[1], "sse-", 4)) {
            test_sse_seed = (unsigned int)strtoul(argv[1] + 4, NULL, 10);
            test_sse_pipeline();
        } else return 2;
        puts("CONTRACT_PASS");
        return 0;
    }
    assert(argc == 1);
    test_wake_gate_case(0, 0);
    test_wake_gate_case(1, 0);
    test_wake_gate_case(0, 1);
    test_wake_gate_case(0, 2);
    test_wake_gate_case(0, 0);
    assert(test_ack_requests == 4 && test_ack_skips == 1 &&
        test_ack_cancels == 3); /* Duplicate cancel notifications are safe. */
    s_voice.event_cb = NULL;
    test_preconnect_cancel(1);
    test_preconnect_cancel(0);
    for (test_mode = 0; test_mode <= 4; test_mode++) {
        tts_output_t output = {0};
        uint64_t id = 1;
        test_written = 0;
        test_opens = 0;
        atomic_store(&test_canceled, 0);
        atomic_store(&s_voice.tts_abort, 0);
        s_voice.tts_pb = NULL;
        clock_gettime(CLOCK_MONOTONIC, &s_tts_start);
        int ret = tts_speak_queued("fixture", &output, 0, &id);
        if (output.error) ret = output.error;
        if (test_mode == 0) {
            assert(ret == 0 && output.terminal && test_written == 200000);
        } else {
            int expected[] = {0, -EPROTO, -EIO, -ECANCELED, -EPROTO};
            assert(ret == expected[test_mode]);
        }
    }
    test_reply_pipeline();
    test_body_commit();
    test_sse_pipeline();
    puts("BKVOICE_AGENT_QUEUE_HOST_PASS atomic-body-commit cancel-before-after history-once stream-failure-no-history wake-ack cancel-next-turn reply-before-end utf8-tail cancel-recover single-media preconnect fragmented-pcm bounded-ring media-error duplicate-eof");
    return 0;
}
#else
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
#endif
