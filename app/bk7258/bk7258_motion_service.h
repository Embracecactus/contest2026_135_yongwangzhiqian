/****************************************************************************
 * app/bk7258/bk7258_motion_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_MOTION_SERVICE_H
#define __APP_BK7258_BK7258_MOTION_SERVICE_H

#include <stdbool.h>
#include "bk7258_motion_protocol.h"

/* Stops software sampling admission; zero acknowledges no in-flight I/O.
 * Stop revokes queued and waiting samples even after admission resumes.
 * Admission identity exhaustion fails closed until device restart.
 * This does not certify physical sensor power-down behind the uORB
 * upper half.
 */

int bk7258_motion_service_quiesce(bool stop);
int bk7258_motion_service_prepare(void);
int bk7258_motion_service_start(void);
int bk7258_motion_service_sample(struct bkmotion_rpc_response_s *sample);

#endif /* __APP_BK7258_BK7258_MOTION_SERVICE_H */
