/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_pc_approval.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/sha256.h>

static uint8_t record[BKPC_APPROVAL_RECORD_MAX];
static const uint8_t client[16] = {9};
static const uint8_t other[16] = {8};

static void put(uint8_t *p, uint32_t v, int n)
{
  while (n) { p[--n] = (uint8_t)v; v >>= 8; }
}

static size_t build(uint8_t id, uint32_t ttl, const char *agent,
                    const char *target, const char *operation)
{
  size_t a = strlen(agent), t = strlen(target), o = strlen(operation);
  memset(record, 0, sizeof(record));
  memcpy(record, "APR1", 4);
  memset(record + 4, id, 16);
  put(record + 20, ttl, 4);
  put(record + 24, a, 2); put(record + 26, t, 2); put(record + 28, o, 2);
  memcpy(record + 32, agent, a);
  memcpy(record + 32 + a, target, t);
  memcpy(record + 32 + a + t, operation, o);
  return 32 + a + t + o;
}

static void decision(uint8_t out[56], uint32_t value, uint8_t id,
                     const uint8_t digest[32])
{
  memcpy(out, "APD1", 4);
  put(out + 4, value, 4);
  memset(out + 8, id, 16);
  memcpy(out + 24, digest, 32);
}

/* Read the full owner view exactly as the phone does (16-byte chunks). */
static size_t owner_view(struct bkpc_approval_s *s, uint8_t *out, uint64_t now)
{
  struct bkcontrol_status_s status;
  size_t total;
  memset(&status, 0, sizeof(status));
  assert(bkpc_approval_owner_control(s, BKCONTROL_CONFIG_READ, 0, NULL, 0,
                                     &status, now) == 0);
  total = status.config_total;
  memcpy(out, status.config_chunk, 16);
  for (size_t at = 16; at < total; at += 16)
    {
      assert(bkpc_approval_owner_control(s, BKCONTROL_CONFIG_READ, at, NULL,
                                         0, &status, now) == 0);
      assert(status.config_total == total);
      memcpy(out + at, status.config_chunk,
             total - at < 16 ? total - at : 16);
    }

  return total;
}

int main(int argc, char **argv)
{
  struct bkpc_approval_s s;
  struct bkcontrol_status_s status;
  uint8_t key[32];
  uint8_t digest[32];
  uint8_t view[16 + BKPC_APPROVAL_RECORD_MAX];
  uint8_t receipt[BKPC_APPROVAL_RECEIPT_SIZE];
  uint8_t d[56];
  size_t size;

  memset(&s, 0, sizeof(s));
  memset(key, 0x42, sizeof(key));
  size = build(1, 60000, "codex", "/repo", "git push --force origin main");

  if (argc == 2 && !strcmp(argv[1], "vector"))
    {
      /* Cross-language vector consumed by the workbench Python test. */
      bkpc_approval_bind(&s, 1, 1, client);
      assert(bkpc_approval_request(&s, record, size, 0) == 0);
      mbedtls_sha256(record, size, digest, 0);
      decision(d, 1, 1, digest);
      assert(bkpc_approval_decide(&s, d, sizeof(d), 1) == 0);
      assert(bkpc_approval_receipt(&s, key, receipt, 2) == 0);
      for (size_t i = 0; i < sizeof(receipt); i++) printf("%02x", receipt[i]);
      printf("\n");
      return 0;
    }

  /* No admitted principal: nothing can be requested or decided. */
  assert(bkpc_approval_request(&s, record, size, 0) == -EACCES);
  bkpc_approval_bind(&s, 1, 1, client);

  /* Validation: TTL, lengths, controls and bidi overrides. */
  assert(bkpc_approval_request(&s, record, size - 1, 0) == -EINVAL);
  build(1, 1000, "codex", "/repo", "ls");
  assert(bkpc_approval_request(&s, record, 32 + 5 + 5 + 2, 0) == -EINVAL);
  size = build(1, 60000, "codex", "/repo", "rm -rf /tmp/x\x1b[2K");
  assert(bkpc_approval_request(&s, record, size, 0) == -EINVAL);
  size = build(1, 60000, "codex", "/repo", "rm \xe2\x80\xae" "fdp.exe");
  assert(bkpc_approval_request(&s, record, size, 0) == -EINVAL);
  size = build(1, 60000, "codex", "/repo", "echo \xc0\xaf");
  assert(bkpc_approval_request(&s, record, size, 0) == -EINVAL);
  size = build(0, 60000, "codex", "/repo", "ls");
  assert(bkpc_approval_request(&s, record, size, 0) == -EINVAL);
  assert(s.state == BKPC_APPROVAL_NONE);

  /* Accept one request with multi-line UTF-8 text. */
  size = build(1, 60000, "claude-code", "/home/u/repo",
               "git push --force origin main\n# \xe4\xb8\xad\xe6\x96\x87");
  assert(bkpc_approval_request(&s, record, size, 100) == 0);
  assert(bkpc_approval_pending(&s));
  assert(bkpc_approval_request(&s, record, size, 101) == 0); /* retry */
  mbedtls_sha256(record, size, digest, 0);
  assert(!memcmp(s.digest, digest, 32));

  /* Exclusive pending request; same ID cannot name different bytes. */
  size_t second = build(2, 60000, "codex", "/r", "ls");
  assert(bkpc_approval_request(&s, record, second, 102) == -EBUSY);
  build(1, 60000, "codex", "/r", "ls");
  assert(bkpc_approval_request(&s, record, second, 102) == -EEXIST);

  /* Alerts: once, then a reminder period; never while unavailable. */
  assert(!bkpc_approval_take_alert(&s, 103, false));
  assert(bkpc_approval_take_alert(&s, 103, true));
  assert(!bkpc_approval_take_alert(&s, 104, true));
  assert(bkpc_approval_take_alert(&s, 103 + BKPC_APPROVAL_REMIND_MS, true));
  assert((bkpc_approval_visual(&s, 100, true) >> 8) == 7);
  assert((bkpc_approval_visual(&s, 100, true) & 255) == 32);
  assert(bkpc_approval_visual(&s, 100, false) == 0);

  /* The phone sees the exact bytes and binds its decision to their hash. */
  size_t total = owner_view(&s, view, 200);
  assert(total == 16 + size && !memcmp(view, "APV1", 4));
  build(1, 60000, "claude-code", "/home/u/repo",
        "git push --force origin main\n# \xe4\xb8\xad\xe6\x96\x87");
  assert(!memcmp(view + 16, record, size));
  uint8_t phone_digest[32];
  mbedtls_sha256(view + 16, size, phone_digest, 0);

  /* PC receipt while pending carries no tag. */
  assert(bkpc_approval_receipt(&s, key, receipt, 300) == 0);
  assert(receipt[7] == BKPC_APPROVAL_PENDING);
  for (int i = 64; i < 96; i++) assert(receipt[i] == 0);

  /* Wrong ID / digest / format are rejected without consuming. */
  decision(d, 1, 3, phone_digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 400) == -ESTALE);
  phone_digest[0] ^= 1;
  decision(d, 1, 1, phone_digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 400) == -EBADMSG);
  phone_digest[0] ^= 1;
  decision(d, 3, 1, phone_digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 400) == -EINVAL);
  assert(bkpc_approval_pending(&s));

  /* Reject, idempotent retry, then no change of mind. */
  decision(d, 2, 1, phone_digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 500) == 0);
  assert(s.state == BKPC_APPROVAL_REJECTED);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 501) == 0);
  decision(d, 1, 1, phone_digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 502) == -EALREADY);
  assert(bkpc_approval_receipt(&s, key, receipt, 503) == 0);
  assert(receipt[7] == BKPC_APPROVAL_REJECTED);
  int tagged = 0;
  for (int i = 64; i < 96; i++) tagged |= receipt[i];
  assert(tagged);
  assert(bkpc_approval_receipt(&s, NULL, receipt, 503) == -ENOKEY);
  /* A terminal ID can never be reopened. */
  size = build(1, 60000, "claude-code", "/home/u/repo",
               "git push --force origin main\n# \xe4\xb8\xad\xe6\x96\x87");
  assert(bkpc_approval_request(&s, record, size, 600) == 0);
  assert(s.state == BKPC_APPROVAL_REJECTED);

  /* Expiry: decisions after the deadline fail closed. */
  size = build(4, 5000, "codex", "/repo", "npm publish");
  assert(bkpc_approval_request(&s, record, size, 1000) == 0);
  mbedtls_sha256(record, size, digest, 0);
  decision(d, 1, 4, digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 6000) == -ETIMEDOUT);
  assert(s.state == BKPC_APPROVAL_EXPIRED);
  assert(bkpc_approval_visual(&s, 6000, true) == 0);

  /* PC cancel. */
  size = build(5, 60000, "codex", "/repo", "terraform apply");
  assert(bkpc_approval_request(&s, record, size, 7000) == 0);
  uint8_t cancel[20] = "APC1";
  memset(cancel + 4, 6, 16);
  assert(bkpc_approval_request(&s, cancel, sizeof(cancel), 7001) == -ENOENT);
  memset(cancel + 4, 5, 16);
  assert(bkpc_approval_request(&s, cancel, sizeof(cancel), 7001) == 0);
  assert(s.state == BKPC_APPROVAL_CANCELED);
  mbedtls_sha256(record, size, digest, 0);
  decision(d, 1, 5, digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 7002) == -EALREADY);

  /* Approve, then a changed PC principal wipes the receipt. */
  size = build(7, 60000, "codex", "/repo", "kubectl delete ns prod");
  assert(bkpc_approval_request(&s, record, size, 8000) == 0);
  mbedtls_sha256(record, size, digest, 0);
  decision(d, 1, 7, digest);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 8001) == 0);
  assert(s.state == BKPC_APPROVAL_APPROVED);
  bkpc_approval_bind(&s, 1, 1, other);
  assert(s.state == BKPC_APPROVAL_NONE && s.size == 0);
  assert(bkpc_approval_decide(&s, d, sizeof(d), 8002) == -ENOENT);

  /* Losing admission cancels a pending request. */
  assert(bkpc_approval_request(&s, record, size, 9000) == 0);
  bkpc_approval_cancel(&s);
  assert(s.state == BKPC_APPROVAL_CANCELED);
  bkpc_approval_bind(&s, 0, 0, NULL);
  assert(bkpc_approval_request(&s, record, size, 9001) == -EACCES);

  /* PC adapter: BEGIN sizes and 16-byte receipt reads. */
  bkpc_approval_bind(&s, 2, 2, client);
  assert(bkpc_approval_pc_control(&s, BKCONTROL_CONFIG_BEGIN, 0, NULL, 56,
                                  NULL, key, 0) == 0);
  assert(bkpc_approval_pc_control(&s, BKCONTROL_CONFIG_BEGIN, 0, NULL,
                                  BKPC_APPROVAL_RECORD_MAX + 1, NULL, key,
                                  0) == -EINVAL);
  assert(bkpc_approval_owner_control(&s, BKCONTROL_CONFIG_BEGIN, 0, NULL, 20,
                                     NULL, 0) == -EINVAL);
  memset(&status, 0, sizeof(status));
  assert(bkpc_approval_pc_control(&s, BKCONTROL_CONFIG_READ, 80, NULL, 0,
                                  &status, key, 0) == 0);
  assert(status.config_total == 96);
  assert(bkpc_approval_pc_control(&s, BKCONTROL_CONFIG_READ, 96, NULL, 0,
                                  &status, key, 0) == -EINVAL);
  printf("test_pc_approval PASS\n");
  return 0;
}
