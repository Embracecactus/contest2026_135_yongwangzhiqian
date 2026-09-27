/****************************************************************************
 * app/bk7258/bk7258_display_job_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <string.h>
#include <mbedtls/sha256.h>

#include "bk7258_display_job_control.h"
#include "bk7258_display_job_service.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}

static uint64_t get64(const uint8_t *p)
{
  return (uint64_t)get32(p) << 32 | get32(p + 4);
}

static void put32(uint8_t *p, uint32_t v)
{
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static void put64(uint8_t *p, uint64_t v)
{
  put32(p, v >> 32);
  put32(p + 4, v);
}

static bool nonzero(const uint8_t p[16])
{
  uint8_t value = 0;
  for (size_t i = 0; i < 16; i++) value |= p[i];
  return value != 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkpack_control_bind(struct bkpack_control_s *s, uint64_t binding,
                        uint64_t grant, const uint8_t client[16],
                        const uint8_t epoch[16])
{
  int ret;
  if (s == NULL || binding == 0 || grant == 0 || client == NULL ||
      epoch == NULL || !nonzero(client) || !nonzero(epoch)) return -EINVAL;
  if (s->bound)
    {
      return s->binding == binding && s->grant == grant &&
             !memcmp(s->client, client, 16) &&
             !memcmp(s->epoch, epoch, 16) ? 0 : -ESTALE;
    }

  /* No new authority may inherit a still-running old job. */

  ret = bk7258_display_job_quiesce(true);
  if (ret < 0) return ret;
  memset(s, 0, sizeof(*s));
  s->binding = binding;
  s->grant = grant;
  memcpy(s->client, client, 16);
  memcpy(s->epoch, epoch, 16);
  s->bound = true;
  return bk7258_display_job_quiesce(false);
}

void bkpack_control_invalidate(struct bkpack_control_s *s)
{
  if (s != NULL) memset(s, 0, sizeof(*s));
  (void)bk7258_display_job_quiesce(true);
}

int bkpack_control_apply(struct bkpack_control_s *s,
                         const uint8_t *p, size_t size, uint64_t now)
{
  struct bkdisplay_job_status_s job;
  uint8_t digest[32];
  uint64_t id;
  uint64_t created = 0;
  uint32_t op;
  uint32_t arg;
  uint32_t ttl;
  uint32_t bytes;
  int ret;

  if (s == NULL || !s->bound) return -EACCES;
  if (p == NULL || size < 64 || size > 64 + BKDISPLAY_UPLOAD_CHUNK_MAX ||
      memcmp(p, "RJI1", 4) || get32(p + 60) != 0 ||
      !nonzero(p + 24)) return -EINVAL;
  op = get32(p + 4);
  id = get64(p + 40);
  arg = get32(p + 48);
  ttl = get32(p + 52);
  bytes = get32(p + 56);
  if (op < 1 || op > 4 || bytes != size - 64 ||
      (op != 2 && bytes != 0) || (op == 2 && bytes == 0) ||
      (op != 1 && ttl != 0) || (op >= 3 && arg != 0) ||
      (op == 1 && (ttl == 0 || arg < 128 || arg > 32u * 1024 * 1024 ||
                    now == 0 || now > UINT64_MAX - ttl))) return -EINVAL;
  if (memcmp(p + 8, s->epoch, 16)) return -ESTALE;
  ret = bk7258_display_job_status(&job);
  if (ret < 0) return ret;

  if (op == 1)
    {
      if (s->id != 0 && !memcmp(p + 24, s->nonce, 16))
        {
          return id == s->previous && arg == s->total && ttl == s->ttl ?
                 s->begin_error : -EEXIST;
        }

      if (id != job.id) return -ESTALE;
      ret = bk7258_display_job_begin(s->binding, arg, now + ttl, &created);
      /* A task-create failure can allocate a job ID and publish FAILED.
       * Keep that receipt correlated even though the request returned error.
       */

      if (created != 0)
        {
          s->id = created;
          s->previous = id;
          s->total = arg;
          s->ttl = ttl;
          s->size = 0;
          s->begin_error = ret;
          memcpy(s->nonce, p + 24, 16);
        }

      return ret;
    }

  if (id == 0 || id != s->id || id != job.id ||
      job.binding != s->binding || memcmp(p + 24, s->nonce, 16))
    {
      return -ESTALE;
    }

  if (op == 4) return bk7258_display_job_cancel(s->binding, id);
  if (op == 3 && (job.state == BKDISPLAY_JOB_COMMIT_PENDING ||
                  job.state == BKDISPLAY_JOB_COMMITTING ||
                  job.state == BKDISPLAY_JOB_DONE)) return 0;
  if (job.state >= BKDISPLAY_JOB_COMMIT_PENDING) return -EALREADY;
  if (op == 3) return bk7258_display_job_finish(s->binding, id);
  ret = mbedtls_sha256(p + 64, bytes, digest, 0);
  if (ret != 0) return -EIO;
  if (s->size != 0 && arg == s->offset)
    {
      return bytes == s->size && !memcmp(digest, s->digest, 32) ?
             0 : -EEXIST;
    }

  if (s->size != 0 && arg < s->offset) return -ESTALE;
  ret = bk7258_display_job_append(s->binding, id, arg, p + 64, bytes);
  if (ret == 0)
    {
      s->offset = arg;
      s->size = bytes;
      memcpy(s->digest, digest, 32);
    }

  return ret;
}

int bkpack_control_read(struct bkpack_control_s *s, uint32_t offset,
                        const uint8_t *query, size_t size,
                        struct bkcontrol_status_s *status, uint64_t now)
{
  struct bkdisplay_job_status_s job;
  uint8_t *out;
  uint64_t remaining;
  int ret;

  if (s == NULL || !s->bound) return -EACCES;
  if (status == NULL || query == NULL || size != 16 || !nonzero(query) ||
      offset >= 128 || (offset & 15) != 0) return -EINVAL;
  if (!s->captured || memcmp(query, s->query, 16))
    {
      if (offset != 0) return -ESTALE;
      if (s->revision == UINT64_MAX) return -EOVERFLOW;
      ret = bk7258_display_job_status(&job);
      if (ret < 0) return ret;
      out = s->snapshot;
      memset(out, 0, sizeof(s->snapshot));
      memcpy(out, "RJS1", 4);
      memcpy(out + 8, s->epoch, 16);
      put64(out + 48, ++s->revision);
      put32(out + 72, 4); /* All receipts are volatile. */
      if (job.id != 0 && job.id == s->id && job.binding == s->binding)
        {
          put32(out + 4, job.state);
          memcpy(out + 24, s->nonce, 16);
          put64(out + 40, job.id);
          put32(out + 56, job.total);
          put32(out + 60, job.written);
          put32(out + 64, job.error);
          put32(out + 68, job.release_error);
          put32(out + 72, 4 | (job.resources_held ? 2 : 0));
          remaining = job.deadline_ms > now ? job.deadline_ms - now : 0;
          put32(out + 76, remaining > UINT32_MAX ? UINT32_MAX : remaining);
          memcpy(out + 80, job.filename, sizeof(job.filename));
        }
      else
        {
          /* Previous receipt belongs to another authority, but a new BEGIN
           * still needs the current service generation as its precondition.
           */

          put64(out + 40, job.id);
        }

      memcpy(s->query, query, 16);
      s->captured = true;
    }

  status->config_total = sizeof(s->snapshot);
  memcpy(status->config_chunk, s->snapshot + offset, 16);
  return 0;
}
