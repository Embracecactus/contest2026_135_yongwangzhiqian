/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PC_CAMERA_H
#define __APP_BK7258_PC_CAMERA_H
#include <stdbool.h>
#include <stdint.h>
#include "bk7258_control_session.h"
void bkcamera_step(uint64_t now, bool admitted);
void bkcamera_close(void);
bool bkcamera_busy(void);
bool bkcamera_work(void);
int bkcamera_control(enum bkcontrol_command_e command, uint32_t kind,
  uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status);
#endif
