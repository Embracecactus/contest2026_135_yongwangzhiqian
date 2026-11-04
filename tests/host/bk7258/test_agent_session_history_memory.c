/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_agent_memory.h"
#include "bk7258_agent_memory_codec.h"
#include "bk7258_preferences.h"
#include "bk7258_provision_store.h"
#include "core/session_mgr.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <nuttx/mutex.h>

#define TEST_ROOT "/tmp/bk7258-agent-session-history-memory"
#define TEST_POLICY TEST_ROOT "/policy"
#define TEST_SNAPSHOT TEST_ROOT "/snapshot"
#define TEST_SESSIONS TEST_ROOT "/agent/sessions"

enum fault_e { FAULT_NONE, FAULT_FOPEN_EIO, FAULT_MIDREAD_EIO };
static enum fault_e g_fault;
static const char *g_session_path;
static size_t g_cookie_offset;
static const char g_cookie_line[] =
  "{\"role\":\"user\",\"content\":\"second-user\"}\n";

struct snapshot_state_s
{
  uint8_t bytes[BKPROV_STORE_RECORD_MAX];
  size_t size;
  uint64_t revision;
};

FILE *__real_fopen(const char *path, const char *mode);

static ssize_t cookie_read(void *unused, char *buffer, size_t size)
{
  size_t remaining;
  (void)unused;
  if (g_cookie_offset == sizeof(g_cookie_line) - 1)
    {
      errno = EIO;
      return -1;
    }
  remaining = sizeof(g_cookie_line) - 1 - g_cookie_offset;
  if (size > remaining) size = remaining;
  memcpy(buffer, g_cookie_line + g_cookie_offset, size);
  g_cookie_offset += size;
  return (ssize_t)size;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
  cookie_io_functions_t functions = { .read = cookie_read };
  if (g_session_path != NULL && !strcmp(path, g_session_path) &&
      !strcmp(mode, "r"))
    {
      if (g_fault == FAULT_FOPEN_EIO) { errno = EIO; return NULL; }
      if (g_fault == FAULT_MIDREAD_EIO)
        {
          g_cookie_offset = 0;
          return fopencookie(NULL, "r", functions);
        }
    }
  return __real_fopen(path, mode);
}

int bk7258_preferences_with_storage(int (*operation)(void *), void *context)
{
  return operation(context);
}

int nxmutex_lock(mutex_t *mutex) { return pthread_mutex_lock(mutex); }
int nxmutex_unlock(mutex_t *mutex) { return pthread_mutex_unlock(mutex); }

static int test_random(void *context, unsigned char *bytes, size_t size)
{
  unsigned char *next = context;
  for (size_t i = 0; i < size; i++) bytes[i] = (*next)++;
  return 0;
}

static struct snapshot_state_s snapshot_state(void)
{
  struct bkprov_store_s store;
  struct snapshot_state_s state;

  memset(&state, 0, sizeof(state));
  assert(bkprov_store_open(&store, TEST_SNAPSHOT) == 0);
  assert(bkprov_store_load(&store, state.bytes, sizeof(state.bytes),
                           &state.size, &state.revision, NULL) == 0);
  return state;
}

static void expect_unchanged(const struct snapshot_state_s *before,
                             enum fault_e fault)
{
  struct snapshot_state_s after;

  g_fault = fault;
  assert(bkagent_memory_commit(0, "second") != 0);
  after = snapshot_state();
  assert(after.revision == before->revision && after.size == before->size);
  assert(!memcmp(after.bytes, before->bytes, after.size));
  g_fault = FAULT_NONE;
}

int main(void)
{
  static const uint8_t owner[32] = { 1 };
  unsigned char random = 1;
  struct snapshot_state_s before;
  char session_path[sizeof(TEST_SESSIONS) + 32];

  assert(mkdir(TEST_ROOT, 0700) == 0);
  assert(mkdir(TEST_ROOT "/agent", 0700) == 0);
  assert(mkdir(TEST_SESSIONS, 0700) == 0);
  assert(bkmemory_policy_set(TEST_POLICY, owner, true, false, test_random,
                             &random) == 0);
  assert(bkagent_memory_bind(owner) == 0);
  assert(bkagent_memory_restore(0) == 0);
  assert(session_append("voice", "user", "first-user") == 0);
  assert(session_append("voice", "assistant", "first") == 0);
  assert(bkagent_memory_commit(0, "first") == 0);
  before = snapshot_state();
  assert(session_append("voice", "user", "second-user") == 0);
  assert(session_append("voice", "assistant", "second") == 0);
  assert(snprintf(session_path, sizeof(session_path), "%s/tg_voice.jsonl",
                  TEST_SESSIONS) > 0);
  g_session_path = session_path;
  expect_unchanged(&before, FAULT_FOPEN_EIO);
  expect_unchanged(&before, FAULT_MIDREAD_EIO);
  assert(bkagent_memory_commit(0, "second") == 0);
  assert(snapshot_state().revision == before.revision + 1);
  puts("AGENT_SESSION_HISTORY_MEMORY_PASS");
  return 0;
}
