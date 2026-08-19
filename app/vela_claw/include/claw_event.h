/****************************************************************************
 * app/vela_claw/include/claw_event.h
 *
 * Normalized event type flowing through the system: input channels (CLI,
 * future IM) emit MESSAGE/COMMAND events, the agent loop emits OUT_MESSAGE.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_EVENT_H
#define VELA_CLAW_EVENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CLAW_EVENT_MESSAGE = 0,   /* natural-language user input */
  CLAW_EVENT_COMMAND,       /* already-structured command */
  CLAW_EVENT_TIMER,
  CLAW_EVENT_STARTUP,
  CLAW_EVENT_OUT_MESSAGE,   /* agent response to be delivered */
  CLAW_EVENT_ATTACHMENT
} claw_event_type_t;

#define CLAW_EVENT_TEXT_LEN   1024
#define CLAW_EVENT_PLAT_LEN   32
#define CLAW_EVENT_USER_LEN   64
#define CLAW_EVENT_SESS_LEN   64

typedef struct claw_event_s {
  claw_event_type_t type;
  char text[CLAW_EVENT_TEXT_LEN];
  char platform[CLAW_EVENT_PLAT_LEN];   /* "cli", "telegram", ... */
  char user[CLAW_EVENT_USER_LEN];
  char session[CLAW_EVENT_SESS_LEN];
  void *attachment;
} claw_event_t;

void claw_event_init(claw_event_t *ev, claw_event_type_t type,
                     const char *text, const char *platform,
                     const char *user, const char *session);

/* Simple in-process pub/sub bus. */
typedef void (*claw_event_handler_t)(claw_event_t *ev, void *arg);

claw_err_t claw_bus_subscribe(claw_event_handler_t handler, void *arg);
void claw_bus_unsubscribe(claw_event_handler_t handler);
void claw_bus_publish(claw_event_t *ev);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_EVENT_H */
