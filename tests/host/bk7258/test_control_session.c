/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_control_session.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static int operation_error;
static int ota_error;
static unsigned ota_calls;
static enum bkcontrol_command_e ota_command;
static const uint8_t *ota_record_pointer;
static size_t ota_record_size;
static uint8_t ota_record_copy[3371];
static int ota(void *context, enum bkcontrol_command_e command,
               const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  (void)context;
  ota_calls++;
  ota_command = command;
  ota_record_pointer = record;
  ota_record_size = size;
  if (command == BKCONTROL_OTA_START)
    {
      assert(record != NULL && size >= 44 && size <= sizeof(ota_record_copy));
      memcpy(ota_record_copy, record, size);
    }
  else
    {
      assert(record == NULL && size == 0);
    }
  status->ota = (struct bkcontrol_ota_status_s){3, 4, 55, 999,
                                               -EINPROGRESS};
  return ota_error;
}
static int execute(void *context, enum bkcontrol_command_e command,
                   uint32_t value, struct bkcontrol_status_s *status)
{
  assert(context == &calls);
  assert(command >= BKCONTROL_STATUS && command <= BKCONTROL_INFO);
  calls++;
  if (command == BKCONTROL_INFO)
    {
      status->device_info = (struct bkcontrol_device_info_s){1, 2, 3, 4, 5};
      return operation_error;
    }
  status->flags = 8;
  status->volume = value;
  return operation_error;
}
static void put(uint8_t *p, uint32_t n)
{ p[0] = n>>24; p[1] = n>>16; p[2] = n>>8; p[3] = n; }
static uint32_t get(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void frame(uint8_t p[48], uint32_t command, uint32_t seq, uint32_t size)
{
  memset(p, 0, 48); memcpy(p, "SDC1", 4);
  put(p+4, command); put(p+8, seq); put(p+12, size);
}
static void authenticate(struct bkcontrol_session_s *s)
{
  uint8_t p[48], response[40], key[32]; memset(key, 42, 32);
  assert(bkcontrol_session_open(s, key, execute, &calls) == 0);
  frame(p, 1, 0, 32); memcpy(p+16, key, 32);
  assert(bkcontrol_session_packet(s, p, 48, response) == 0);
  assert(s->authenticated && get(response+4) == 0x80000001u);
  for (unsigned i = 0; i<32; i++) assert(s->secret[i] == 0);
}
static void authenticate_ota(struct bkcontrol_session_s *s)
{
  uint8_t p[48], response[40], key[32];
  memset(key, 42, sizeof(key));
  assert(bkcontrol_session_open(s, key, execute, &calls) == 0);
  assert(bkcontrol_session_set_ota_handler(s, ota) == 0);
  frame(p, BKCONTROL_AUTH, 0, 32);
  memcpy(p + 16, key, 32);
  assert(bkcontrol_session_packet(s, p, sizeof(p), response) == 0);
  assert(s->authenticated);
}
static bool bytes_zero(const void *data, size_t size)
{
  const uint8_t *bytes = data;
  for (size_t index = 0; index < size; index++)
    if (bytes[index] != 0) return false;
  return true;
}
static void expect_unknown_ota(const uint8_t response[40])
{
  for (size_t offset = 20; offset < 40; offset += 4)
    assert(get(response + offset) == UINT32_MAX);
}
static void append(struct bkcontrol_session_s *s, uint32_t sequence,
                   const uint8_t *data, uint32_t size, uint8_t response[40])
{
  uint8_t p[48];
  assert(size <= 32);
  frame(p, BKCONTROL_OTA_APPEND, sequence, size);
  memcpy(p + 16, data, size);
  assert(bkcontrol_session_packet(s, p, 16 + size, response) == 0);
}

static void test_ota_transport(void)
{
  struct bkcontrol_session_s s = {0};
  uint8_t p[48], response[40], key[32], record[44];
  unsigned before;

  memset(key, 42, sizeof(key));
  for (size_t index = 0; index < sizeof(record); index++)
    record[index] = (uint8_t)(index + 1);

  /* OTA is authenticated-only, and a missing optional handler is reported. */
  assert(bkcontrol_session_open(&s, key, execute, &calls) == 0);
  assert(bkcontrol_session_set_ota_handler(&s, ota) == 0);
  frame(p, BKCONTROL_OTA_BEGIN, 0, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == -EPROTO);
  assert(bytes_zero(&s, sizeof(s)));

  authenticate(&s);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  assert((int32_t)get(response + 16) == -ENOTSUP && s.open);
  expect_unknown_ota(response);
  frame(p, BKCONTROL_OTA_STATUS, 2, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert((int32_t)get(response + 16) == -ENOTSUP && s.open);
  expect_unknown_ota(response);
  bkcontrol_session_close(&s);

  /* BEGIN accepts both record limits and rejects values outside them. */
  for (uint32_t total = 44; total <= 3371; total += 3371 - 44)
    {
      authenticate_ota(&s);
      frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
      put(p + 16, total);
      before = ota_calls;
      assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
      assert((int32_t)get(response + 16) == 0 && s.ota_total == total);
      assert(ota_calls == before);
      expect_unknown_ota(response);
      frame(p, BKCONTROL_OTA_CANCEL, 2, 0);
      assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
      bkcontrol_session_close(&s);
    }
  for (uint32_t total = 43; total <= 3372; total += 3372 - 43)
    {
      authenticate_ota(&s);
      frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
      put(p + 16, total);
      assert(bkcontrol_session_packet(&s, p, 20, response) == -EPROTO);
      assert(bytes_zero(&s, sizeof(s)));
    }

  /* A second BEGIN reports busy while preserving the first upload. */
  authenticate_ota(&s);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  frame(p, BKCONTROL_OTA_BEGIN, 2, 4);
  put(p + 16, 3371);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  assert((int32_t)get(response + 16) == -EBUSY && s.ota_total == 44 &&
         s.ota_received == 0);
  expect_unknown_ota(response);
  bkcontrol_session_close(&s);

  /* APPEND accepts 1..32 bytes, preserves ordering, and rejects overflow. */
  authenticate_ota(&s);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  append(&s, 2, record, 1, response);
  expect_unknown_ota(response);
  append(&s, 3, record + 1, 32, response);
  append(&s, 4, record + 33, 11, response);
  assert(s.ota_received == sizeof(record) &&
         memcmp(s.ota_record, record, sizeof(record)) == 0);
  frame(p, BKCONTROL_OTA_APPEND, 5, 1);
  p[16] = 0xaa;
  assert(bkcontrol_session_packet(&s, p, 17, response) == -EPROTO);
  assert(bytes_zero(&s, sizeof(s)));

  /* START before the exact byte count is a terminal protocol error. */
  authenticate_ota(&s);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  append(&s, 2, record, 32, response);
  frame(p, BKCONTROL_OTA_START, 3, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == -EPROTO);
  assert(bytes_zero(&s, sizeof(s)));

  /* A replay during upload closes and wipes all buffered request material. */
  authenticate_ota(&s);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  append(&s, 2, record, 1, response);
  frame(p, BKCONTROL_OTA_APPEND, 2, 1);
  p[16] = 0xbb;
  assert(bkcontrol_session_packet(&s, p, 17, response) == -EPROTO);
  assert(bytes_zero(&s, sizeof(s)));

  /* Rejected START retains a complete upload; accepted START copies during
   * the callback and wipes the borrowed backing record before returning.
   */
  authenticate_ota(&s);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  append(&s, 2, record, 32, response);
  append(&s, 3, record + 32, 12, response);
  ota_error = -EAGAIN;
  before = ota_calls;
  frame(p, BKCONTROL_OTA_START, 4, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert(ota_calls == before + 1 && ota_command == BKCONTROL_OTA_START);
  assert(ota_record_pointer == s.ota_record && ota_record_size == sizeof(record));
  assert((int32_t)get(response + 16) == -EAGAIN && get(response + 20) == 3 &&
         get(response + 24) == 4 && get(response + 28) == 55 &&
         get(response + 32) == 999 && (int32_t)get(response + 36) == -EINPROGRESS);
  assert(s.ota_total == sizeof(record) && s.ota_received == sizeof(record));
  assert(memcmp(s.ota_record, record, sizeof(record)) == 0);
  ota_error = 0;
  frame(p, BKCONTROL_OTA_START, 5, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert(memcmp(ota_record_copy, record, sizeof(record)) == 0);
  assert(s.ota_total == 0 && s.ota_received == 0 &&
         bytes_zero(s.ota_record, sizeof(s.ota_record)));

  /* Runtime STATUS/CANCEL have no record argument; successful CANCEL also
   * discards an in-progress upload.
   */
  frame(p, BKCONTROL_OTA_STATUS, 6, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert(ota_command == BKCONTROL_OTA_STATUS && ota_record_pointer == NULL &&
         ota_record_size == 0 && get(response + 28) == 55);
  frame(p, BKCONTROL_OTA_BEGIN, 7, 4);
  put(p + 16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  append(&s, 8, record, 1, response);
  frame(p, BKCONTROL_OTA_CANCEL, 9, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert(ota_command == BKCONTROL_OTA_CANCEL && ota_record_pointer == NULL &&
         ota_record_size == 0 && s.ota_total == 0 && s.ota_received == 0 &&
         bytes_zero(s.ota_record, sizeof(s.ota_record)));
  bkcontrol_session_close(&s);

  memset(&s, 0xa5, sizeof(s));
  bkcontrol_session_close(&s);
  assert(bytes_zero(&s, sizeof(s)));
}
/* Optional pipe endpoint consumed by Android DeviceControlInteropTest.
 * Transport is stdin/stdout; key and AP state are synthetic test fixtures.
 */
static uint32_t peer_volume = 50, peer_persona;
static bool peer_busy = true;
static bool peer_memory_enabled, peer_memory_pending;
static int peer_execute(void *context, enum bkcontrol_command_e command,
                        uint32_t value, struct bkcontrol_status_s *status)
{
  (void)context;
  if (peer_busy && (command == BKCONTROL_VOLUME || command == BKCONTROL_PERSONA))
    return -EBUSY;
  if (command == BKCONTROL_CANCEL) peer_busy = false;
  if (command == BKCONTROL_VOLUME) peer_volume = value;
  if (command == BKCONTROL_PERSONA) peer_persona = value;
  if (command == BKCONTROL_MEMORY_SET || command == BKCONTROL_MEMORY_DELETE)
    {
      peer_memory_enabled = command == BKCONTROL_MEMORY_SET && value != 0;
      peer_memory_pending = true;
    }
  else if (command == BKCONTROL_STATUS) peer_memory_pending = false;
  status->flags = 1u | 8u | 16u | 512u | 1024u | (peer_busy ? 0u : 2048u) | ((peer_busy || peer_memory_pending) ? 2u : 0u) |
      (peer_memory_pending ? 128u : 32u | (peer_memory_enabled ? 64u : 0u));
  status->volume = peer_volume;
  status->persona = peer_persona;
  return 0;
}
static int pipe_peer(void)
{
  struct bkcontrol_session_s session = {0};
  uint8_t key[32], request[48], response[40];
  memset(key, 42, sizeof(key));
  if (bkcontrol_session_open(&session, key, peer_execute, NULL) != 0) return 1;
  memset(key, 0, sizeof(key));
  for (;;)
    {
      size_t count = fread(request, 1, 16, stdin);
      if (count == 0 && feof(stdin)) break;
      if (count != 16) return 2;
      uint32_t payload = get(request + 12);
      if (payload > 32 || fread(request + 16, 1, payload, stdin) != payload) return 3;
      if (bkcontrol_session_packet(&session, request, 16 + payload, response) != 0) return 4;
      memset(request, 0, sizeof(request));
      if (fwrite(response, 1, sizeof(response), stdout) != sizeof(response) || fflush(stdout)) return 5;
      memset(response, 0, sizeof(response));
    }
  bkcontrol_session_close(&session);
  return 0;
}
int main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "--pipe-peer") == 0) return pipe_peer();
  struct bkcontrol_session_s s = {0};
  uint8_t p[48], response[40], key[32]; memset(key, 42, 32);
  /* Neither malformed AUTH nor unauthenticated commands can reach AP. */
  for (unsigned bad = 0; bad<4; bad++)
    {
      assert(bkcontrol_session_open(&s, key, execute, &calls) == 0);
      frame(p, 1, 0, 32); memcpy(p+16, key, 32);
      if (bad == 0) p[16] ^= 1;
      if (bad == 1) put(p+4, 2);
      if (bad == 2) put(p+8, 1);
      if (bad == 3) put(p+12, UINT32_MAX);
      assert(bkcontrol_session_packet(&s, p, 48, response) < 0);
      assert(!s.open && calls == 0);
      for (unsigned i = 0; i<32; i++) assert(s.secret[i] == 0);
      for (unsigned i = 0; i<40; i++) assert(response[i] == 0);
    }
  authenticate(&s);
  /* OTA remains authenticated-only and uses the existing request sequence. */
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4); put(p+16, 44);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0 && (int32_t)get(response+16) == -ENOTSUP);
  bkcontrol_session_close(&s); authenticate(&s);
  assert(bkcontrol_session_set_ota_handler(&s, ota) == -EINVAL); /* before AUTH only */
  bkcontrol_session_close(&s);
  assert(bkcontrol_session_open(&s, key, execute, &calls) == 0);
  assert(bkcontrol_session_set_ota_handler(&s, ota) == 0);
  frame(p, 1, 0, 32);memcpy(p+16, key, 32);assert(bkcontrol_session_packet(&s, p, 48, response) == 0);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);put(p+16, 43);assert(bkcontrol_session_packet(&s, p, 20, response)<0 && !s.open);
  assert(bkcontrol_session_open(&s, key, execute, &calls) == 0);assert(bkcontrol_session_set_ota_handler(&s, ota) == 0);frame(p, 1, 0, 32);memcpy(p+16, key, 32);assert(bkcontrol_session_packet(&s, p, 48, response) == 0);
  frame(p, BKCONTROL_OTA_BEGIN, 1, 4);put(p+16, 44);assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  frame(p, BKCONTROL_OTA_START, 2, 0);assert(bkcontrol_session_packet(&s, p, 16, response)<0 && !s.open);
  authenticate(&s);
  frame(p, BKCONTROL_INFO, 1, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert(calls == 1 && get(response + 4) == (0x80000000u | BKCONTROL_INFO) &&
         get(response + 20) == 1u && get(response + 36) == 5u);
  bkcontrol_session_close(&s);
  authenticate(&s);
  frame(p, BKCONTROL_INFO, 1, 4); put(p + 16, 0);
  assert(bkcontrol_session_packet(&s, p, 20, response) < 0 && !s.open);
  calls = 0;
  authenticate(&s);
  frame(p, BKCONTROL_MEMORY_SET, 1, 4); put(p + 16, 1);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0 && calls == 1);
  frame(p, BKCONTROL_MEMORY_DELETE, 2, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0 && calls == 2);
  frame(p, BKCONTROL_MEMORY_SET, 3, 4); put(p + 16, 2);
  assert(bkcontrol_session_packet(&s, p, 20, response) < 0 && calls == 2 && !s.open);
  calls = 0;
  authenticate(&s);
  frame(p, 4, 1, 4); put(p+16, 73);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  assert(calls == 1 && get(response+24) == 73 && get(response+8) == 1);
  assert(bkcontrol_session_packet(&s, p, 20, response) == -EPROTO);
  assert(calls == 1 && !s.open); /* Replayed volume never executes twice. */
  authenticate(&s);
  operation_error = -EBUSY;
  frame(p, 5, 1, 4); put(p+16, 4);
  assert(bkcontrol_session_packet(&s, p, 20, response) == 0);
  assert((int32_t)get(response+16) == -EBUSY && get(response+20) == 0);
  assert(get(response+24) == UINT32_MAX);
  operation_error = 0;
  frame(p, 6, 2, 0);
  assert(bkcontrol_session_packet(&s, p, 16, response) == 0);
  assert(calls == 3);
  bkcontrol_session_close(&s);
  for (unsigned bad = 0; bad<5; bad++)
    {
      unsigned before = calls;
      authenticate(&s);
      frame(p, 4, 1, 4); put(p+16, 101);
      if (bad == 1) { put(p+4, 5); put(p+16, 5); }
      if (bad == 2) put(p+4, 1); /* No reauthentication in a session. */
      if (bad == 3) { put(p+4, 2); put(p+12, 0); }
      if (bad == 4) p[0] = 'X';
      assert(bkcontrol_session_packet(&s, p, 20, response) < 0);
      assert(calls == before && !s.open);
    }
  test_ota_transport();
  puts("PASS: control authentication, replay rejection, OTA bounds, ownership and wipe semantics");
  return 0;
}
