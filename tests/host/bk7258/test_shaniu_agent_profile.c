/* SPDX-License-Identifier: Apache-2.0 */
#define CONFIG_BK7258_PREFERENCES 1
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mbedtls/platform_util.h>
#include "cJSON.h"
#include "bk7258_control_session.h"
#include "bk7258_reply_expression.h"
#include "bk7258_runtime_skills.h"
#include "core/message_bus.h"

static atomic_bool g_local_text_done;
static atomic_int g_active_persona;
static uint64_t request = 1;
static bool idle;
static int extracts;
static int fact_error;
static int applies;
static int commits;
static int history_calls;
static int stream_result;
static uint64_t voice_channel_request_id(void) { return request; }
static bool voice_channel_is_idle(void) { return idle; }
static const char facts[] =
  "{\"version\":2,\"revision\":\"7\",\"enabled\":true,"
  "\"facts\":[{\"key\":\"name\",\"value\":\"test fact\"}]}";
static int bkagent_memory_facts_read(char *out, size_t size)
{
  assert(size > sizeof(facts));
  strcpy(out, facts);
  return fact_error;
}
static int bkagent_memory_facts_context(char *out, size_t size)
{
  return bkagent_memory_facts_read(out, size);
}
static int bkagent_memory_facts_extract_user(const char *text)
{
  assert(!strcmp(text, "记住我叫测试"));
  extracts++;
  return 0;
}
static int bkagent_memory_commit(unsigned int persona, const char *text)
{
  assert(persona == 0 && text == NULL);
  commits++;
  return fact_error;
}
static int bkagent_memory_facts_apply(const char *text)
{
  assert(!strcmp(text, "{}"));
  applies++;
  return 0;
}
#include "bk7258_agent_profile.inc"

static int status(uint64_t id) { return id == request ? 0 : -ESTALE; }
static int stream_finish(uint64_t id, int event, const char *text, size_t size)
{
  assert(id == request && event == AGENT_REPLY_END && !text && !size);
  return stream_result;
}
static void history(const agent_msg_t *message, const char *text)
{
  assert(message->request_id == request && !strcmp(text, "reply"));
  history_calls++;
}
static void complete(uint64_t id, int result)
{
  if (!result) assert(history_calls > 0);
  product_profile_complete(id, result == 0);
}

static int execute(void *context, enum bkcontrol_command_e command,
                   uint32_t value, struct bkcontrol_status_s *reply)
{
  (void)context;
  (void)command;
  (void)value;
  (void)reply;
  return 0;
}
static int config(void *context, enum bkcontrol_command_e command,
                  uint32_t kind, uint32_t offset, const uint8_t *record,
                  size_t size, struct bkcontrol_status_s *reply)
{
  (void)context;
  return product_agent_data(command, kind, offset, record, size, reply);
}
static void put32(uint8_t *bytes, uint32_t value)
{
  for (int i = 3; i >= 0; i--) { bytes[i] = value; value >>= 8; }
}
static int packet(struct bkcontrol_session_s *session, uint32_t sequence,
                  enum bkcontrol_command_e command, const uint8_t *bytes,
                  size_t size)
{
  uint8_t input[80] = {0};
  uint8_t response[BKCONTROL_RESPONSE_SIZE];
  memcpy(input, "SDC1", 4);
  put32(input + 4, command);
  put32(input + 8, sequence);
  put32(input + 12, size);
  if (size) memcpy(input + 16, bytes, size);
  int ret = bkcontrol_session_packet(session, input, 16 + size, response);
  if (ret) return ret;
  return (int32_t)((uint32_t)response[16] << 24 |
    (uint32_t)response[17] << 16 | (uint32_t)response[18] << 8 | response[19]);
}

/* This fixture checks product metadata + the real Agent delivery owner.
 * Durable storage is exercised by run-runtime-skills and run-memory-facts.
 */
int bkruntime_skills_store_load(void *buffer, size_t capacity, size_t *actual,
                               uint64_t *revision)
{
  (void)buffer;
  (void)capacity;
  *actual = 0;
  *revision = 0;
  return 0;
}
int bkruntime_skills_store_commit(uint64_t expected, const void *buffer,
                                 size_t size, uint64_t *revision)
{
  (void)expected;
  (void)buffer;
  (void)size;
  (void)revision;
  assert(!"profile fixture must not install Skills");
  return -ENOTSUP;
}

int main(void)
{
  struct bkreply_expression_ops_s ops = {0};
  struct bkcontrol_status_s reply = {0};
  char assembled[512] = {0};
  char output[128];
  cJSON *root;
  cJSON *args;
  assert(bk7258_runtime_skills_init() == 0);
  bkreply_expression_init(&ops);
  assert(product_profile_begin(request, "记住我叫测试", status) == 0);
  product_profile_complete(2, true);
  assert(extracts == 0);
  product_profile_playback(request);
  product_profile_complete(request, true);
  product_profile_complete(request, true);
  assert(extracts == 1 && commits == 1);
  request = 2;
  assert(product_profile_begin(request, "记住我叫测试", status) == 0);
  product_profile_playback(request);
  product_profile_complete(request, false);
  assert(extracts == 1);
  request = 3;
  assert(product_profile_begin(request, "ordinary", status) == 0);
  args = cJSON_Parse("{\"emotion\":\"happy\"}");
  assert(product_reply_expression(args, output, sizeof(output)) == 0);
  assert(strstr(output, "\"rendered\":false"));
  assert(product_reply_expression(args, output, 2) == -ENOSPC);
  cJSON_Delete(args);
  args = cJSON_Parse("{\"emotion\":\"happy\",\"emotion\":\"sorry\"}");
  assert(product_reply_expression(args, output, sizeof(output)) == -EINVAL);
  cJSON_Delete(args);
  product_profile_complete(request, true);
  assert(commits == 1 && extracts == 1);

  root = cJSON_Parse("{\"messages\":[{\"role\":\"user\",\"content\":\"hello\"}]}");
  assert(product_agent_context(root) == 0);
  assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "messages")) == 2);
  cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "messages"), 0);
  assert(strstr(cJSON_GetObjectItem(item, "content")->valuestring, "test fact"));
  cJSON_Delete(root);
  root = cJSON_Parse("{\"messages\":[{\"role\":\"user\",\"content\":[]}]}");
  assert(product_agent_context(root) == 0);
  assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "messages")) == 1);
  cJSON_Delete(root);
  root = cJSON_Parse("{\"messages\":[{\"role\":\"user\",\"content\":\"hello\"}]}");
  fact_error = -EIO;
  assert(product_agent_context(root) == -EIO);
  fact_error = 0;
  idle = true;
  assert(product_agent_context(root) == 0);
  assert(cJSON_GetArraySize(cJSON_GetObjectItem(root, "messages")) == 1);
  cJSON_Delete(root);

  assert(product_agent_data(BKCONTROL_CONFIG_READ, 26, 0, NULL, 0, &reply) == 0);
  assert(!memcmp(reply.config_chunk, "AJV1", 4));
  assert(reply.config_chunk[15] == 7);
  for (unsigned int offset = 16; offset < reply.config_total; offset += 16)
    {
      assert(product_agent_data(BKCONTROL_CONFIG_READ, 26, offset, NULL, 0,
                                &reply) == 0);
      memcpy(assembled + offset - 16, reply.config_chunk, 16);
    }
  assert(!strcmp(assembled, facts));
  assert(product_agent_data(BKCONTROL_CONFIG_READ, 26, 1, NULL, 0, &reply) == -EINVAL);
  assert(product_agent_data(BKCONTROL_CONFIG_APPLY, 26, 0,
                           (const uint8_t *)"{}", 2, &reply) == 0);
  assert(applies == 1);
  idle = false;
  assert(product_agent_data(BKCONTROL_CONFIG_APPLY, 26, 0,
                           (const uint8_t *)"{}", 2, &reply) == -EBUSY);
  assert(applies == 1);

  /* The streaming path never queues an outbound reply. Its real message
   * bus must finish/join playback, publish history, then complete the turn.
   */
  request = 4;
  assert(product_profile_begin(request, "记住我叫测试", status) == 0);
  product_profile_playback(request);
  agent_msg_t message = {.request_id = request, .request_status = status,
    .request_commit = status, .request_complete = complete,
    .reply_stream = stream_finish, .reply_stream_started = 1};
  assert(message_bus_reply_with_history(&message, strdup("reply"), 0,
                                       history) == 0);
  assert(history_calls == 1 && commits == 2 && extracts == 2);
  request = 5;
  message.request_id = request;
  assert(product_profile_begin(request, "记住我叫测试", status) == 0);
  product_profile_playback(request);
  stream_result = -ECANCELED;
  assert(message_bus_reply_with_history(&message, strdup("reply"), 0,
                                       history) == -ECANCELED);
  assert(history_calls == 1 && commits == 2 && extracts == 2);
  idle = true;
  struct bkcontrol_session_s *session = calloc(1, sizeof(*session));
  uint8_t key[32] = {1};
  uint8_t header[8];
  assert(session && bkcontrol_session_open(session, key, execute, NULL) == 0);
  assert(bkcontrol_session_set_config_handler(session, config) == 0);
  assert(packet(session, 0, BKCONTROL_AUTH, key, sizeof(key)) == 0);
  put32(header, 26);
  put32(header + 4, 2);
  assert(packet(session, 1, BKCONTROL_CONFIG_BEGIN, header, 8) == 0);
  assert(packet(session, 2, BKCONTROL_CONFIG_APPEND,
                (const uint8_t *)"{}", 2) == 0);
  assert(packet(session, 3, BKCONTROL_CONFIG_APPLY, NULL, 0) == 0);
  assert(applies == 2);
  put32(header, 26u << 16);
  assert(packet(session, 4, BKCONTROL_CONFIG_READ, header, 4) == 0);
  bkcontrol_session_close(session);
  free(session);
  puts("AGENT_PROFILE_PASS");
  return 0;
}
