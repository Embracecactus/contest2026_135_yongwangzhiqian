/****************************************************************************
 * apps/bk7258/bk7258_native_apps.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __APP_BK7258_NATIVE_APPS_H
#define __APP_BK7258_NATIVE_APPS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_session.h"
#include "bk7258_native_abi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKNATIVE_PACKAGE_MAX 24576u
#define BKNATIVE_IMAGE_MAX 16384u
#define BKNATIVE_REQUEST_MAX 32768u
#define BKNATIVE_APP_MAX 4u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bknative_init(void);
int bknative_read(char *output, size_t capacity);
int bknative_apply(const char *input);
int bknative_control(enum bkcontrol_command_e command, uint32_t offset,
                     const uint8_t *record, size_t size,
                     struct bkcontrol_status_s *status);

/* Returns 1 while running, 0 when stopped, or a negative cleanup failure.
 * After a step that ends the program, output carries only its completion
 * haptic/cue requests; the product applies its own gates to both.
 */

int bknative_step(uint64_t now, bool admitted,
                  struct bknative_output_s *output);

/* Product motion classifier events for a running app holding "motion". */

void bknative_post_events(uint32_t events);

/* Starts the unique installed app whose bknative_app_hash() matches, for an
 * owner-registered local trigger.  seconds 0 selects the default of 60.
 */

int bknative_launch(uint32_t hash, uint32_t seconds);
bool bknative_motion_owner(void);
int bknative_stop(void);
int bknative_abort(int reason);

#endif
