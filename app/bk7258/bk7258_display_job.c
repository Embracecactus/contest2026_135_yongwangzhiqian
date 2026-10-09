/****************************************************************************
 * app/bk7258/bk7258_display_job.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_display_job.h"

#include <errno.h>
#include <string.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bkdisplay_job_active(struct bkdisplay_job_s *job)
{
  return job->status.state >= BKDISPLAY_JOB_QUEUED &&
         job->status.state <= BKDISPLAY_JOB_CANCELING;
}

static int bkdisplay_job_lock(struct bkdisplay_job_s *job)
{
  int ret;

  if (job == NULL)
    {
      return -EINVAL;
    }

  ret = pthread_mutex_trylock(&job->lock);
  return ret == EBUSY ? -EAGAIN : -ret;
}

static int bkdisplay_job_identity(struct bkdisplay_job_s *job,
                                  uint64_t binding, uint64_t id)
{
  return id != 0 && binding != 0 && job->status.id == id &&
         job->status.binding == binding ? 0 : -ESTALE;
}

int bkdisplay_job_gate(struct bkdisplay_job_s *job, bool admitted)
{
  int ret = bkdisplay_job_lock(job);

  if (ret < 0)
    {
      return ret;
    }

  job->admitted = admitted;
  if (!admitted && bkdisplay_job_active(job))
    {
      job->cancel = true;
      job->cancel_error = -ECANCELED;
    }

  pthread_mutex_unlock(&job->lock);
  return 0;
}

int bkdisplay_job_begin(struct bkdisplay_job_s *job, uint64_t binding,
                        size_t size, uint64_t now_ms, uint64_t deadline_ms,
                        uint64_t *id)
{
  int ret;
  uint64_t next;

  if (job == NULL || id == NULL || binding == 0 || size < 128 ||
      size > BKDISPLAY_MAX_PACK_BYTES || deadline_ms <= now_ms ||
      job->root == NULL || job->root[0] != '/' || job->ops == NULL ||
      job->ops->acquire == NULL || job->ops->release == NULL)
    {
      return -EINVAL;
    }

  ret = bkdisplay_job_lock(job);
  if (ret < 0)
    {
      return ret;
    }

  if (!job->admitted)
    {
      ret = -EACCES;
    }
  else if (bkdisplay_job_active(job) || job->executing ||
           job->status.resources_held)
    {
      ret = -EBUSY;
    }
  else if (job->status.id == UINT64_MAX)
    {
      ret = -EOVERFLOW;
    }
  else
    {
      next = job->status.id + 1;
      memset(&job->status, 0, sizeof(job->status));
      memset(&job->upload, 0, sizeof(job->upload));
      job->status.id = next;
      job->status.binding = binding;
      job->status.total = size;
      job->status.deadline_ms = deadline_ms;
      job->status.state = BKDISPLAY_JOB_QUEUED;
      job->observed_ms = now_ms;
      job->pending = 0;
      job->cancel = false;
      job->cancel_error = 0;
      *id = next;
    }

  pthread_mutex_unlock(&job->lock);
  return ret;
}

int bkdisplay_job_append(struct bkdisplay_job_s *job, uint64_t binding,
                         uint64_t id, size_t offset,
                         const void *data, size_t size)
{
  int ret = bkdisplay_job_lock(job);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_job_identity(job, binding, id);
  if (ret == 0)
    {
      if (!bkdisplay_job_active(job))
        {
          ret = -EALREADY;
        }
      else if (!job->admitted || job->cancel)
        {
          ret = -ECANCELED;
        }
      else if (job->executing || job->pending != 0 ||
               job->status.state != BKDISPLAY_JOB_RECEIVING)
        {
          ret = -EBUSY;
        }
      else if (data == NULL || size == 0 ||
               size > BKDISPLAY_UPLOAD_CHUNK_MAX ||
               offset != job->status.written ||
               size > job->status.total - offset)
        {
          ret = -EINVAL;
        }
      else
        {
          memcpy(job->data, data, size);
          job->pending = size;
        }
    }

  pthread_mutex_unlock(&job->lock);
  return ret;
}

int bkdisplay_job_finish(struct bkdisplay_job_s *job, uint64_t binding,
                         uint64_t id)
{
  int ret = bkdisplay_job_lock(job);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_job_identity(job, binding, id);
  if (ret == 0)
    {
      if (!bkdisplay_job_active(job))
        {
          ret = -EALREADY;
        }
      else if (!job->admitted || job->cancel)
        {
          ret = -ECANCELED;
        }
      else if (job->executing || job->pending != 0 ||
               job->status.state != BKDISPLAY_JOB_RECEIVING) ret = -EBUSY;
      else if (job->status.written != job->status.total)
        {
          ret = -EAGAIN;
        }
      else
        {
          job->status.state = BKDISPLAY_JOB_COMMIT_PENDING;
        }
    }

  pthread_mutex_unlock(&job->lock);
  return ret;
}

int bkdisplay_job_cancel(struct bkdisplay_job_s *job, uint64_t binding,
                         uint64_t id)
{
  int ret = bkdisplay_job_lock(job);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkdisplay_job_identity(job, binding, id);
  if (ret == 0)
    {
      if (job->status.state == BKDISPLAY_JOB_COMMITTING)
        {
          ret = -EBUSY;
        }
      else if (job->status.state == BKDISPLAY_JOB_CANCELED)
        {
          ret = 0;
        }
      else if (!bkdisplay_job_active(job))
        {
          ret = -EALREADY;
        }
      else
        {
          job->cancel = true;
          job->cancel_error = -ECANCELED;
        }
    }

  pthread_mutex_unlock(&job->lock);
  return ret;
}

int bkdisplay_job_status(struct bkdisplay_job_s *job,
                         struct bkdisplay_job_status_s *status)
{
  int ret;

  if (status == NULL)
    {
      return -EINVAL;
    }

  ret = bkdisplay_job_lock(job);
  if (ret < 0)
    {
      return ret;
    }

  *status = job->status;
  pthread_mutex_unlock(&job->lock);
  return 0;
}

int bkdisplay_job_quiesced(struct bkdisplay_job_s *job)
{
  int ret = bkdisplay_job_lock(job);

  if (ret < 0)
    {
      return ret;
    }

  if (job->executing || bkdisplay_job_active(job))
    {
      ret = -EAGAIN;
    }
  else if (job->status.resources_held)
    ret = job->status.release_error < 0 ? job->status.release_error : -EIO;
  pthread_mutex_unlock(&job->lock);
  return ret;
}

/* The sole consumer owns upload and all I/O. Shared status is only published
 * after actual cleanup. Uncertain descriptors/volumes stay pinned without
 * auto-retry; an unlink failure remains an error even after safe unmount.
 */

static int bkdisplay_job_release(struct bkdisplay_job_s *job, int *cleanup)
{
  int ret = bkdisplay_upload_quiesced(&job->upload);

  if (ret == -EBUSY)
    {
      *cleanup = bkdisplay_upload_cancel(&job->upload);
      ret = bkdisplay_upload_quiesced(&job->upload);
    }

  return ret < 0 ? ret : job->ops->release(job->context);
}

int bkdisplay_job_step(struct bkdisplay_job_s *job, uint64_t now_ms)
{
  struct bkdisplay_store_selection_s selected;
  enum bkdisplay_job_state_e operation;
  enum bkdisplay_job_state_e final = BKDISPLAY_JOB_IDLE;
  bool owns;
  size_t count;
  size_t offset;
  int locked;
  int release = 0;
  int cleanup = 0;
  int ret = bkdisplay_job_lock(job);

  if (ret < 0)
    {
      return ret;
    }

  if (job->executing || !bkdisplay_job_active(job))
    {
      pthread_mutex_unlock(&job->lock);
      return 0;
    }

  if (now_ms < job->observed_ms || now_ms >= job->status.deadline_ms)
    {
      job->cancel = true;
      job->cancel_error = -ETIMEDOUT;
    }

  job->observed_ms = now_ms;
  if (job->cancel) operation = BKDISPLAY_JOB_CANCELING;
  else if (job->status.state == BKDISPLAY_JOB_QUEUED)
    operation = BKDISPLAY_JOB_OPENING;
  else if (job->pending != 0) operation = BKDISPLAY_JOB_WRITING;
  else if (job->status.state == BKDISPLAY_JOB_COMMIT_PENDING)
    operation = BKDISPLAY_JOB_COMMITTING;
  else
    {
      pthread_mutex_unlock(&job->lock);
      return 0;
    }

  job->status.state = operation;
  job->executing = true;
  if (operation == BKDISPLAY_JOB_OPENING)
    job->status.resources_held = true;
  owns = job->status.resources_held;
  count = job->pending;
  offset = job->status.written;
  ret = job->cancel_error;
  pthread_mutex_unlock(&job->lock);

  if (operation == BKDISPLAY_JOB_OPENING)
    {
      ret = job->ops->acquire(job->context);
      if (ret == 0)
        ret = bkdisplay_upload_begin(&job->upload, job->root,
                                      job->status.total);
      if (ret < 0) final = BKDISPLAY_JOB_FAILED;
    }
  else if (operation == BKDISPLAY_JOB_WRITING)
    {
      ret = bkdisplay_upload_append(&job->upload, offset, job->data, count);
      if (ret < 0) final = BKDISPLAY_JOB_FAILED;
    }
  else if (operation == BKDISPLAY_JOB_COMMITTING)
    {
      ret = bkdisplay_upload_finish(&job->upload, &selected);
      final = ret == 0 ? BKDISPLAY_JOB_DONE : BKDISPLAY_JOB_UNKNOWN;
    }
  else
    {
      final = BKDISPLAY_JOB_CANCELED;
    }

  if (final != BKDISPLAY_JOB_IDLE && owns)
    release = bkdisplay_job_release(job, &cleanup);
  if (cleanup < 0)
    {
      ret = cleanup;
      final = BKDISPLAY_JOB_UNKNOWN;
    }

  /* Producers only hold this mutex for bounded metadata/copy operations.
   * Waiting here never prevents the control side from reading during I/O.
   */

  locked = pthread_mutex_lock(&job->lock);
  if (locked != 0)
    {
      return -locked;
    }

  job->executing = false;
  job->pending = 0;
  if (operation == BKDISPLAY_JOB_WRITING && ret == 0)
    job->status.written += count;
  if (final != BKDISPLAY_JOB_IDLE)
    {
      job->status.resources_held = owns && release < 0;
      job->status.release_error = release;
      job->status.error = ret;
      job->status.state = release < 0 ? BKDISPLAY_JOB_UNKNOWN : final;
      if (final == BKDISPLAY_JOB_DONE)
        memcpy(job->status.filename, selected.filename,
               sizeof(job->status.filename));
    }
  else
    {
      job->status.state = BKDISPLAY_JOB_RECEIVING;
    }

  pthread_mutex_unlock(&job->lock);
  return 1;
}

void bkdisplay_job_start_failed(struct bkdisplay_job_s *job, uint64_t id,
                                int error)
{
  if (pthread_mutex_lock(&job->lock) != 0) return;
  if (job->status.id == id && job->status.state == BKDISPLAY_JOB_QUEUED &&
      !job->executing)
    {
      job->status.state = BKDISPLAY_JOB_FAILED;
      job->status.error = error;
    }

  pthread_mutex_unlock(&job->lock);
}
