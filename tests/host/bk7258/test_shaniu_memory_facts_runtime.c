/****************************************************************************
 * apps/bk7258/test_shaniu_memory_facts_runtime.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_agent_memory.h"
#include "bk7258_agent_memory_codec.h"
#include "bk7258_preferences.h"
#include "bk7258_provision_store.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nuttx/mutex.h>

#include "cJSON.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

#define TEST_ROOT "/tmp/bk7258-memory-facts-runtime"
#define TEST_POLICY TEST_ROOT "/policy"
#define TEST_SNAPSHOT TEST_ROOT "/snapshot"

struct test_message_s
{
  char role[10];
  char content[128];
};

static struct test_message_s g_messages[6];
static size_t g_message_count;
static int g_force_unknown;
static int g_session_clear_fail;

/****************************************************************************
 * Session and Storage Stubs
 ****************************************************************************/

int session_append(const char *chat_id, const char *role, const char *content)
{
  struct test_message_s *message;

  if (strcmp(chat_id, "voice") || g_message_count >= 6 ||
      strlen(role) >= sizeof(message->role) ||
      strlen(content) >= sizeof(message->content))
    {
      return -EINVAL;
    }

  message = &g_messages[g_message_count++];
  snprintf(message->role, sizeof(message->role), "%s", role);
  snprintf(message->content, sizeof(message->content), "%s", content);
  return 0;
}

int session_get_history_json(const char *chat_id, char *buffer, size_t size,
                             int maximum)
{
  cJSON *array;
  char *text;
  size_t first;

  if (strcmp(chat_id, "voice") || maximum < 0) return -EINVAL;
  array = cJSON_CreateArray();
  if (array == NULL) return -ENOMEM;
  first = g_message_count > (size_t)maximum ? g_message_count - maximum : 0;
  for (size_t i = first; i < g_message_count; i++)
    {
      cJSON *item = cJSON_CreateObject();

      if (item == NULL || !cJSON_AddStringToObject(item, "role",
                                                     g_messages[i].role) ||
          !cJSON_AddStringToObject(item, "content", g_messages[i].content))
        {
          cJSON_Delete(item);
          cJSON_Delete(array);
          return -ENOMEM;
        }
      cJSON_AddItemToArray(array, item);
    }

  text = cJSON_PrintUnformatted(array);
  cJSON_Delete(array);
  if (text == NULL) return -ENOMEM;
  if (strlen(text) + 1 > size)
    {
      cJSON_free(text);
      return -ENOSPC;
    }

  snprintf(buffer, size, "%s", text);
  cJSON_free(text);
  return 0;
}

int session_clear(const char *chat_id)
{
  if (g_session_clear_fail)
    {
      errno = EIO;
      return -1;
    }
  if (strcmp(chat_id, "voice") || g_message_count == 0)
    {
      /* Match the Agent Session API: remove() reports -1 plus errno. */

      errno = ENOENT;
      return -1;
    }
  memset(g_messages, 0, sizeof(g_messages));
  g_message_count = 0;
  return 0;
}

int bk7258_preferences_with_storage(int (*operation)(void *), void *context)
{
  return operation(context);
}

int nxmutex_lock(mutex_t *mutex)
{
  return -pthread_mutex_lock(mutex);
}

int nxmutex_unlock(mutex_t *mutex)
{
  return -pthread_mutex_unlock(mutex);
}

int __real_bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                               const uint8_t transaction[16], const void *bundle,
                               size_t size);

int __wrap_bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                               const uint8_t transaction[16], const void *bundle,
                               size_t size)
{
  if (g_force_unknown) return -EINPROGRESS;
  return __real_bkprov_store_commit(store, expected, transaction, bundle, size);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void remove_store(void)
{
  (void)unlink(TEST_POLICY "/config.bin");
  (void)unlink(TEST_POLICY "/config.pending");
  (void)unlink(TEST_SNAPSHOT "/config.bin");
  (void)unlink(TEST_SNAPSHOT "/config.pending");
  (void)rmdir(TEST_POLICY);
  (void)rmdir(TEST_SNAPSHOT);
  (void)rmdir(TEST_ROOT);
}

static void expect_fact(const char *key, const char *value)
{
  char json[512];

  assert(bkagent_memory_facts_read(json, sizeof(json)) == 0);
  assert(strstr(json, key) != NULL);
  assert(strstr(json, value) != NULL);
}

static int test_random(void *context, unsigned char *bytes, size_t size)
{
  unsigned char *next = context;

  for (size_t i = 0; i < size; i++) bytes[i] = (*next)++;
  return 0;
}

static void write_smh1(const uint8_t owner[32])
{
  struct bkmemory_policy_s policy;
  struct bkprov_store_s store;
  uint8_t plain[8] = { 'S', 'M', 'H', '1' };
  uint8_t sealed[64];
  uint8_t transaction[16] = { 'S', 'M', 'H', '1' };
  unsigned char nonce = 1;
  size_t size;

  assert(bkmemory_policy_load(TEST_POLICY, owner, &policy) == 0);
  assert(mkdir(TEST_SNAPSHOT, 0700) == 0);
  assert(bkmemory_seal(&policy, plain, sizeof(plain), sealed, sizeof(sealed),
                       &size, test_random, &nonce) == 0);
  assert(bkprov_store_open(&store, TEST_SNAPSHOT) == 0);
  assert(bkprov_store_commit(&store, 0, transaction, sealed, size) == 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  static const uint8_t owner[32] = { 1 };
  char json[512];

  remove_store();
  assert(mkdir(TEST_ROOT, 0700) == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  assert(bkagent_memory_facts_read(json, sizeof(json)) == 0);
  assert(strstr(json, "\"enabled\":false") != NULL);
  strcpy(json, "not-empty");
  assert(bkagent_memory_facts_context(json, sizeof(json)) == 0);
  assert(json[0] == 0);
  assert(bkagent_memory_control(BKCONTROL_MEMORY_SET, 1, 0) == 0);

  assert(bkagent_memory_facts_apply(
      "{\"op\":\"set\",\"key\":\"name\",\"value\":\"小山\","
      "\"expected_revision\":\"0\"}") == 0);
  expect_fact("name", "小山");
  assert(g_message_count == 0);
  assert(bkagent_memory_facts_apply(
      "{\"op\":\"set\",\"key\":\"topic\",\"value\":\"天气\","
      "\"expected_revision\":\"0\"}") == -ESTALE);

  assert(session_append("voice", "user", "你好") == 0);
  assert(session_append("voice", "assistant", "你好呀") == 0);
  assert(bkagent_memory_facts_extract_user("我喜欢蓝色") == 0);
  expect_fact("preference", "蓝色");

  assert(bkagent_memory_reset() == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  expect_fact("name", "小山");
  expect_fact("preference", "蓝色");
  assert(g_message_count == 2);

  g_session_clear_fail = 1;
  assert(bkagent_memory_facts_apply(
      "{\"op\":\"set\",\"key\":\"topic\",\"value\":\"音乐\","
      "\"expected_revision\":\"2\"}") == -EIO);
  expect_fact("topic", "音乐");
  assert(bkagent_memory_facts_context(json, sizeof(json)) == -EIO);
  g_session_clear_fail = 0;
  assert(bkagent_memory_reset() == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  expect_fact("topic", "音乐");

  assert(mkdir(TEST_SNAPSHOT "/config.pending", 0700) == 0);
  assert(bkagent_memory_facts_apply(
      "{\"op\":\"set\",\"key\":\"topic\",\"value\":\"绘画\","
      "\"expected_revision\":\"3\"}") == -EISDIR);
  expect_fact("name", "小山");
  assert(rmdir(TEST_SNAPSHOT "/config.pending") == 0);

  g_force_unknown = 1;
  assert(bkagent_memory_facts_apply(
      "{\"op\":\"set\",\"key\":\"topic\",\"value\":\"绘画\","
      "\"expected_revision\":\"3\"}") == -EINPROGRESS);
  assert(bkagent_memory_facts_read(json, sizeof(json)) == -EINPROGRESS);
  g_force_unknown = 0;
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  expect_fact("name", "小山");
  assert(bkagent_memory_facts_read(json, sizeof(json)) == 0);
  assert(strstr(json, "音乐") != NULL);
  assert(strstr(json, "绘画") == NULL);

  assert(bkagent_memory_facts_apply(
      "{\"op\":\"delete\",\"key\":\"name\","
      "\"expected_revision\":\"3\"}") == 0);
  assert(g_message_count == 0);
  assert(bkagent_memory_reset() == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  assert(bkagent_memory_facts_read(json, sizeof(json)) == 0);
  assert(strstr(json, "小山") == NULL);
  assert(strstr(json, "蓝色") != NULL);

  remove_store();
  assert(mkdir(TEST_ROOT, 0700) == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  assert(bkagent_memory_control(BKCONTROL_MEMORY_SET, 1, 0) == 0);
  write_smh1(owner);
  assert(bkagent_memory_reset() == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  assert(bkagent_memory_facts_read(json, sizeof(json)) == 0);
  assert(strstr(json, "\"facts\":[]") != NULL);
  remove_store();
  puts("MEMORY_FACTS_RUNTIME_PASS");
  return 0;
}
