/****************************************************************************
 * app/vela_claw/include/claw_event_router.h
 *
 * Declarative event router. Loads router_rules.json; for each inbound event
 * it matches rules and performs actions: run_agent, run_script, call_cap,
 * send_message, drop. Mirrors esp-claw's claw_event_router + router_rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_EVENT_ROUTER_H
#define VELA_CLAW_EVENT_ROUTER_H

#include "claw_common.h"
#include "claw_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wire the agent runner (normally claw_core_submit). */
void claw_event_router_set_agent_runner(
    claw_err_t (*runner)(const char *prompt, const char *session,
                         void (*cb)(const char *, void *), void *arg));

/* Register a sender for a platform (used by send_message action). */
void claw_event_router_set_sender(const char *platform,
                                  claw_err_t (*sender)(const char *text,
                                                       const char *session));

claw_err_t claw_event_router_init(const char *rules_path);
void       claw_event_router_deinit(void);

/* Handle an inbound event: match rules and perform actions. */
claw_err_t claw_event_router_handle(claw_event_t *ev);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_EVENT_ROUTER_H */
