"""Persist response preference, reload it, and inspect a real controlled HTTP request."""

import subprocess
import tempfile
import re
from pathlib import Path

from test_nfc_rf_lifecycle import ROOT, function
from test_bk7258_cloud_http import APPS, build_http_fixture

HERE = Path(__file__).resolve().parent
AGENT = ROOT.parent / "packages/ai_agent"
CJSON = APPS / "netutils/cjson/cJSON"
PRODUCT = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
CLOUD = (ROOT / "app/bk7258/bk7258_agent_cloud.c").read_text()


def struct_block(name: str) -> str:
    start = CLOUD.index(f"struct {name}\n")
    end = CLOUD.index("\n};", start) + 3
    return CLOUD[start:end]


def cloud_function(name: str) -> str:
    match = re.search(
        r"(?:static )?(?:int|void) " + re.escape(name) + r"\([^;{}]*\)\s*\{", CLOUD
    )
    if not match:
        raise ValueError("production function missing: " + name)
    brace = match.end() - 1
    depth, end = 1, brace + 1
    while depth:
        depth += (CLOUD[end] == "{") - (CLOUD[end] == "}")
        end += 1
    return CLOUD[match.start() : end]


BACKEND = struct_block("cloud_backend_s")
APPLY = cloud_function("llm_apply_response_length")
SET = cloud_function("bkagent_cloud_set_response_length")
GET = cloud_function("bkagent_cloud_get_response_length")


def cloud_range(start: str, end: str) -> str:
    first = CLOUD.index(start)
    return CLOUD[first : CLOUD.index(end, first)]


PREPARE = cloud_range("static int request_prepare(", "\nstatic int request_cancel")
CLOUD_TRANSPORT = cloud_range("static int cloud_open(", "\n/* Opt-in reference ASR")
BODY = cloud_range("struct llm_body_s", "\nstatic int llm_transport")
TRANSPORT = cloud_range("static int llm_transport(", "\nstatic int llm_cancel")
PRODUCT_ROUTE = function(PRODUCT, "product_response_length")

PREFIX = r"""
#define CONFIG_BK7258_AUDIO_PIPELINE_VALIDATION 1
#define CONFIG_BK7258_PREFERENCES 1
#define CONFIG_BK7258_PROVISION_GATT 1
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <syslog.h>
#include <unistd.h>
#include <nuttx/mutex.h>
#include "bk7258_agent_cloud.h"
#include "bk7258_cloud_audio.h"
#include "bk7258_cloud_fixture.h"
#include "bk7258_control_session.h"
#include "bk7258_preferences.h"
#include "bk7258_provision_store.h"
#include "bk7258_voice_tls.h"
#include "bk7258_voice_config.h"
#include "cJSON.h"

static atomic_bool g_voice_initialized;
#define voice_channel_is_idle() true
#define bkprov_network_busy() false
static uint64_t product_be64(const uint8_t *p) { uint64_t v=0; for(unsigned i=0;i<8;i++) v=(v<<8)|p[i]; return v; }
static void product_put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=v>>(24-8*i); }
static void product_put64(uint8_t *p,uint64_t v) { for(unsigned i=0;i<8;i++) p[i]=v>>(56-8*i); }
static char root_path[] = "/tmp/shaniu-response-chain-XXXXXX";
int nxmutex_lock(mutex_t *m) { return -pthread_mutex_lock(m); }
int nxmutex_unlock(mutex_t *m) { return -pthread_mutex_unlock(m); }
int bk7258_preferences_storage_begin(void) { return -ENOTSUP; }
int bk7258_preferences_storage_end(int r) { return r; }
uint32_t bk7258_preferences_storage_generation(void) { return 0; }
int property_get_with_err(const char *k, char *v) { (void)k; (void)v; return -ENOENT; }
int property_set(const char *k, const char *v) { (void)k; (void)v; return -ENOTSUP; }
int property_delete(const char *k) { (void)k; return -ENOENT; }
int property_commit(void) { return -ENOTSUP; }
int __real_mkdir(const char *, mode_t);
int __wrap_mkdir(const char *p, mode_t m) { return __real_mkdir(!strcmp(p,"/cpdata/shaniu/response-length") ? root_path : p,m); }
int __real_bkprov_store_open(struct bkprov_store_s *, const char *);
int __wrap_bkprov_store_open(struct bkprov_store_s *s,const char *p) { return __real_bkprov_store_open(s,!strcmp(p,"/cpdata/shaniu/response-length") ? root_path : p); }
int __real_fsync(int); int __wrap_fsync(int fd) { return __real_fsync(fd); }

/* These declarations are source-adjacent transport state, not another cloud
 * implementation. g_validation makes the real request path use only the
 * maintained in-memory TLS/HTTP peer. */
struct cloud_settings_s { struct bkvoice_config_s trust; };
int bkvoice_config_trusted_time(void *unused) { (void)unused; return 0; }
static atomic_int g_thinking = ATOMIC_VAR_INIT(-1);
static atomic_int g_response_length = ATOMIC_VAR_INIT(-1);
static bool g_validation = true;
uint64_t bkvoice_config_now_ms(void *unused) { (void)unused; return 1000; }
static struct cloud_backend_s g_tts;
#define TTS_IDLE_READ_TIMEOUT_MS 1000u
#define TTS_FIRST_READ_TIMEOUT_MS 1000u
static const struct bkvoice_wss_tls_ops_s g_unused_tls_ops;
const struct bkvoice_wss_tls_ops_s *bkvoice_tls_ops(void) { return &g_unused_tls_ops; }
const struct bkvoice_wss_tls_ops_s *__real_bkcloud_fixture_tls_ops(void);
static const struct bkvoice_wss_tls_ops_s *g_fixture_ops;
static struct bkvoice_wss_tls_ops_s g_capture_ops;
static char captured_request[4096]; static size_t captured_size;
static ssize_t capture_send(void *context, const uint8_t *data, size_t size, uint64_t deadline)
{ ssize_t sent = g_fixture_ops->send(context, data, size, deadline);
  if (sent > 0) { size_t copied = (size_t)sent;
    size_t room = sizeof(captured_request) - 1 - captured_size;
    if (copied > room) copied = room;
    if (copied) memcpy(captured_request + captured_size, data, copied);
    captured_size += copied; captured_request[captured_size] = 0; }
  return sent; }
const struct bkvoice_wss_tls_ops_s *__wrap_bkcloud_fixture_tls_ops(void)
{ g_fixture_ops = __real_bkcloud_fixture_tls_ops(); g_capture_ops = *g_fixture_ops;
  g_capture_ops.send = capture_send; return &g_capture_ops; }
"""

HARNESS = r"""
static int request_ok(void *unused) { (void)unused; return 0; }
static void init_backend(struct cloud_backend_s *backend,
                         struct bkcloud_fixture_ctx_s *peer)
{
  memset(backend, 0, sizeof(*backend));
  backend->settings = (struct cloud_settings_s *)(uintptr_t)1;
  backend->fixture = peer;
  backend->service.dialect = 1;
  backend->service.port = 443;
  strcpy(backend->service.host, "fixture.invalid");
  strcpy(backend->service.base_path, "/v1");
  strcpy(backend->service.api_key, "fixture-only");
  strcpy(backend->service.chat_model, "fixture-chat");
  atomic_init(&backend->canceled, false);
}

static void assert_standard_request(void)
{
  struct cloud_backend_s backend;
  struct bkcloud_fixture_ctx_s peer;
  char reply[1024] = {0}; size_t length = 0; int status = 0;
  const char *request = "{\"messages\":[{\"role\":\"user\",\"content\":\"Please explain in detail.\"}]}";
  bkagent_cloud_set_response_length(0); captured_size = 0; captured_request[0] = 0;
  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_fixture_begin(&peer, BKCLOUD_FIXTURE_LLM, BKCLOUD_FIXTURE_NORMAL) == 0);
  init_backend(&backend, &peer);
  assert(llm_transport(request, reply, sizeof(reply), &length, &status,
                       &backend, request_ok, NULL) == 0);
  assert(status == 200 && length && strstr(captured_request, request));
  assert(!strstr(captured_request, "Prefer one useful conclusion") &&
         !strstr(captured_request, "Provide a complete explanation"));
  assert(bkcloud_fixture_end(&peer) == 0);
}

static void assert_reloaded_request(unsigned int mode)
{
  struct cloud_backend_s backend;
  struct bkcloud_fixture_ctx_s peer;
  struct bk7258_response_length_s restored;
  const char *request = "{\"messages\":[{\"role\":\"user\",\"content\":\"Please explain in detail.\"}]}";
  assert(bk7258_preferences_response_length_get(&restored) == 0);
  assert(restored.mode == mode);
  bkagent_cloud_set_response_length(restored.mode);
  assert(bkagent_cloud_get_response_length(&mode) == 0 && mode == restored.mode);
  assert(bkcloud_fixture_reset(BKCLOUD_FIXTURE_NORMAL) == 0);
  assert(bkcloud_fixture_begin(&peer, BKCLOUD_FIXTURE_LLM, BKCLOUD_FIXTURE_NORMAL) == 0);
  init_backend(&backend, &peer);
  char reply[1024] = {0}; size_t length = 0; int status = 0;
  captured_size = 0; captured_request[0] = 0;
  assert(llm_transport(request, reply, sizeof(reply), &length, &status,
                       &backend, request_ok, NULL) == 0);
  if (mode == 1)
    assert(status == 200 && length && strstr(captured_request, "Prefer one useful conclusion") &&
           strstr(captured_request, "Please explain in detail."));
  else
    assert(status == 200 && length &&
           strstr(captured_request, "Provide a complete explanation") &&
           strstr(captured_request, "explicit length request takes priority") &&
           strstr(captured_request, "Please explain in detail."));
  assert(bkcloud_fixture_end(&peer) == 0);
}

static void reopen(const char *store, unsigned int mode)
{
  strcpy(root_path, store);
  struct bkcontrol_status_s status;
  assert(product_response_length(BKCONTROL_CONFIG_READ, 0, NULL, 0, &status) == 0);
  assert(status.config_chunk[7] == mode);
  assert_reloaded_request(mode);
  puts(mode == 1 ? "FRESH_CONCISE_REQUEST_PASS" : "FRESH_DETAILED_REQUEST_PASS");
}

int main(int argc, char **argv)
{
  struct bkcontrol_status_s status; uint8_t record[32] = {0};
  if (argc == 4) { assert(!strcmp(argv[1], "--reopen")); reopen(argv[2], (unsigned)atoi(argv[3])); return 0; }
  assert(argc == 1 && mkdtemp(root_path));
  assert_standard_request();
  assert(product_response_length(BKCONTROL_CONFIG_READ, 0, NULL, 0, &status) == 0 && status.config_total == 24);
  assert(product_response_length(BKCONTROL_CONFIG_BEGIN, 0, NULL, 32, &status) == 0);
  memcpy(record, "RLP1", 4); record[7] = 1; record[16] = 1;
  assert(product_response_length(BKCONTROL_CONFIG_APPLY, 0, record, sizeof(record), &status) == 0);
  pid_t child = fork(); assert(child >= 0);
  if (!child) { execl(argv[0], argv[0], "--reopen", root_path, "1", (char *)NULL); _exit(127); }
  int child_status; assert(waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) && !WEXITSTATUS(child_status));
  record[7] = 2; record[15] = 1; record[16] = 2;
  assert(product_response_length(BKCONTROL_CONFIG_APPLY, 0, record, sizeof(record), &status) == 0);
  child = fork(); assert(child >= 0);
  if (!child) { execl(argv[0], argv[0], "--reopen", root_path, "2", (char *)NULL); _exit(127); }
  assert(waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) && !WEXITSTATUS(child_status));
  record[7] = 3;
  assert(product_response_length(BKCONTROL_CONFIG_APPLY, 0, record, sizeof(record), &status) == -EBADMSG);
  puts("CONTRACT_PASS response-length product HTTP route");
  return 0;
}
"""

subprocess.run(
    ["make", "-C", str(HERE), "build/test_shaniu_response_length_store"], check=True
)
with tempfile.TemporaryDirectory(prefix="response-length-route-") as directory:
    temp = Path(directory)
    source = temp / "route.c"
    source.write_text(
        "\n".join(
            (
                PREFIX,
                BACKEND,
                SET,
                GET,
                APPLY,
                PREPARE,
                CLOUD_TRANSPORT,
                BODY,
                TRANSPORT,
                PRODUCT_ROUTE,
                HARNESS,
            )
        )
    )
    binary = build_http_fixture(
        temp,
        source,
        (
            ROOT / "app/bk7258/bk7258_cloud_fixture.c",
            ROOT / "app/bk7258/bk7258_provision_store.c",
            ROOT / "app/bk7258/bk7258_preferences.c",
            CJSON / "cJSON.c",
        ),
        (
            "-DCONFIG_BK7258_AUDIO_PIPELINE_VALIDATION",
            "-DCONFIG_BK7258_PREFERENCES",
            "-DCONFIG_BK7258_PROVISION_GATT",
            "-I",
            str(CJSON),
            "-I",
            str(ROOT.parent / "external/unqlite/unqlite"),
            "-I",
            str(AGENT / "include"),
            "-I",
            str(AGENT / "src"),
            "-Wl,--gc-sections",
            "-Wl,--wrap=mkdir",
            "-Wl,--wrap=bkprov_store_open",
            "-Wl,--wrap=fsync",
            "-Wl,--wrap=bkcloud_fixture_tls_ops",
            "-lm",
        ),
    )
    raise SystemExit(subprocess.run([str(binary)]).returncode)
