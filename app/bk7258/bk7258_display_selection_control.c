/****************************************************************************
 * app/bk7258/bk7258_display_selection_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <string.h>

#include "bk7258_display_selection_control.h"
#include "bk7258_display_service.h"

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

static void put32(uint8_t *p, uint32_t value)
{
  for (int i = 3; i >= 0; i--)
    {
      p[i] = value;
      value >>= 8;
    }
}

static void put64(uint8_t *p, uint64_t value)
{
  put32(p, value >> 32);
  put32(p + 4, value);
}

static bool nonzero(const uint8_t *p, size_t size)
{
  uint8_t bits = 0;
  for (size_t i = 0; i < size; i++)
    {
      bits |= p[i];
    }

  return bits != 0;
}

static bool filename_valid(const uint8_t *p)
{
  size_t n = 0;

  if (p[0] < 'a' || p[0] > 'z')
    {
      return false;
    }

  while (n < 40 && p[n])
    {
      uint8_t c = p[n++];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-'))
        {
          return false;
        }
    }

  return n > 5 && n < 40 && !memcmp(p + n - 5, ".bkep", 5) &&
         !nonzero(p + n, 40 - n);
}

static int selection_read(struct bkselection_control_s *s, uint32_t offset,
                          const uint8_t *query, size_t size,
                          struct bkcontrol_status_s *status)
{
  struct bkdisplay_selection_status_s job;
  uint8_t *out = s->snapshot;
  int ret;

  if (query == NULL || size != 16 || !nonzero(query, 16) ||
      offset >= sizeof(s->snapshot) || (offset & 15))
    {
      return -EINVAL;
    }

  if (!s->captured || memcmp(query, s->query, 16))
    {
      if (offset != 0)
        {
          return -ESTALE;
        }

      if (s->sequence == UINT64_MAX)
        {
          return -EOVERFLOW;
        }

      ret = bk7258_display_selection_status(&job);
      if (ret < 0)
        {
          return ret;
        }

      memset(out, 0, sizeof(s->snapshot));
      memcpy(out, "ESS1", 4);
      put32(out + 4, job.state);
      memcpy(out + 8, s->epoch, 16);
      put32(out + 24, job.id);
      put32(out + 28, job.error);
      put32(out + 32, job.release_error);
      put32(out + 36, (job.version_known ? 1 : 0) |
                       (job.save_confirmed ? 2 : 0) |
                       (job.render_confirmed ? 4 : 0) |
                       (job.refresh ? 8 : 0) |
                       (job.recovery_pending ? 16 : 0));
      put64(out + 40, job.version.revision);
      put64(out + 48, job.expected_revision);
      if (s->accepted && job.id == s->id)
        {
          memcpy(out + 56, s->request + 24, 16);
        }

      memcpy(out + 72, job.version.filename, 40);
      put64(out + 112, ++s->sequence);
      memcpy(s->query, query, 16);
      s->captured = true;
    }

  status->config_total = sizeof(s->snapshot);
  memcpy(status->config_chunk, out + offset, 16);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkselection_control_bind(struct bkselection_control_s *s,
                             const uint8_t epoch[16])
{
  if (s == NULL || epoch == NULL || !nonzero(epoch, 16))
    {
      return -EINVAL;
    }

  if (s->bound)
    {
      return memcmp(s->epoch, epoch, 16) ? -ESTALE : 0;
    }

  memset(s, 0, sizeof(*s));
  memcpy(s->epoch, epoch, 16);
  s->bound = true;
  return 0;
}

void bkselection_control_invalidate(struct bkselection_control_s *s)
{
  if (s != NULL)
    {
      if (s->accepted)
        {
          /* Commit cannot be undone. The native job keeps its true result. */

          (void)bk7258_display_selection_cancel(s->id);
        }

      memset(s, 0, sizeof(*s));
    }
}

int bkselection_control(struct bkselection_control_s *s,
                        enum bkcontrol_command_e command, uint32_t offset,
                        const uint8_t *p, size_t size,
                        struct bkcontrol_status_s *status)
{
  struct bkdisplay_selection_status_s current;
  uint32_t action;
  uint32_t id;
  int ret;

  if (s == NULL || !s->bound)
    {
      return -EACCES;
    }

  if (status == NULL)
    {
      return -EINVAL;
    }

  if (command == BKCONTROL_CONFIG_READ)
    {
      return selection_read(s, offset, p, size, status);
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return offset || p ? -EINVAL : size == 96 ? 0 : -EMSGSIZE;
    }

  if (command != BKCONTROL_CONFIG_APPLY || offset || p == NULL ||
      size != 96 || memcmp(p, "ESC1", 4) || get32(p + 44) ||
      !nonzero(p + 24, 16))
    {
      return -EINVAL;
    }

  if (memcmp(p + 8, s->epoch, 16))
    {
      return -ESTALE;
    }

  action = get32(p + 4);
  if (action < 1 || action > 4 ||
      (action == 1 ? !filename_valid(p + 56) : nonzero(p + 48, 48)))
    {
      return -EINVAL;
    }

  ret = bk7258_display_selection_status(&current);
  if (ret < 0)
    {
      return ret;
    }

  if (s->accepted && !memcmp(p + 24, s->request + 24, 16))
    {
      if (memcmp(p, s->request, sizeof(s->request)))
        {
          return -EEXIST;
        }

      return current.id == s->id ? 0 : -ESTALE;
    }

  id = get32(p + 40);
  if (id != current.id)
    {
      return -ESTALE;
    }

  switch (action)
    {
      case 1:
        ret = bk7258_display_selection_request((const char *)p + 56,
                                                get64(p + 48), id, &id);
        break;
      case 2:
        ret = bk7258_display_selection_refresh(id, &id);
        break;
      case 3:
        ret = bk7258_display_selection_cancel(id);
        break;
      default:
        ret = bk7258_display_selection_recover(id);
        break;
    }

  if (ret == 0)
    {
      memcpy(s->request, p, sizeof(s->request));
      s->id = id;
      s->accepted = true;
    }

  return ret;
}
