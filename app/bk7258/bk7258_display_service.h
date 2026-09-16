/****************************************************************************
 * app/bk7258/bk7258_display_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Product-facing Shaniu dual-eye display service.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_SERVICE_H
#define __APP_BK7258_BK7258_DISPLAY_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "bk7258_display_pack.h"
#include "bk7258_display_store.h"

enum bkdisplay_service_state_e
{
  BKDISPLAY_SERVICE_STOPPED = 0,
  BKDISPLAY_SERVICE_WAITING_DEVICES,
  BKDISPLAY_SERVICE_WAITING_ASSET,
  BKDISPLAY_SERVICE_READY,
  BKDISPLAY_SERVICE_ERROR,
};

struct bkdisplay_service_status_s
{
  enum bkdisplay_service_state_e state;
  int last_error;
  bool physical_mapping_verified;
  uint8_t screen_count;
  uint32_t render_sequence;
  char expression[BKDISPLAY_EXPRESSION_SIZE];
  char pack_id[BKDISPLAY_PACK_ID_SIZE];
  uint32_t pack_revision;
};

int bk7258_display_service_prepare(void);
int bk7258_display_service_start(void);

/* These transport-neutral calls are the future phone/Gateway adapter seam.
 * install() consumes an already-uploaded file from display/staging and also
 * activates it.  Neither call owns a network protocol.
 */

int bk7258_display_set_expression(const char *expression);
int bk7258_display_replace_expression(const char *expected,
                                      const char *replacement);
int bk7258_display_show_mapping_test(void);
int bk7258_display_install(const char *filename);
int bk7258_display_activate(const char *filename);
int bk7258_display_get_status(struct bkdisplay_service_status_s *status);

#endif /* __APP_BK7258_BK7258_DISPLAY_SERVICE_H */
