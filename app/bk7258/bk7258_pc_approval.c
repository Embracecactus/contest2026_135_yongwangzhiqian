/****************************************************************************
 * app/bk7258/bk7258_pc_approval.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_approval.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char g_tag_context[] = "SHANIU-PC-APPROVAL-v1";

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t read_be(const uint8_t *data, unsigned int size)
{
  uint32_t value = 0;
  while (size--)
    {
      value = (value << 8) | *data++;
    }

  return value;
}

static void write_be(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static bool nonzero(const uint8_t *data, size_t size)
{
  uint8_t bits = 0;
  while (size--)
    {
      bits |= *data++;
    }

  return bits != 0;
}

static bool same(const uint8_t *a, const uint8_t *b, size_t size)
{
  uint8_t diff = 0;
  while (size--)
    {
      diff |= *a++ ^ *b++;
    }

  return diff == 0;
}

/* Well-formed shortest-form UTF-8 without characters that can hide or
 * reorder what the phone shows next to the approval buttons.
 */

static bool text_valid(const uint8_t *text, size_t size)
{
  size_t i = 0;

  while (i < size)
    {
      uint32_t code = text[i];
      unsigned int extra;
      uint32_t minimum;

      if (code < 0x80)
        {
          if ((code < 0x20 && code != '\t' && code != '\n') || code == 0x7f)
            {
              return false;
            }

          i++;
          continue;
        }

      if ((code & 0xe0) == 0xc0)
        {
          extra = 1; minimum = 0x80; code &= 0x1f;
        }
      else if ((code & 0xf0) == 0xe0)
        {
          extra = 2; minimum = 0x800; code &= 0x0f;
        }
      else if ((code & 0xf8) == 0xf0)
        {
          extra = 3; minimum = 0x10000; code &= 0x07;
        }
      else
        {
          return false;
        }

      if (extra > size - i - 1)
        {
          return false;
        }

      for (unsigned int j = 1; j <= extra; j++)
        {
          if ((text[i + j] & 0xc0) != 0x80)
            {
              return false;
            }

          code = (code << 6) | (text[i + j] & 0x3f);
        }

      if (code < minimum || code > 0x10ffff ||
          (code >= 0xd800 && code <= 0xdfff) ||
          (code >= 0x80 && code < 0xa0) ||
          code == 0x061c || code == 0x200e || code == 0x200f ||
          (code >= 0x202a && code <= 0x202e) ||
          (code >= 0x2066 && code <= 0x2069))
        {
          return false;
        }

      i += extra + 1;
    }

  return true;
}

static int sha256_parts(const uint8_t *a, size_t a_size, const uint8_t *b,
                        size_t b_size, uint8_t out[32])
{
  mbedtls_sha256_context context;
  int ret;

  mbedtls_sha256_init(&context);
  ret = mbedtls_sha256_starts(&context, 0);
  if (ret == 0 && a_size)
    {
      ret = mbedtls_sha256_update(&context, a, a_size);
    }

  if (ret == 0 && b_size)
    {
      ret = mbedtls_sha256_update(&context, b, b_size);
    }

  if (ret == 0)
    {
      ret = mbedtls_sha256_finish(&context, out);
    }

  mbedtls_sha256_free(&context);
  return ret == 0 ? 0 : -EIO;
}

static int approval_tag(const uint8_t key[32], const uint8_t *message,
                        size_t size, uint8_t out[32])
{
  uint8_t pad[64];
  uint8_t inner[32];
  int ret;

  memset(pad, 0x36, sizeof(pad));
  for (unsigned int i = 0; i < 32; i++)
    {
      pad[i] ^= key[i];
    }

  ret = sha256_parts(pad, sizeof(pad), message, size, inner);
  if (ret == 0)
    {
      for (unsigned int i = 0; i < sizeof(pad); i++)
        {
          pad[i] ^= 0x36 ^ 0x5c;
        }

      ret = sha256_parts(pad, sizeof(pad), inner, sizeof(inner), out);
    }

  mbedtls_platform_zeroize(pad, sizeof(pad));
  mbedtls_platform_zeroize(inner, sizeof(inner));
  return ret;
}

static void wipe(struct bkpc_approval_s *state)
{
  mbedtls_platform_zeroize(state->record, sizeof(state->record));
  mbedtls_platform_zeroize(state->digest, sizeof(state->digest));
  state->size = 0;
  state->state = BKPC_APPROVAL_NONE;
  state->deadline = 0;
  state->ttl = 0;
  state->alerted = false;
  state->alerted_at = 0;
}

static uint32_t remaining(const struct bkpc_approval_s *state, uint64_t now)
{
  if (state->state != BKPC_APPROVAL_PENDING || now >= state->deadline)
    {
      return 0;
    }

  return (uint32_t)(state->deadline - now);
}

static int read_chunk(const uint8_t *view, size_t total, uint32_t offset,
                      struct bkcontrol_status_s *status)
{
  size_t count;

  if (status == NULL || offset > total || (offset == total && total != 0))
    {
      return -EINVAL;
    }

  count = total - offset;
  if (count > sizeof(status->config_chunk))
    {
      count = sizeof(status->config_chunk);
    }

  status->config_total = (uint32_t)total;
  memset(status->config_chunk, 0, sizeof(status->config_chunk));
  memcpy(status->config_chunk, view + offset, count);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bkpc_approval_bind(struct bkpc_approval_s *state, uint64_t binding,
                        uint64_t grant, const uint8_t client[16])
{
  uint8_t zero[16] = {0};

  if (client == NULL || binding == 0 || grant == 0)
    {
      binding = 0;
      grant = 0;
      client = zero;
    }

  if (state->binding != binding || state->grant != grant ||
      memcmp(state->client, client, sizeof(state->client)))
    {
      wipe(state);
      state->binding = binding;
      state->grant = grant;
      memcpy(state->client, client, sizeof(state->client));
    }
}

void bkpc_approval_step(struct bkpc_approval_s *state, uint64_t now)
{
  if (state->state == BKPC_APPROVAL_PENDING && now >= state->deadline)
    {
      state->state = BKPC_APPROVAL_EXPIRED;
    }
}

void bkpc_approval_cancel(struct bkpc_approval_s *state)
{
  if (state->state == BKPC_APPROVAL_PENDING)
    {
      state->state = BKPC_APPROVAL_CANCELED;
    }
}

bool bkpc_approval_pending(const struct bkpc_approval_s *state)
{
  return state->state == BKPC_APPROVAL_PENDING;
}

int bkpc_approval_request(struct bkpc_approval_s *state, const uint8_t *record,
                          size_t size, uint64_t now)
{
  uint32_t ttl;
  size_t agent;
  size_t target;
  size_t operation;

  if (record == NULL)
    {
      return -EINVAL;
    }

  if (state->binding == 0)
    {
      return -EACCES;
    }

  bkpc_approval_step(state, now);
  if (size == BKPC_APPROVAL_CANCEL_SIZE && !memcmp(record, "APC1", 4))
    {
      if (state->size == 0 || !same(state->record + 4, record + 4, 16))
        {
          return -ENOENT;
        }

      if (state->state == BKPC_APPROVAL_PENDING)
        {
          state->state = BKPC_APPROVAL_CANCELED;
          return 0;
        }

      return state->state == BKPC_APPROVAL_CANCELED ? 0 : -EALREADY;
    }

  if (size < BKPC_APPROVAL_HEADER + 2 || size > BKPC_APPROVAL_RECORD_MAX ||
      memcmp(record, "APR1", 4) || !nonzero(record + 4, 16))
    {
      return -EINVAL;
    }

  ttl = read_be(record + 20, 4);
  agent = read_be(record + 24, 2);
  target = read_be(record + 26, 2);
  operation = read_be(record + 28, 2);
  if (ttl < BKPC_APPROVAL_TTL_MIN || ttl > BKPC_APPROVAL_TTL_MAX ||
      agent < 1 || agent > BKPC_APPROVAL_AGENT_MAX ||
      target > BKPC_APPROVAL_TARGET_MAX ||
      operation < 1 || operation > BKPC_APPROVAL_OPERATION_MAX ||
      read_be(record + 30, 2) != 0 ||
      BKPC_APPROVAL_HEADER + agent + target + operation != size ||
      !text_valid(record + BKPC_APPROVAL_HEADER, agent) ||
      !text_valid(record + BKPC_APPROVAL_HEADER + agent, target) ||
      !text_valid(record + BKPC_APPROVAL_HEADER + agent + target, operation))
    {
      return -EINVAL;
    }

  if (state->size != 0 && same(state->record + 4, record + 4, 16))
    {
      /* An exact retry is idempotent; a terminal request is never reopened
       * and an ID never names different bytes.
       */

      return state->size == size && same(state->record, record, size) ?
             0 : -EEXIST;
    }

  if (state->state == BKPC_APPROVAL_PENDING)
    {
      return -EBUSY;
    }

  if (now > UINT64_MAX - ttl)
    {
      return -EOVERFLOW;
    }

  wipe(state);
  if (sha256_parts(record, size, NULL, 0, state->digest) < 0)
    {
      wipe(state);
      return -EIO;
    }

  memcpy(state->record, record, size);
  state->size = size;
  state->ttl = ttl;
  state->deadline = now + ttl;
  state->state = BKPC_APPROVAL_PENDING;
  return 0;
}

int bkpc_approval_decide(struct bkpc_approval_s *state, const uint8_t *record,
                         size_t size, uint64_t now)
{
  uint32_t decision;
  uint32_t outcome;

  if (record == NULL || size != BKPC_APPROVAL_DECISION_SIZE ||
      memcmp(record, "APD1", 4))
    {
      return -EINVAL;
    }

  decision = read_be(record + 4, 4);
  if (decision != 1 && decision != 2)
    {
      return -EINVAL;
    }

  if (state->binding == 0 || state->size == 0)
    {
      return -ENOENT;
    }

  bkpc_approval_step(state, now);
  if (!same(state->record + 4, record + 8, 16))
    {
      return -ESTALE;
    }

  if (!same(state->digest, record + 24, 32))
    {
      return -EBADMSG;
    }

  outcome = decision == 1 ? BKPC_APPROVAL_APPROVED : BKPC_APPROVAL_REJECTED;
  if (state->state != BKPC_APPROVAL_PENDING)
    {
      if (state->state == outcome)
        {
          return 0;
        }

      return state->state == BKPC_APPROVAL_EXPIRED ? -ETIMEDOUT : -EALREADY;
    }

  state->state = outcome;
  return 0;
}

int bkpc_approval_receipt(const struct bkpc_approval_s *state,
                          const uint8_t key[32],
                          uint8_t out[BKPC_APPROVAL_RECEIPT_SIZE],
                          uint64_t now)
{
  uint8_t message[sizeof(g_tag_context) - 1 + 16 + 32 + 4];
  int ret = 0;

  memset(out, 0, BKPC_APPROVAL_RECEIPT_SIZE);
  memcpy(out, "APS1", 4);
  write_be(out + 4, state->state);
  if (state->size == 0)
    {
      return 0;
    }

  memcpy(out + 8, state->record + 4, 16);
  memcpy(out + 24, state->digest, 32);
  write_be(out + 56, remaining(state, now));
  if (state->state == BKPC_APPROVAL_APPROVED ||
      state->state == BKPC_APPROVAL_REJECTED)
    {
      if (key == NULL || !nonzero(key, 32))
        {
          memset(out, 0, BKPC_APPROVAL_RECEIPT_SIZE);
          return -ENOKEY;
        }

      memcpy(message, g_tag_context, sizeof(g_tag_context) - 1);
      memcpy(message + sizeof(g_tag_context) - 1, out + 8, 48);
      write_be(message + sizeof(message) - 4, state->state);
      ret = approval_tag(key, message, sizeof(message), out + 64);
      mbedtls_platform_zeroize(message, sizeof(message));
      if (ret < 0)
        {
          memset(out, 0, BKPC_APPROVAL_RECEIPT_SIZE);
        }
    }

  return ret;
}

int bkpc_approval_pc_control(struct bkpc_approval_s *state,
                             enum bkcontrol_command_e command,
                             uint32_t offset, const uint8_t *record,
                             size_t size, struct bkcontrol_status_s *status,
                             const uint8_t key[32], uint64_t now)
{
  uint8_t view[BKPC_APPROVAL_RECEIPT_SIZE];
  int ret;

  if (command == BKCONTROL_CONFIG_READ)
    {
      if (size != 0 || offset % 16)
        {
          return -EINVAL;
        }

      bkpc_approval_step(state, now);
      ret = bkpc_approval_receipt(state, key, view, now);
      if (ret == 0)
        {
          ret = read_chunk(view, sizeof(view), offset, status);
        }

      mbedtls_platform_zeroize(view, sizeof(view));
      return ret;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return size == BKPC_APPROVAL_CANCEL_SIZE ||
             (size >= BKPC_APPROVAL_HEADER + 2 &&
              size <= BKPC_APPROVAL_RECORD_MAX) ? 0 : -EINVAL;
    }

  return command == BKCONTROL_CONFIG_APPLY ?
         bkpc_approval_request(state, record, size, now) : -ENOTSUP;
}

int bkpc_approval_owner_control(struct bkpc_approval_s *state,
                                enum bkcontrol_command_e command,
                                uint32_t offset, const uint8_t *record,
                                size_t size, struct bkcontrol_status_s *status,
                                uint64_t now)
{
  if (command == BKCONTROL_CONFIG_READ)
    {
      uint8_t header[BKPC_APPROVAL_VIEW_HEADER];
      size_t total;

      if (size != 0)
        {
          return -EINVAL;
        }

      bkpc_approval_step(state, now);
      total = BKPC_APPROVAL_VIEW_HEADER + state->size;
      if (status == NULL || offset >= total || offset % 16)
        {
          return -EINVAL;
        }

      if (offset == 0)
        {
          memcpy(header, "APV1", 4);
          write_be(header + 4, state->state);
          write_be(header + 8, (uint32_t)state->size);
          write_be(header + 12, remaining(state, now));
          if (read_chunk(header, sizeof(header), 0, status) < 0)
            {
              return -EINVAL;
            }
        }
      else if (read_chunk(state->record, state->size,
                          offset - BKPC_APPROVAL_VIEW_HEADER, status) < 0)
        {
          return -EINVAL;
        }

      status->config_total = (uint32_t)total;
      return 0;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return size == BKPC_APPROVAL_DECISION_SIZE ? 0 : -EINVAL;
    }

  return command == BKCONTROL_CONFIG_APPLY ?
         bkpc_approval_decide(state, record, size, now) : -ENOTSUP;
}

unsigned int bkpc_approval_visual(const struct bkpc_approval_s *state,
                                  uint64_t now, bool available)
{
  uint64_t left;

  if (!available || state->state != BKPC_APPROVAL_PENDING ||
      now >= state->deadline || state->ttl == 0)
    {
      return 0;
    }

  left = state->deadline - now;
  return (7u << 8) | (unsigned int)((left * 32 + state->ttl - 1) /
                                    state->ttl);
}

bool bkpc_approval_take_alert(struct bkpc_approval_s *state, uint64_t now,
                              bool available)
{
  if (!available || state->state != BKPC_APPROVAL_PENDING ||
      now >= state->deadline)
    {
      return false;
    }

  if (state->alerted && now >= state->alerted_at &&
      now - state->alerted_at < BKPC_APPROVAL_REMIND_MS)
    {
      return false;
    }

  state->alerted = true;
  state->alerted_at = now;
  return true;
}
