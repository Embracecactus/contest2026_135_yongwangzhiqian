/****************************************************************************
 * app/bk7258/bk7258_display_job_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_JOB_SERVICE_H
#define __APP_BK7258_BK7258_DISPLAY_JOB_SERVICE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_display_job.h"

/* An authenticated adapter supplies binding and the absolute
 * monotonic deadline. These calls never run file or filesystem operations.
 * No USB wire command is enabled merely by exposing this native service.
 */

int bk7258_display_job_begin(uint64_t binding, size_t size,
                             uint64_t deadline_ms, uint64_t *id);
int bk7258_display_job_append(uint64_t binding, uint64_t id, size_t offset,
                              const void *data, size_t size);
int bk7258_display_job_finish(uint64_t binding, uint64_t id);
int bk7258_display_job_cancel(uint64_t binding, uint64_t id);
int bk7258_display_job_status(struct bkdisplay_job_status_s *status);
int bk7258_display_job_quiesce(bool stop);

#endif /* __APP_BK7258_BK7258_DISPLAY_JOB_SERVICE_H */
