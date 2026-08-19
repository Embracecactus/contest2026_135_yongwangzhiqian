/****************************************************************************
 * app/vela_claw/include/vela_claw_app.h
 *
 * Application-level wiring: brings up storage/config/memory/transports/
 * capabilities/lua/core/router and (optionally) the screen UI, and tears
 * them down. Shared by the NSH entry point and the host test harness.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_APP_H
#define VELA_CLAW_APP_H

#include "claw_common.h"

/* Bring up the whole stack. kv_dir is where the KV store / config lives
 * (e.g. "/data/vela_claw"). */
claw_err_t vela_claw_app_init(const char *kv_dir);

void vela_claw_app_deinit(void);

#ifdef CONFIG_VELA_CLAW_UI
/* Screen UI front-end (LVGL + openvela UIKit). Only compiled/linked when the
 * UI is enabled. The UI emits the same claw_event (platform "ui") to the
 * shared router and registers its own response sender. */
claw_err_t vela_claw_ui_init(void);
void       vela_claw_ui_deinit(void);
#endif

#endif /* VELA_CLAW_APP_H */
