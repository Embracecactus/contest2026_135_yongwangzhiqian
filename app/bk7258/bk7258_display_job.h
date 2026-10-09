/****************************************************************************
 * app/bk7258/bk7258_display_job.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_JOB_H
#define __APP_BK7258_BK7258_DISPLAY_JOB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <pthread.h>
#include "bk7258_display_store.h"

/* Metadata is protected independently of I/O. One consumer calls step;
 * producers never invoke storage or wait for consumer I/O. Authentication
 * is upstream; binding is the already-verified authorization identity.
 */

enum bkdisplay_job_state_e
{
  BKDISPLAY_JOB_IDLE = 0,
  BKDISPLAY_JOB_QUEUED,
  BKDISPLAY_JOB_OPENING,
  BKDISPLAY_JOB_RECEIVING,
  BKDISPLAY_JOB_WRITING,
  BKDISPLAY_JOB_COMMIT_PENDING,
  BKDISPLAY_JOB_COMMITTING,
  BKDISPLAY_JOB_CANCELING,
  BKDISPLAY_JOB_DONE,
  BKDISPLAY_JOB_CANCELED,
  BKDISPLAY_JOB_FAILED,
  BKDISPLAY_JOB_UNKNOWN
};

struct bkdisplay_job_status_s
{
  uint64_t id;
  uint64_t binding;
  uint64_t deadline_ms;
  enum bkdisplay_job_state_e state;
  size_t total;
  size_t written;
  int error;
  int release_error;
  bool resources_held;
  char filename[BKDISPLAY_STORE_FILENAME_SIZE];
};

struct bkdisplay_job_ops_s
{
  /* Release must also handle a partially failed acquire. */

  int (*acquire)(void *context);
  int (*release)(void *context);
};

struct bkdisplay_job_s
{
  pthread_mutex_t lock;
  const struct bkdisplay_job_ops_s *ops;
  void *context;
  const char *root;
  struct bkdisplay_job_status_s status;
  struct bkdisplay_upload_s upload;
  uint64_t observed_ms;
  bool admitted;
  bool executing;
  bool cancel;
  int cancel_error;
  size_t pending;
  uint8_t data[BKDISPLAY_UPLOAD_CHUNK_MAX];
};

/* Initialize once with PTHREAD_MUTEX_INITIALIZER, ops, context and stable
 * absolute root; remaining fields are zero. Do not reset a live object.
 * Only the most recent result is retained; every successful begin gets a new
 * non-wrapping ID. A terminal result is not an activation/render receipt.
 */

int bkdisplay_job_gate(struct bkdisplay_job_s *job, bool admitted);
int bkdisplay_job_begin(struct bkdisplay_job_s *job, uint64_t binding,
                        size_t size, uint64_t now_ms, uint64_t deadline_ms,
                        uint64_t *id);
int bkdisplay_job_append(struct bkdisplay_job_s *job, uint64_t binding,
                         uint64_t id, size_t offset,
                         const void *data, size_t size);
int bkdisplay_job_finish(struct bkdisplay_job_s *job, uint64_t binding,
                         uint64_t id);

/* Zero means requested, not confirmed. COMMITTING is not cancellable. */

int bkdisplay_job_cancel(struct bkdisplay_job_s *job, uint64_t binding,
                         uint64_t id);
int bkdisplay_job_status(struct bkdisplay_job_s *job,
                         struct bkdisplay_job_status_s *status);
int bkdisplay_job_quiesced(struct bkdisplay_job_s *job);

/* Consumer only: one operation, with no metadata lock held during I/O. */

int bkdisplay_job_step(struct bkdisplay_job_s *job, uint64_t now_ms);

/* Native task creation failure, before any consumer has started. */

void bkdisplay_job_start_failed(struct bkdisplay_job_s *job, uint64_t id,
                                int error);

#endif /* __APP_BK7258_BK7258_DISPLAY_JOB_H */
