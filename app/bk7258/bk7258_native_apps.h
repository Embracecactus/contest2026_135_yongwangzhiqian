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

/* Returns 1 while running, 0 when stopped, or a negative failure. */

int bknative_step(uint64_t now, bool admitted,
                  struct bknative_frame_s *frame);
int bknative_stop(void);
int bknative_abort(int reason);

#endif
