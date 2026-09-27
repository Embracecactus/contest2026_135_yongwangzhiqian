/****************************************************************************
 * app/bk7258/bk7258_display_selection_control.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_DISPLAY_SELECTION_CONTROL_H
#define __APP_BK7258_DISPLAY_SELECTION_CONTROL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_session.h"

/* Serialized authenticated owner only. ESC1/ESS1 kind17 is specified in
 * acceptance/contracts.md. No I/O, credential ownership or implicit replay.
 */

struct bkselection_control_s
{
  uint8_t epoch[16];
  uint8_t request[96];
  uint8_t query[16];
  uint8_t snapshot[128];
  uint8_t catalog_query[16];
  uint8_t catalog_snapshot[608];
  bool catalog_captured;
  uint64_t sequence;
  uint32_t id;
  bool bound;
  bool accepted;
  bool captured;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bkselection_control_bind(struct bkselection_control_s *state,
                             const uint8_t epoch[16]);
void bkselection_control_invalidate(struct bkselection_control_s *state);
int bkselection_control(struct bkselection_control_s *state,
                        enum bkcontrol_command_e command, uint32_t offset,
                        const uint8_t *record, size_t size,
                        struct bkcontrol_status_s *status);
int bkcatalog_control(struct bkselection_control_s *state,
                      enum bkcontrol_command_e command, uint32_t offset,
                      const uint8_t *record, size_t size,
                      struct bkcontrol_status_s *status);
#endif
