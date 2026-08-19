/****************************************************************************
 * app/vela_claw/include/claw_cli.h
 *
 * Serial/stdio REPL input channel. Commands: ask/ask_once/cap/auto/session.
 * Input is normalized into a claw_event and pushed to the configured sink
 * (normally the event router).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_CLI_H
#define VELA_CLAW_CLI_H

#include "claw_common.h"
#include "claw_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where normalized input events go (set by the app shell to the router).
 * The sink signature matches claw_event_router_handle so the router can be
 * wired in directly. */
claw_err_t claw_cli_set_event_sink(claw_err_t (*sink)(claw_event_t *ev));

claw_err_t claw_cli_start(void);   /* blocking REPL on stdin */
void       claw_cli_stop(void);

/* Non-interactive one-shot submit (used by tests / scripts). */
claw_err_t claw_cli_submit_line(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_CLI_H */
