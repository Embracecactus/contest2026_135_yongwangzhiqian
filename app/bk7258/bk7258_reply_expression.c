/****************************************************************************
 * apps/bk7258/bk7258_reply_expression.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_reply_expression.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct bkreply_expression_ops_s g_ops;
static uint64_t g_request;
static unsigned int g_emotion;
static uint32_t g_trial;
static bool g_started;
static bool g_finished;
static bool g_releasing;

/* These names match installed eye assets; emotion remains separate metadata.
 * No new frame buffer, TTS queue or rendering worker is allocated here.
 */

static const char *const g_emotions[] =
{
  "neutral", "happy", "confused", "thinking", "sorry"
};

static const char *const g_expressions[] =
{
  "neutral", "happy", "thinking", "thinking", "sad"
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bkreply_expression_init(const struct bkreply_expression_ops_s *ops)
{
  pthread_mutex_lock(&g_lock);
  g_ops = *ops;
  g_request = 0;
  g_emotion = 0;
  g_started = false;
  g_finished = false;
  g_releasing = g_trial != 0;
  if (g_ops.active)
    {
      g_ops.active(false);
    }

  pthread_mutex_unlock(&g_lock);
  bkreply_expression_step();
}

int bkreply_expression_begin(uint64_t request)
{
  int ret = 0;
  pthread_mutex_lock(&g_lock);
  if (!request || request < g_request ||
      (request == g_request && g_finished))
    {
      ret = -ESTALE;
    }
  else if (request != g_request)
    {
      if (g_trial)
        {
          g_releasing = true;
          if (g_ops.active)
            {
              g_ops.active(false);
            }
        }

      g_request = request;
      g_emotion = 0;
      g_started = false;
      g_finished = false;
    }

  pthread_mutex_unlock(&g_lock);
  bkreply_expression_step();
  return ret;
}

int bkreply_expression_set(uint64_t request, const char *emotion)
{
  int ret = -EINVAL;
  pthread_mutex_lock(&g_lock);
  if (!request || request != g_request || g_finished)
    {
      ret = -ESTALE;
    }
  else if (g_started)
    {
      ret = -EBUSY;
    }
  else if (emotion)
    {
      for (unsigned int i = 0;
           i < sizeof(g_emotions) / sizeof(g_emotions[0]); i++)
        {
          if (!strcmp(emotion, g_emotions[i]))
            {
              g_emotion = i;
              ret = 0;
              break;
            }
        }
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bkreply_expression_start(uint64_t request)
{
  int ret = 0;
  pthread_mutex_lock(&g_lock);
  if (!request || request != g_request || g_finished)
    {
      ret = -ESTALE;
    }
  else if (!g_started)
    {
      g_started = true;
      if (g_emotion)
        {
          ret = g_trial ? -EBUSY : !g_ops.start ? -ENOTSUP :
                g_ops.start(g_expressions[g_emotion], &g_trial);
          if (!ret && g_ops.active)
            {
              g_ops.active(true);
            }
        }

      syslog(LOG_INFO,
        "BKEMOTION request=%llu emotion=%u playback=1 "
        "accepted=%u result=%d\n",
        (unsigned long long)request, g_emotion, g_trial != 0 && !ret, ret);
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

void bkreply_expression_step(void)
{
  pthread_mutex_lock(&g_lock);
  if (g_releasing && g_trial && g_ops.cancel)
    {
      int ret = g_ops.cancel(g_trial);
      if (ret != -EBUSY)
        {
          /* Success is cancellation acceptance. The existing display owner
           * restores only while this trial still owns the rendered pixels.
           */

          syslog(LOG_INFO, "BKEMOTION release=%lu result=%d\n",
                 (unsigned long)g_trial, ret);
          g_trial = 0;
          g_releasing = false;
        }
    }

  pthread_mutex_unlock(&g_lock);
}

void bkreply_expression_finish(uint64_t request)
{
  pthread_mutex_lock(&g_lock);
  if (request && request == g_request)
    {
      g_finished = true;
      g_emotion = 0;
      g_releasing = g_trial != 0;
      if (g_ops.active)
        {
          g_ops.active(false);
        }
    }

  pthread_mutex_unlock(&g_lock);
  bkreply_expression_step();
}
