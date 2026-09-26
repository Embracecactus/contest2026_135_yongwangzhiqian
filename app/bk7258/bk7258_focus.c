/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus.h"
#include "bk7258_focus_pixels.h"
#include <errno.h>
#include <string.h>

static struct
{
  unsigned state;
  uint64_t revision, duration, remaining, deadline, last_time;
  struct bkfocus_request_s last_request;
  bool have_request;
} g_focus;

static uint64_t get64(const uint8_t *p)
{ uint64_t n = 0; for (unsigned i = 0; i < 8; i++) n = (n << 8) | p[i]; return n; }
static void put64(uint8_t *p, uint64_t n)
{ for (int i = 7; i >= 0; i--) { p[i] = n; n >>= 8; } }
static uint64_t remaining(uint64_t now)
{
  if (now < g_focus.last_time) now = g_focus.last_time;
  return g_focus.state == 1 ? (now < g_focus.deadline ? g_focus.deadline - now : 0) :
         g_focus.state == 2 ? g_focus.remaining : 0;
}
unsigned bkfocus_visual(uint64_t now)
{
  if (g_focus.state < 1 || g_focus.state > 3) return 0;
  return (g_focus.state << 8) | bkfocus_segments(g_focus.duration, remaining(now));
}
int bkfocus_step(uint64_t now)
{
  if (now < g_focus.last_time) return 0;
  g_focus.last_time = now;
  if (g_focus.state != 1 || now < g_focus.deadline) return 0;
  g_focus.state = 3;
  g_focus.remaining = 0;
  if (g_focus.revision != UINT64_MAX) g_focus.revision++;
  return 1;
}
void bkfocus_cancel(void)
{
  if (g_focus.state != 1 && g_focus.state != 2) return;
  g_focus.state = 4;
  g_focus.remaining = 0;
  g_focus.have_request = false;
  if (g_focus.revision != UINT64_MAX) g_focus.revision++;
}
int bkfocus_snapshot(struct bkfocus_snapshot_s *snapshot, uint64_t now)
{
  if (snapshot == NULL) return -EINVAL;
  snapshot->state = g_focus.state;
  snapshot->revision = g_focus.revision;
  snapshot->remaining_ms = remaining(now);
  snapshot->duration_ms = g_focus.duration;
  return 0;
}

int bkfocus_execute(const struct bkfocus_request_s *request, uint64_t now)
{
  unsigned action;
  uint64_t duration;

  if (request == NULL || request->action < 1 || request->action > 4 ||
      request->operation == 0) return -EINVAL;
  action = request->action;
  duration = request->duration_ms;
  if ((action == 1 && !duration) || (action != 1 && duration)) return -EINVAL;
  if (g_focus.have_request &&
      request->action == g_focus.last_request.action &&
      request->revision == g_focus.last_request.revision &&
      request->operation == g_focus.last_request.operation &&
      request->duration_ms == g_focus.last_request.duration_ms) return 0;
  if (request->revision != g_focus.revision) return -ESTALE;
  if (g_focus.revision == UINT64_MAX) return -EOVERFLOW;
  if (now < g_focus.last_time) return -EAGAIN;
  if (action == 1)
    {
      if (g_focus.state == 1 || g_focus.state == 2) return -EBUSY;
      if (duration > UINT64_MAX - now) return -EOVERFLOW;
      g_focus.duration = duration;
      g_focus.deadline = now + duration;
      g_focus.state = 1;
    }
  else if (action == 2)
    {
      if (g_focus.state != 1) return -EINVAL;
      if (now >= g_focus.deadline) return -EAGAIN; /* Owner step completes first. */
      g_focus.remaining = remaining(now);
      g_focus.state = 2;
    }
  else if (action == 3)
    {
      if (g_focus.state != 2) return -EINVAL;
      if (g_focus.remaining > UINT64_MAX - now) return -EOVERFLOW;
      g_focus.deadline = now + g_focus.remaining;
      g_focus.state = 1;
    }
  else
    {
      if (g_focus.state != 1 && g_focus.state != 2) return -EINVAL;
      g_focus.remaining = 0;
      g_focus.state = 4;
    }
  g_focus.last_time = now;
  g_focus.revision++;
  g_focus.last_request = *request;
  g_focus.have_request = true;
  return 0;
}

int bkfocus_control(enum bkcontrol_command_e command, uint32_t offset,
                    const uint8_t *record, size_t size,
                    struct bkcontrol_status_s *status, uint64_t now)
{
  struct bkfocus_request_s request;

  if (!status) return -EINVAL;
  if (command == BKCONTROL_CONFIG_READ)
    {
      struct bkfocus_snapshot_s snapshot;
      uint8_t data[32] = {'F', 'O', 'S', '1'};
      if (offset != 0 && offset != 16) return -ERANGE;
      (void)bkfocus_snapshot(&snapshot, now);
      data[7] = snapshot.state;
      put64(data + 8, snapshot.revision);
      put64(data + 16, snapshot.remaining_ms);
      put64(data + 24, snapshot.duration_ms);
      status->config_total = sizeof(data);
      memcpy(status->config_chunk, data + offset, 16);
      return 0;
    }
  if (command == BKCONTROL_CONFIG_BEGIN) return size == 32 ? 0 : -EMSGSIZE;
  if (command != BKCONTROL_CONFIG_APPLY || !record || size != 32 ||
      memcmp(record, "FOC1", 4) || record[4] || record[5] || record[6])
    return -EINVAL;
  request.action = record[7];
  request.revision = get64(record + 8);
  request.operation = get64(record + 16);
  request.duration_ms = get64(record + 24);
  return bkfocus_execute(&request, now);
}
