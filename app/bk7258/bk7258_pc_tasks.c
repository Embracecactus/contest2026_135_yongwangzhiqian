/****************************************************************************
 * app/bk7258/bk7258_pc_tasks.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_tasks.h"
#include <errno.h>
#include <string.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t read_be(const uint8_t *data, unsigned int size)
{
  uint64_t value = 0;
  while (size--)
    {
      value = (value << 8) | *data++;
    }

  return value;
}

static void write_be(uint8_t *data, uint64_t value, unsigned int size)
{
  while (size)
    {
      data[--size] = (uint8_t)value;
      value >>= 8;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bkpc_tasks_bind(struct bkpc_tasks_s *state, uint64_t binding,
                     uint64_t grant)
{
  if (state->binding != binding || state->grant != grant)
    {
      memset(state, 0, sizeof(*state));
      state->binding = binding;
      state->grant = grant;
      state->admitted = binding != 0 && grant != 0;
    }
}

void bkpc_tasks_step(struct bkpc_tasks_s *state, uint64_t now, bool admitted)
{
  state->admitted = admitted && state->binding != 0 && state->grant != 0;
  if (!state->admitted || now < state->observed ||
      (state->deadline != 0 && now >= state->deadline))
    {
      state->expired = true;
    }

  if (now >= state->observed)
    {
      state->observed = now;
    }
}

int bkpc_tasks_apply(struct bkpc_tasks_s *state, const void *record,
                     size_t size, uint64_t now)
{
  const uint8_t *data = record;
  uint8_t bits = 0;
  uint32_t action;
  uint32_t progress;
  uint64_t sequence;
  uint64_t previous;
  uint64_t ttl;
  bool same;
  bool expired;

  if (data == NULL || size != sizeof(state->last) ||
      memcmp(data, "PTE1", 4))
    {
      return -EINVAL;
    }

  action = read_be(data + 4, 4);
  sequence = read_be(data + 24, 8);
  ttl = read_be(data + 32, 4);
  progress = read_be(data + 36, 4);
  for (unsigned int i = 8; i < 24; i++)
    {
      bits |= data[i];
    }

  if (action < 1 || action > 5 || sequence == 0 || ttl == 0 || bits == 0 ||
      (progress > 100 && progress != UINT32_MAX) ||
      (action == 1 && progress != 0))
    {
      return -EINVAL;
    }

  if (now > UINT64_MAX - ttl)
    {
      return -EOVERFLOW;
    }

  if (state->binding == 0 || state->grant == 0)
    {
      return -EACCES;
    }

  if (!state->admitted)
    {
      return -ESHUTDOWN;
    }

  previous = read_be(state->last + 24, 8);
  if (sequence == previous)
    {
      return memcmp(data, state->last, size) == 0 ? 0 : -EEXIST;
    }

  if (sequence < previous)
    {
      return -ESTALE;
    }

  if (now < state->observed)
    {
      return -ETIMEDOUT;
    }

  same = memcmp(data + 8, state->last + 8, 16) == 0;
  expired = state->expired || now >= state->deadline;
  if (action == 1)
    {
      if (same)
        {
          return -EALREADY;
        }

      if (previous && state->last[7] < 3 && !expired)
        {
          return -EBUSY;
        }
    }
  else
    {
      if (!previous || !same)
        {
          return -ENOENT;
        }

      if (state->last[7] >= 3)
        {
          return -EALREADY;
        }

      if (expired)
        {
          return -ETIMEDOUT;
        }
    }

  /* Progress is coalesced by rejection, never queued. Terminal events bypass
   * this limit so a completed job is not held behind intermediate updates.
   */

  if (action == 2 && state->last[7] == 2 &&
      now - (state->deadline - read_be(state->last + 32, 4)) < 1000)
    {
      return -EAGAIN;
    }

  memcpy(state->last, data, size);
  state->observed = now;
  state->deadline = now + ttl;
  state->expired = false;
  return 0;
}

void bkpc_tasks_snapshot(const struct bkpc_tasks_s *state, uint8_t out[48],
                         uint64_t now)
{
  uint32_t flags = state->admitted ? 1 : 0;
  bool expired = state->expired || now < state->observed ||
                 now >= state->deadline;

  memset(out, 0, 48);
  memcpy(out, "PTS1", 4);
  memcpy(out + 4, state->last + 4, 28);
  if (state->deadline != 0)
    {
      if (expired)
        {
          flags |= 2;
        }
      else
        {
          write_be(out + 32, state->deadline - now, 8);
          if (state->last[7] >= 3)
            {
              flags |= 4;
            }
        }
    }

  write_be(out + 40, flags, 4);
  memcpy(out + 44, state->last + 36, 4);
}

int bkpc_tasks_control(struct bkpc_tasks_s *state,
                       enum bkcontrol_command_e command, uint32_t offset,
                       const uint8_t *record, size_t size,
                       struct bkcontrol_status_s *status, uint64_t now)
{
  if (command == BKCONTROL_CONFIG_READ)
    {
      uint8_t view[48];
      size_t count;
      if (offset > sizeof(view) || size != 0 || status == NULL)
        {
          return -EINVAL;
        }

      bkpc_tasks_snapshot(state, view, now);
      count = sizeof(view) - offset;
      if (count > sizeof(status->config_chunk))
        {
          count = sizeof(status->config_chunk);
        }

      status->config_total = sizeof(view);
      memset(status->config_chunk, 0, sizeof(status->config_chunk));
      memcpy(status->config_chunk, view + offset, count);
      return 0;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return size == sizeof(state->last) ? 0 : -EINVAL;
    }

  return command == BKCONTROL_CONFIG_APPLY ?
    bkpc_tasks_apply(state, record, size, now) : -ENOTSUP;
}
