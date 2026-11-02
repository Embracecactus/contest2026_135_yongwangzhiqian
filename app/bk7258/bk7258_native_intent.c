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

/* Metadata only: the NFC worker submits under its own spinlock, so this
 * module never blocks, allocates or touches storage.
 */

static spinlock_t g_lock = SP_UNLOCKED;
static struct bknative_intent_status_s g_status;
static uint64_t g_created;
static bool g_taken;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t intent_now(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
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

int bknative_intent_submit(uint64_t argument, uint32_t *id)
{
  irqstate_t flags;
  int ret = 0;

  if (id == NULL || !bknative_intent_argument_valid(argument))
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
      g_status.app_hash = (uint32_t)(argument >> 32);
      g_status.seconds = (uint32_t)argument;
      g_created = intent_now();
      g_taken = false;
      *id = g_status.id;
    }

  spin_unlock_irqrestore(&g_lock, flags);
  return ret;
}

int bknative_intent_cancel(uint32_t id)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  int ret = -ESTALE;

  /* Once taken, the start is already in progress on the product loop and
   * finish() owns the outcome; a running app is stopped by its own owner.
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

bool bknative_intent_take(bool admitted, struct bknative_intent_status_s *out)
{
  irqstate_t flags = spin_lock_irqsave(&g_lock);
  bool taken = false;

  if (g_status.phase == 1 && !g_taken)
    {
      uint64_t now = intent_now();
      if (now < g_created || now - g_created > BKNATIVE_INTENT_PENDING_MS)
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
