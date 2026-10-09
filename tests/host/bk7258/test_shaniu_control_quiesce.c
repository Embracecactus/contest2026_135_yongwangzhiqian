/* SPDX-License-Identifier: Apache-2.0 */
/* Independent SDC1 wire peer for the real authenticated session parser. */
#include "bk7258_control_session.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static struct bkcontrol_session_s session;
static unsigned int executions, configs, otas;
static void put(uint8_t *p, uint32_t v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t get(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int execute(void *c, enum bkcontrol_command_e cmd, uint32_t v, struct bkcontrol_status_s *s)
{ (void)c; (void)cmd; (void)v; (void)s; executions++; return 0; }
static int config(void *c, enum bkcontrol_command_e cmd, uint32_t kind, uint32_t off,
                  const uint8_t *record, size_t size, struct bkcontrol_status_s *s)
{ (void)c; (void)cmd; (void)kind; (void)off; (void)record; (void)size; (void)s; configs++; return 0; }
static int ota(void *c, enum bkcontrol_command_e cmd, const uint8_t *p, size_t n, struct bkcontrol_status_s *s)
{ (void)c; (void)cmd; (void)p; (void)n; (void)s; otas++; return 0; }
static uint32_t seq;
static int send(enum bkcontrol_command_e command, const void *data, size_t n)
{
  uint8_t request[80] = {0}, response[BKCONTROL_RESPONSE_SIZE];
  assert(n <= sizeof(request) - 16);
  memcpy(request, "SDC1", 4); put(request + 4, command);
  put(request + 8, seq); put(request + 12, n);
  if (n) memcpy(request + 16, data, n);
  int ret = bkcontrol_session_packet(&session, request, 16 + n, response);
  if (ret) return ret;
  assert(get(response + 8) == seq++);
  assert(get(response + 4) == ((uint32_t)command | 0x80000000u));
  return (int32_t)get(response + 16);
}
static void start(void)
{
  uint8_t key[32]; memset(key, 0x42, sizeof(key));
  assert(bkcontrol_session_open(&session, key, execute, NULL) == 0);
  assert(bkcontrol_session_set_config_handler(&session, config) == 0);
  assert(bkcontrol_session_set_ota_handler(&session, ota) == 0);
  assert(bkcontrol_session_quiesce(&session) == -EACCES);
  seq = 0;
  assert(send(BKCONTROL_AUTH, key, sizeof(key)) == 0);
}
int main(int argc, char **argv)
{
  assert(argc == 2);
  start();
  uint8_t payload[8] = {0};
  if (!strcmp(argv[1], "queries"))
    {
      assert(bkcontrol_session_quiesce(&session) == 0);
      assert(bkcontrol_session_quiesce(&session) == 0);
      assert(send(BKCONTROL_STATUS, NULL, 0) == 0);
      put(payload, 50);
      assert(send(BKCONTROL_VOLUME, payload, 4) == -EBUSY);
      assert(executions == 1);
      assert(send(BKCONTROL_CANCEL, NULL, 0) == 0);
      assert(send(BKCONTROL_INFO, NULL, 0) == 0);
      assert(executions == 3);
      put(payload, BKCONTROL_CONFIG_SETTINGS << 16);
      assert(send(BKCONTROL_CONFIG_READ, payload, 4) == 0);
      put(payload, BKCONTROL_CONFIG_WIFI_SCAN << 16);
      assert(send(BKCONTROL_CONFIG_READ, payload, 4) == -EBUSY);
      assert(configs == 1);
    }
  else if (!strcmp(argv[1], "staging"))
    {
      put(payload, 44);
      assert(send(BKCONTROL_OTA_BEGIN, payload, 4) == 0);
      assert(bkcontrol_session_quiesce(&session) == 0);
      assert(send(BKCONTROL_OTA_APPEND, payload, 4) == -EBUSY);
      assert(session.ota_received == 0);
      assert(send(BKCONTROL_OTA_STATUS, NULL, 0) == 0);
      assert(send(BKCONTROL_OTA_CANCEL, NULL, 0) == 0);
      assert(otas == 2 && session.ota_total == 0);
      put(payload, BKCONTROL_CONFIG_SETTINGS); put(payload + 4, 12);
      assert(send(BKCONTROL_CONFIG_BEGIN, payload, 8) == -EBUSY);
      assert(configs == 0 && session.ota_total == 0);
    }
  else if (!strcmp(argv[1], "config-staging"))
    {
      put(payload, BKCONTROL_CONFIG_SETTINGS); put(payload + 4, 8);
      assert(send(BKCONTROL_CONFIG_BEGIN, payload, 8) == 0);
      assert(send(BKCONTROL_CONFIG_APPEND, payload, 4) == 0);
      assert(bkcontrol_session_quiesce(&session) == 0);
      assert(send(BKCONTROL_CONFIG_APPEND, payload, 4) == -EBUSY);
      assert(configs == 1 && session.ota_received == 4);
      assert(send(BKCONTROL_CONFIG_CANCEL, NULL, 0) == 0);
      bkcontrol_session_close(&session);
      start();
      assert(send(BKCONTROL_CONFIG_BEGIN, payload, 8) == 0);
      assert(send(BKCONTROL_CONFIG_APPEND, payload, 8) == 0);
      assert(bkcontrol_session_quiesce(&session) == 0);
      assert(send(BKCONTROL_CONFIG_APPLY, NULL, 0) == -EBUSY);
      assert(configs == 2);
      assert(send(BKCONTROL_CONFIG_CANCEL, NULL, 0) == 0);
    }
  else if (!strcmp(argv[1], "ota-commit"))
    {
      uint8_t record[32] = {0};
      put(payload, 44);
      assert(send(BKCONTROL_OTA_BEGIN, payload, 4) == 0);
      assert(send(BKCONTROL_OTA_APPEND, record, 32) == 0);
      assert(send(BKCONTROL_OTA_APPEND, record, 12) == 0);
      assert(bkcontrol_session_quiesce(&session) == 0);
      assert(send(BKCONTROL_OTA_START, NULL, 0) == -EBUSY);
      assert(otas == 0);
      assert(send(BKCONTROL_OTA_CANCEL, NULL, 0) == 0);
      assert(send(BKCONTROL_OTA_BEGIN, payload, 4) == -EBUSY);
      assert(session.ota_total == 0);
    }
  else
    {
      assert(!strcmp(argv[1], "invalid"));
      assert(bkcontrol_session_quiesce(&session) == 0);
      put(payload, 101);
      assert(send(BKCONTROL_VOLUME, payload, 4) == -EPROTO);
      assert(!session.open && executions == 0);
      start();
      assert(bkcontrol_session_quiesce(&session) == 0);
      seq++;
      assert(send(BKCONTROL_STATUS, NULL, 0) == -EPROTO);
      assert(!session.open && executions == 0);
    }
  bkcontrol_session_close(&session);
  puts("CONTRACT_PASS");
  return 0;
}
