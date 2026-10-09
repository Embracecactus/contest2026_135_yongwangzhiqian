/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_NFC_CONTROL_H
#define BK7258_NFC_CONTROL_H
#include "bk7258_control_session.h"
int bknfc_control(enum bkcontrol_command_e command, uint32_t offset,
                  const uint8_t *record, size_t size,
                  struct bkcontrol_status_s *status);
/* NCA1 independent read-only record; original NCS1 remains unchanged. */
int bknfc_scene_control(enum bkcontrol_command_e command, uint32_t offset,
                        struct bkcontrol_status_s *status);
#endif
