/* SPDX-License-Identifier: Apache-2.0 */
/* CFG-01/03: real patch merger, durable storage, decoder and HTTP writer.
 * TLS socket I/O is external: the peer checks bytes in memory, not TLS itself.
 * No real device credentials or networking are used.
 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_config.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include "bk7258_cloud_http.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>

/* Inject only the external durability syscall, never the storage worker. */
static atomic_int sync_fault;
static atomic_int sync_failures;
int __real_rename(const char *from, const char *to);
int __wrap_rename(const char *from, const char *to)
{
  if (atomic_load(&sync_fault) == 3)
    {
      atomic_fetch_add(&sync_failures, 1);
      errno = EIO;
      return -1;
    }
  return __real_rename(from, to);
}
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  struct stat st;
  assert(fstat(fd, &st) == 0);
  int fault = atomic_load(&sync_fault);
  if ((fault == 1 && S_ISREG(st.st_mode)) ||
      (fault == 2 && S_ISDIR(st.st_mode)))
    {
      atomic_fetch_add(&sync_failures, 1);
      errno = EIO;
      return -1;
    }
  return __real_fsync(fd);
}

static void put16(uint8_t *p, size_t n) { p[0] = n >> 8; p[1] = n; }
static void put64(uint8_t *p, uint64_t n)
{ for (int i = 7; i >= 0; i--) { p[i] = n; n >>= 8; } }
static void tick(void)
{ const struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static int wait_loaded(uint8_t *record, size_t *size, uint64_t *revision)
{
  uint8_t tx[16];
  int ret = -EAGAIN;
  for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_snapshot(record, 9216, size, revision, tx); if (ret == -EAGAIN) tick(); }
  return ret;
}
static int wait_commit(uint64_t rev, uint8_t *tx, uint8_t *record, size_t size)
{
  int ret = -EAGAIN;
  for (int i = 0; i < 3000 && ret == -EAGAIN; i++)
    { ret = bkprov_storage_commit(rev, tx, record, size); if (ret == -EAGAIN) tick(); }
  return ret;
}

struct peer_s { char sent[2048]; size_t count; size_t received; int failure; };
static int peer_open(void *arg, const char *host, uint16_t port, uint64_t due)
{
  struct peer_s *p = arg;
  (void)due;
  assert(!strcmp(host, "cloud.example") && port == 443);
  return p->failure;
}
static ssize_t peer_send(void *arg, const uint8_t *bytes, size_t size, uint64_t due)
{
  struct peer_s *p = arg;
  (void)due;
  if (size > 7) size = 7;
  assert(p->count + size < sizeof(p->sent));
  memcpy(p->sent + p->count, bytes, size); p->count += size;
  return size;
}
static ssize_t peer_recv(void *arg, uint8_t *bytes, size_t size, uint64_t due)
{
  static const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
  struct peer_s *p = arg;
  size_t left = sizeof(response) - 1 - p->received;
  (void)due;
  if (size > left) size = left;
  if (size > 3) size = 3;
  memcpy(bytes, response + p->received, size); p->received += size;
  return size;
}
static int peer_close(void *arg) { (void)arg; return 0; }
static int body(void *buf, size_t *size, const void **data, size_t requested, void *arg)
{ (void)arg; assert(requested == 2 && *size >= 2); memcpy(buf, "{}", 2); *size = 2; *data = buf; return 0; }
static void request(const struct bkcloud_config_s *cloud, int failure, bool replaced)
{
  const struct bkvoice_wss_tls_ops_s tls = {.open_verified = peer_open,
    .send = peer_send, .recv = peer_recv, .close = peer_close};
  struct peer_s peer = {.failure = failure};
  struct bkcloud_http_s *http = calloc(1, sizeof(*http));
  char response[32];
  assert(http);
  int ret = bkcloud_http_post(http, cloud, "chat/completions", &tls,
    &peer, 123456, body, NULL, 2, response, sizeof(response));
  assert(ret == failure);
  if (!failure)
    {
      assert(strstr(peer.sent, "POST /v1/chat/completions HTTP/1.1\r\n"));
      assert(strstr(peer.sent, replaced ? "Authorization: Bearer fixture-key-Z\r\n" :
                    "Authorization: Bearer fixture-key-K\r\n"));
      assert(!strcmp(response, "{}"));
    }
  memset(&peer, 0, sizeof(peer)); free(http);
}
int main(int argc, char **argv)
{
  assert(argc == 5); /* scenario, private root, public DER, independent golden */
  uint8_t cert[4096], cloud_record[256] = {0}, old[9216], saved[9216];
  uint8_t owner[32] = {0x73}, tx[16] = {1};
  size_t size = 0, n;
  uint64_t revision = 0;
  FILE *file = fopen(argv[3], "rb"); assert(file);
  size_t cert_size = fread(cert, 1, sizeof(cert), file);
  assert(cert_size > 0 && cert_size < sizeof(cert)); assert(fclose(file) == 0);
  /* CCF1 public layout; literal fixture fields are independent of the encoder. */
  const char *fields[] = {"cloud.example", "/v1", "fixture-key-K", "asr", "chat", "tts"};
  memcpy(cloud_record, "CCF1", 4); cloud_record[4] = 2;
  put16(cloud_record + 6, 443); n = 24;
  for (int i = 0; i < 6; i++)
    { size_t len = strlen(fields[i]); put16(cloud_record + 8 + 2 * i, len); memcpy(cloud_record + n, fields[i], len); n += len; }
  struct bkprov_settings_s original = {.ssid = "network-A", .password = "password-A",
    .host = "cloud.example", .address = {192, 0, 2, 1}, .port = 443,
    .utc = 1800000000, .ca = cert, .ca_size = cert_size, .cloud = cloud_record,
    .cloud_size = n, .control_key = owner, .deferred = true};
  assert(bkprov_settings_encode(&original, old, sizeof(old), &size) == 0);
  assert(bkprov_storage_start(argv[2]) == 0);
  if (!strncmp(argv[1], "restart-", 8))
    {
      bool newer = !strcmp(argv[1], "restart-new");
      assert(newer || !strcmp(argv[1], "restart-old"));
      assert(wait_loaded(saved, &size, &revision) == 0);
      assert(revision == (newer ? 2u : 1u));
      struct bkprov_settings_s recovered;
      struct bkcloud_config_s retained;
      assert(bkprov_settings_decode(&recovered, saved, size) == 0);
      assert(!strcmp(recovered.ssid, newer ? "network-B" : "network-A"));
      assert(!memcmp(recovered.control_key, owner, 32));
      assert(recovered.ca_size == cert_size && !memcmp(recovered.ca, cert, cert_size));
      assert(bkcloud_config_decode(&retained, recovered.cloud, recovered.cloud_size) == 0);
      request(&retained, 0, false);
      struct bkcontrol_status_s status = {0};
      assert(!bkprov_config_busy());
      assert(bkprov_config_control(BKCONTROL_CONFIG_READ, 0, NULL, 0, &status) == 0);
      assert(status.config_chunk[7] == 2); /* Public stored state. */
      assert(bkprov_storage_stop() == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  size_t ignored;
  assert(wait_loaded(saved, &ignored, &revision) == -ENOENT);
  assert(wait_commit(0, tx, old, size) == 0);
  /* Golden SCP1 Wi-Fi-only patch: op=02 followed by zeros, revision=1,
   * flags=9, UTC=1800000000, two 9/10-byte fields, cloud/CA/address absent. */
  uint8_t patch[512] = {0};
  size_t patch_size = 71;
  file = fopen(argv[4], "r"); assert(file);
  for (size_t i = 0; i < patch_size; i++)
    { unsigned int byte; assert(fscanf(file, "%2x", &byte) == 1); patch[i] = byte; }
  unsigned int extra;
  assert(fscanf(file, "%2x", &extra) == EOF && fclose(file) == 0);
  bool cloud_edit = !strcmp(argv[1], "keep-key") || !strcmp(argv[1], "replace-key") ||
                    !strcmp(argv[1], "cross-host-retain") || !strcmp(argv[1], "clear-cloud");
  bool replaced = !strcmp(argv[1], "replace-key");
  bool cleared = !strcmp(argv[1], "clear-cloud");
  bool rejected = !strcmp(argv[1], "stale") || !strcmp(argv[1], "conflict") ||
                  !strcmp(argv[1], "cross-host-retain");
  if (cloud_edit)
    {
      memset(patch + 40, 0, sizeof(patch) - 40);
      patch[31] = cleared ? 16 : replaced ? 6 : 2;
      patch_size = 52;
      if (!cleared)
        {
          if (replaced) memcpy(cloud_record + 40, "fixture-key-Z", 13);
          else
            {
              memmove(cloud_record + 40, cloud_record + 53, n - 53);
              n -= 13; put16(cloud_record + 12, 0);
            }
          if (!strcmp(argv[1], "cross-host-retain"))
            memcpy(cloud_record + 24, "other.example", 13);
          memcpy(patch + 52, cloud_record, n); put16(patch + 44, n);
          patch_size += n;
        }
    }
  struct bkcontrol_status_s status = {0};
  bool rename_failure = !strcmp(argv[1], "rename-unknown");
  bool sync_case = strstr(argv[1], "sync-") != NULL || rename_failure;
  if (sync_case)
    {
      bool uncertain = !strcmp(argv[1], "directory-sync-unknown") || rename_failure;
      atomic_store(&sync_fault, rename_failure ? 3 : uncertain ? 2 : 1);
      assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == 0);
      /* The real worker publishes a receipt; poll its public completion. */
      int outcome = -EAGAIN;
      for (int i = 0; i < 3000 && outcome == -EAGAIN; i++)
        { outcome = bkprov_storage_receipt(patch + 4); tick(); }
      assert(atomic_load(&sync_failures) == 1);
      for (int i = 0; i < 20; i++) bkprov_config_step();
      atomic_store(&sync_fault, 0);
      if (uncertain)
        {
          assert(outcome == -EINPROGRESS);
          assert(bkprov_config_busy());
          assert(bkprov_config_control(BKCONTROL_CONFIG_READ, 0, NULL, 0, &status) == -EINPROGRESS);
          assert(bkprov_storage_refresh() == -EINPROGRESS);
          assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == -EBUSY);
          /* Failure remains unknown; same-mount readback is not durability. */
          puts("CONTRACT_PASS");
          return 0;
        }
      assert(outcome == -EINPROGRESS && !bkprov_config_busy());
      assert(bkprov_config_control(BKCONTROL_CONFIG_READ, 0, NULL, 0, &status) == 0);
      assert(status.config_chunk[7] == 3); /* Public SCS1 EDIT_FAILED. */
      assert(bkprov_config_control(BKCONTROL_CONFIG_READ, 32, NULL, 0, &status) == 0);
      uint32_t failure = ((uint32_t)status.config_chunk[0] << 24) |
                         ((uint32_t)status.config_chunk[1] << 16) |
                         ((uint32_t)status.config_chunk[2] << 8) | status.config_chunk[3];
      assert((int32_t)failure == -EIO);
      assert(wait_loaded(saved, &size, &revision) == 0 && revision == 1);
      assert(size > 0 && !memcmp(saved, old, size));
      assert(bkprov_storage_stop() == 0);
      assert(bkprov_storage_start(argv[2]) == 0);
      assert(wait_loaded(saved, &size, &revision) == 0 && revision == 1);
      assert(!memcmp(saved, old, size));
      if (!strcmp(argv[1], "file-sync-retry"))
        {
          patch[4] = 3; /* Explicit new operation, same saved revision. */
          assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == 0);
          for (int i = 0; i < 3000 && bkprov_config_busy(); i++)
            { bkprov_config_step(); tick(); }
          assert(!bkprov_config_busy());
          assert(wait_loaded(saved, &size, &revision) == 0 && revision == 2);
        }
      struct bkprov_settings_s recovered;
      struct bkcloud_config_s retained;
      assert(bkprov_settings_decode(&recovered, saved, size) == 0);
      assert(!strcmp(recovered.ssid, revision == 1 ? "network-A" : "network-B"));
      assert(!memcmp(recovered.control_key, owner, 32));
      assert(bkcloud_config_decode(&retained, recovered.cloud, recovered.cloud_size) == 0);
      request(&retained, 0, false);
      bkcloud_config_clear(&retained);
      assert(bkprov_storage_stop() == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "stale"))
    {
      put64(patch + 20, 0);
      assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == -ESTALE);
    }
  else if (!strcmp(argv[1], "conflict"))
    {
      patch[31] = 2 | 16;
      assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == -EBADMSG);
    }
  else if (!strcmp(argv[1], "cross-host-retain"))
    {
      assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == -EACCES);
    }
  else
    {
      assert(!strcmp(argv[1], "wifi-reopen") || cloud_edit);
      assert(bkprov_config_control(BKCONTROL_CONFIG_APPLY, 0, patch, patch_size, &status) == 0);
      for (int i = 0; i < 3000 && bkprov_config_busy(); i++)
        { bkprov_config_step(); tick(); }
      assert(!bkprov_config_busy());
    }
  assert(wait_loaded(saved, &size, &revision) == 0);
  struct bkprov_settings_s actual;
  struct bkcloud_config_s cloud;
  assert(bkprov_settings_decode(&actual, saved, size) == 0);
  if (!cleared)
    {
      assert(bkcloud_config_decode(&cloud, actual.cloud, actual.cloud_size) == 0);
      request(&cloud, -ECONNREFUSED, replaced); /* External failure, not Wi-Fi hardware. */
    }
  assert(bkprov_storage_stop() == 0);
  assert(bkprov_storage_start(argv[2]) == 0);
  assert(wait_loaded(saved, &size, &revision) == 0);
  assert(bkprov_settings_decode(&actual, saved, size) == 0);
  assert(!strcmp(actual.ssid, !strcmp(argv[1], "wifi-reopen") ? "network-B" : "network-A"));
  assert(revision == (rejected ? 1u : 2u));
  assert(!memcmp(actual.control_key, owner, 32));
  if (cleared)
    {
      assert(actual.cloud_size == 0 && actual.ca_size == 0 && actual.host[0] == 0);
      assert(actual.port == 0); /* Only explicit clear removes the endpoint. */
    }
  else
    {
      assert(actual.ca_size == cert_size && !memcmp(actual.ca, cert, cert_size));
      assert(bkcloud_config_decode(&cloud, actual.cloud, actual.cloud_size) == 0);
      assert(!strcmp(cloud.asr_model, "asr") && !strcmp(cloud.chat_model, "chat") && !strcmp(cloud.tts_model, "tts"));
      request(&cloud, 0, replaced);
    }
  /* Read-only queries must not create another stored revision. */
  for (int i = 0; i < 20; i++)
    assert(bkprov_config_control(BKCONTROL_CONFIG_READ, 0, NULL, 0, &status) == 0);
  uint64_t after; assert(wait_loaded(saved, &size, &after) == 0 && after == revision);
  assert(bkprov_storage_stop() == 0);
  memset(saved, 0, sizeof(saved)); memset(old, 0, sizeof(old));
  bkcloud_config_clear(&cloud);
  puts("CONTRACT_PASS");
  return 0;
}
