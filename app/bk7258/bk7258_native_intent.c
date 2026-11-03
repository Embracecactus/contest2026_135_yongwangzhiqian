/****************************************************************************
 * apps/bk7258/bk7258_native_intent.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_native_intent.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Metadata only: producers may hold their own spinlock (the NFC worker) or
 * run on any CPU (the Agent tool), so this module never blocks, allocates or
 * touches storage.  The native manager owns every side effect on CPU0.
 */

static spinlock_t g_lock = SP_UNLOCKED;
static struct bknative_intent_status_s g_status;
static uint64_t g_created;
static uint32_t g_window;
static bool g_taken;

static spinlock_t g_catalog_lock = SP_UNLOCKED;
static struct bknative_catalog_s g_catalog;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t intent_now(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool request_valid(const struct bknative_request_s *request)
{
  if (request == NULL || request->app_hash == 0 || request->window_ms == 0 ||
      request->window_ms > BKNATIVE_INTENT_VOICE_MS)
    {
      return false;
    }

  switch (request->kind)
    {
      case BKNATIVE_INTENT_START:
        return request->seconds <= BKNATIVE_INTENT_SECONDS_MAX &&
               !request->minute && !request->days;
      case BKNATIVE_INTENT_STOP:
      case BKNATIVE_INTENT_SCHEDULE_CLEAR:
        return !request->seconds && !request->minute && !request->days;
      case BKNATIVE_INTENT_SCHEDULE_SET:
        return request->seconds <= BKNATIVE_INTENT_SECONDS_MAX &&
               request->minute < 1440 && request->days &&
               !(request->days & ~BKNATIVE_DAYS_ALL);
      default:
        return false;
    }
}

static bool text_has(const char *text, size_t size, const char *word)
{
  size_t length = strlen(word);
  for (size_t i = 0; length <= size && i + length <= size; i++)
    {
      if (!memcmp(text + i, word, length))
        {
          return true;
        }
    }

  return false;
}

static bool text_equal(const char *text, size_t size, const char *word)
{
  return strlen(word) == size && !memcmp(text, word, size);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint32_t bknative_app_hash(const char *id)
{
  uint32_t hash = 2166136261u;
  while (id && *id)
    {
      hash ^= (uint8_t)*id++;
      hash *= 16777619u;
    }

  return hash;
}

bool bknative_intent_argument_valid(uint64_t argument)
{
  return (argument >> 32) != 0 &&
         (uint32_t)argument <= BKNATIVE_INTENT_SECONDS_MAX;
}

int bknative_intent_request(const struct bknative_request_s *request,
                            uint32_t *id)
{
  irqstate_t flags;
  int ret = 0;

  if (id == NULL || !request_valid(request))
    {
      return -EINVAL;
    }

  *id = 0;
  flags = spin_lock_irqsave(&g_lock);
  if (g_status.phase == 1)
    {
      ret = -EBUSY;
    }
  else if (g_status.id == UINT32_MAX)
    {
      ret = -EOVERFLOW;
    }
  else
    {
      g_status.id++;
      g_status.phase = 1;
      g_status.error = 0;
      g_status.kind = request->kind;
      g_status.app_hash = request->app_hash;
      g_status.seconds = request->seconds;
      g_status.minute = request->minute;
      g_status.days = request->days;
      g_created = intent_now();
      g_window = request->window_ms;
      g_taken = false;
      *id = g_status.id;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bknative_intent_submit(uint64_t argument, uint32_t *id)
{
  struct bknative_request_s request =
  {
    .kind = BKNATIVE_INTENT_START,
    .app_hash = (uint32_t)(argument >> 32),
    .seconds = (uint32_t)argument,
    .window_ms = BKNATIVE_INTENT_PENDING_MS
  };

  if (id == NULL || !bknative_intent_argument_valid(argument))
    {
      return -EINVAL;
    }

  return bknative_intent_request(&request, id);
}

int bknative_intent_cancel(uint32_t id)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  int ret = -ESTALE;

  /* Once taken, the request is in progress on the product loop and finish()
   * owns the outcome; a running app is stopped by its own owner.
   */

  if (id && id == g_status.id && g_status.phase == 1 && !g_taken)
    {
      g_status.phase = 4;
      g_status.error = -ECANCELED;
      ret = 0;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

void bknative_intent_status(struct bknative_intent_status_s *status)
{
  if (status)
    {
      irqstate_t flags = spin_lock_irqsave(&g_lock);
      *status = g_status;
      spin_unlock_irqrestore(&g_lock, flags);
    }
}

bool bknative_intent_take(bool app_admitted, bool control_admitted,
                          struct bknative_intent_status_s *out)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  bool taken = false;

  if (g_status.phase == 1 && !g_taken)
    {
      uint64_t now = intent_now();
      bool admitted = g_status.kind == BKNATIVE_INTENT_START ?
                      app_admitted : control_admitted;
      if (now < g_created || now - g_created > g_window)
        {
          g_status.phase = 3;
          g_status.error = -ETIMEDOUT;
        }
      else if (admitted)
        {
          g_taken = true;
          taken = true;
          if (out)
            {
              *out = g_status;
            }
        }
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return taken;
}

void bknative_intent_finish(uint32_t id, int result)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  if (id && id == g_status.id && g_status.phase == 1 && g_taken)
    {
      g_status.phase = result ? 3 : 2;
      g_status.error = result;
      g_taken = false;
    }

  spin_unlock_irqrestore(&g_lock, flags);
}

void bknative_catalog_publish(const struct bknative_catalog_s *catalog)
{
  if (catalog)
    {
      irqstate_t flags = spin_lock_irqsave(&g_catalog_lock);
      g_catalog = *catalog;
      g_catalog.valid = true;
      spin_unlock_irqrestore(&g_catalog_lock, flags);
    }
}

void bknative_catalog_read(struct bknative_catalog_s *catalog)
{
  if (catalog)
    {
      irqstate_t flags = spin_lock_irqsave(&g_catalog_lock);
      *catalog = g_catalog;
      spin_unlock_irqrestore(&g_catalog_lock, flags);
    }
}

bool bknative_text_match(const char *text,
                         const struct bknative_catalog_s *catalog,
                         uint32_t *hash)
{
  static const char *const rejects[] =
  {
    "?", "？", "\"", "“", "”", "不", "别", "没", "如果", "要是"
  };

  static const char *const polite[] =
  {
    "傻妞，", "傻妞", "请帮我", "帮我", "替我", "请"
  };

  static const char *const decisions[] =
  {
    "做个决定", "做决定", "决定一下", "摇一摇"
  };

  static const char *const verbs[] =
  {
    "启动", "打开", "开始"
  };

  size_t size;

  if (text == NULL || catalog == NULL || hash == NULL || !catalog->valid)
    {
      return false;
    }

  size = strnlen(text, 256);
  while (size && (text[0] == ' ' || text[0] == '\t'))
    {
      text++;
      size--;
    }

  while (size && (text[size - 1] == ' ' || text[size - 1] == '\t' ||
                  text[size - 1] == '\r' || text[size - 1] == '\n' ||
                  text[size - 1] == '!' || text[size - 1] == '.'))
    {
      size--;
    }

  if (size >= strlen("。") &&
      (!memcmp(text + size - strlen("。"), "。", strlen("。")) ||
       !memcmp(text + size - strlen("！"), "！", strlen("！"))))
    {
      size -= strlen("。");
    }

  if (size == 0 || size >= 256)
    {
      return false;
    }

  for (size_t i = 0; i < sizeof(rejects) / sizeof(rejects[0]); i++)
    {
      if (text_has(text, size, rejects[i]))
        {
          return false;
        }
    }

  for (size_t i = 0; i < sizeof(polite) / sizeof(polite[0]); i++)
    {
      size_t length = strlen(polite[i]);
      if (size > length && !memcmp(text, polite[i], length))
        {
          text += length;
          size -= length;
          i = (size_t)-1; /* "傻妞，帮我做个决定": strip each prefix once. */
          if (size == 0)
            {
              return false;
            }
        }
    }

  for (size_t i = 0; i < sizeof(decisions) / sizeof(decisions[0]); i++)
    {
      if (text_equal(text, size, decisions[i]))
        {
          uint32_t shake = bknative_app_hash("shake");
          for (unsigned int app = 0; app < catalog->count &&
               app < BKNATIVE_CATALOG_APPS; app++)
            {
              if (catalog->apps[app].hash == shake)
                {
                  *hash = shake;
                  return true;
                }
            }

          return false;
        }
    }

  for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); i++)
    {
      size_t length = strlen(verbs[i]);
      if (size <= length || memcmp(text, verbs[i], length))
        {
          continue;
        }

      for (unsigned int app = 0; app < catalog->count &&
           app < BKNATIVE_CATALOG_APPS; app++)
        {
          if (text_equal(text + length, size - length,
                         catalog->apps[app].name))
            {
              *hash = catalog->apps[app].hash;
              return true;
            }
        }
    }

  return false;
}
