/****************************************************************************
 * app/vela_claw/src/platform/claw_event.c
 *
 * Event construction + a simple in-process pub/sub bus. The bus is used by
 * input channels (CLI) and the agent loop to decouple producers from
 * consumers (e.g. the serial sender, future IM bridges).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "claw_common.h"
#include "claw_log.h"
#include "claw_event.h"
#include "claw_rtos.h"

void claw_event_init(claw_event_t *ev, claw_event_type_t type,
                     const char *text, const char *platform,
                     const char *user, const char *session)
{
  if (!ev) return;
  memset(ev, 0, sizeof(*ev));
  ev->type = type;
  if (text)     strncpy(ev->text, text, sizeof(ev->text) - 1);
  if (platform) strncpy(ev->platform, platform, sizeof(ev->platform) - 1);
  if (user)     strncpy(ev->user, user, sizeof(ev->user) - 1);
  if (session)  strncpy(ev->session, session, sizeof(ev->session) - 1);
}

/* ---- pub/sub bus (mutex-protected fixed-size subscriber table) ---- */

#define MAX_BUS_HANDLERS 16

static claw_event_handler_t g_handlers[MAX_BUS_HANDLERS];
static void                *g_handler_args[MAX_BUS_HANDLERS];
static int                  g_handler_count;
static claw_mutex_t         g_bus_lock;
static int                  g_bus_init;

static void bus_ensure(void)
{
  if (g_bus_init) return;
  claw_mutex_init(&g_bus_lock);
  g_bus_init = 1;
}

claw_err_t claw_bus_subscribe(claw_event_handler_t handler, void *arg)
{
  bus_ensure();
  claw_mutex_lock(&g_bus_lock);
  if (g_handler_count >= MAX_BUS_HANDLERS)
    {
      claw_mutex_unlock(&g_bus_lock);
      return CLAW_ENOMEM;
    }
  g_handlers[g_handler_count] = handler;
  g_handler_args[g_handler_count] = arg;
  g_handler_count++;
  claw_mutex_unlock(&g_bus_lock);
  return CLAW_OK;
}

void claw_bus_unsubscribe(claw_event_handler_t handler)
{
  bus_ensure();
  claw_mutex_lock(&g_bus_lock);
  for (int i = 0; i < g_handler_count; i++)
    {
      if (g_handlers[i] == handler)
        {
          g_handlers[i] = g_handlers[g_handler_count - 1];
          g_handler_args[i] = g_handler_args[g_handler_count - 1];
          g_handler_count--;
          break;
        }
    }
  claw_mutex_unlock(&g_bus_lock);
}

void claw_bus_publish(claw_event_t *ev)
{
  bus_ensure();
  claw_mutex_lock(&g_bus_lock);
  for (int i = 0; i < g_handler_count; i++)
    g_handlers[i](ev, g_handler_args[i]);
  claw_mutex_unlock(&g_bus_lock);
}
