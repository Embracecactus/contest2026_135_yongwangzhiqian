/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_control_session.h"
#include <errno.h>
#include <string.h>

static void wipe(void *data, size_t size)
{
  volatile uint8_t *p = data;
  while (size-- != 0) *p++ = 0;
}
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put32(uint8_t *p, uint32_t n)
{ p[0] = n>>24; p[1] = n>>16; p[2] = n>>8; p[3] = n; }

static void status_unknown(struct bkcontrol_status_s *status)
{
  memset(status, 0xff, sizeof(*status));
  status->flags = 0;
  status->error = 0;
}

void bkcontrol_session_close(struct bkcontrol_session_s *s)
{
  if (s != NULL)
    {
      wipe(s, sizeof(*s));
    }
}

int bkcontrol_session_open(struct bkcontrol_session_s *s,
                           const uint8_t key[32], bkcontrol_execute_t execute,
                           void *context)
{
  uint8_t bits = 0;
  if (s == NULL || key == NULL || execute == NULL) return -EINVAL;
  if (s->open) return -EBUSY;
  for (size_t i = 0; i < 32; i++) bits |= key[i];
  if (bits == 0) return -EINVAL;
  memset(s, 0, sizeof(*s));
  memcpy(s->secret, key, 32);
  s->execute = execute;
  s->context = context;
  s->open = true;
  return 0;
}
int bkcontrol_session_set_ota_handler(struct bkcontrol_session_s *s, bkcontrol_ota_t ota)
{ if (!s || !s->open || s->authenticated) return -EINVAL; s->ota = ota; return 0; }
int bkcontrol_session_set_config_handler(struct bkcontrol_session_s *s, bkcontrol_config_t config)
{ if (!s || !s->open || s->authenticated) return -EINVAL; s->config = config; return 0; }

int bkcontrol_session_quiesce(struct bkcontrol_session_s *s)
{
  if (s == NULL || !s->open) return -ENOTCONN;
  if (!s->authenticated) return -EACCES;
  s->quiescing = true;
  return 0;
}

static void clear_record(struct bkcontrol_session_s *s)
{
  wipe(s->config_record, sizeof(s->config_record));
  s->ota_total = s->ota_received = s->record_kind = 0;
}

int bkcontrol_session_packet(struct bkcontrol_session_s *s, const uint8_t *p,
                             size_t size, uint8_t response[BKCONTROL_RESPONSE_SIZE])
{
  struct bkcontrol_status_s status;
  uint32_t command, payload, argument = 0;
  int ret = -EPROTO;
  status_unknown(&status);
  if (response != NULL) memset(response, 0, BKCONTROL_RESPONSE_SIZE);
  if (s == NULL || !s->open) return -ENOTCONN;
  if (p == NULL || response == NULL || size < 16 || size > BKCONTROL_REQUEST_MAX)
    goto fail;
  command = get32(p+4);
  payload = get32(p+12);
  if (memcmp(p, "SDC1", 4) || get32(p+8) != s->sequence ||
      s->sequence == UINT32_MAX || payload != size - 16) goto fail;
  if (!s->authenticated)
    {
      uint8_t difference = 0;
      if (command != BKCONTROL_AUTH || payload != 32) goto fail;
      for (size_t i = 0; i < 32; i++) difference |= s->secret[i] ^ p[16+i];
      wipe(s->secret, sizeof(s->secret));
      if (difference != 0) { ret = -EACCES; goto fail; }
      s->authenticated = true;
      ret = 0;
    }
  else
    {
      if (command < BKCONTROL_STATUS || command > BKCONTROL_CONFIG_CANCEL) goto fail;
      if (command >= BKCONTROL_CONFIG_READ)
        {
          if (command == BKCONTROL_CONFIG_READ)
            {
              if (payload != 4 && payload != 20) goto fail;
              argument = get32(p + 16);
              if (payload == 20 &&
                  (argument >> 16) != BKCONTROL_CONFIG_RESET_TRANSFER)
                goto fail;
            }
          if (s->quiescing && command == BKCONTROL_CONFIG_READ &&
              (argument >> 16) != BKCONTROL_CONFIG_CAPABILITIES &&
              (argument >> 16) != BKCONTROL_CONFIG_SETTINGS &&
              (argument >> 16) != BKCONTROL_CONFIG_RESET_TRANSFER &&
              (argument >> 16) != BKCONTROL_CONFIG_FOCUS &&
              (argument >> 16) != BKCONTROL_CONFIG_EXPRESSION_TRIAL &&
              (argument >> 16) != BKCONTROL_CONFIG_NFC_BINDINGS)
            { ret = -EBUSY; goto config_done; }
          if (s->config == NULL) { ret = -ENOTSUP; goto config_done; }
          if (command == BKCONTROL_CONFIG_READ)
            {
              if ((argument >> 16) == BKCONTROL_CONFIG_CAPABILITIES)
                {
                  ret = -ERANGE;
                  if ((argument & 0xffffu) == 0)
                    {
                      status.config_total = 12;
                      memset(status.config_chunk, 0, sizeof(status.config_chunk));
                      memcpy(status.config_chunk, "CAP1", 4);
                      put32(status.config_chunk + 4, 1);
                      put32(status.config_chunk + 8, BKCONTROL_CONFIG_APPEND_MAX);
                      ret = 0;
                    }
                  goto config_done;
                }
              ret = s->config(s->context, (enum bkcontrol_command_e)command,
                  argument >> 16, argument & 0xffffu,
                  payload == 20 ? p + 20 : NULL, payload == 20 ? 16 : 0,
                  &status);
            }
          else if (command == BKCONTROL_CONFIG_BEGIN)
            {
              uint32_t kind;
              if (payload != 8) goto fail;
              kind = get32(p + 16);
              argument = get32(p + 20);
              if (kind == 0 || kind > 0xffffu || argument == 0 ||
                  argument > BKCONTROL_CONFIG_RECORD_MAX) goto fail;
              if (s->quiescing || s->ota_total != 0)
                { ret = -EBUSY; goto config_done; }
              /* The callback validates supported kind/size before accepting
               * any bytes. It must not mutate persistent state on BEGIN. */
              ret = s->config(s->context, (enum bkcontrol_command_e)command,
                  kind, 0, NULL, argument, &status);
              if (!ret)
                {
                  s->record_kind = 0x10000u | kind;
                  s->ota_total = argument;
                  s->ota_received = 0;
                }
            }
          else if (command == BKCONTROL_CONFIG_APPEND)
            {
              if (payload == 0 || payload > BKCONTROL_CONFIG_APPEND_MAX) goto fail;
              if ((s->record_kind & 0x10000u) == 0 || s->ota_total == 0)
                { ret = -EBUSY; goto config_done; }
              if (s->ota_received > s->ota_total ||
                  payload > s->ota_total - s->ota_received) goto fail;
              if (s->quiescing) { ret = -EBUSY; goto config_done; }
              memcpy(s->config_record + s->ota_received, p + 16, payload);
              s->ota_received += payload;
              /* ACK only the accepted bytes. A full product STATUS here
               * performs unrelated Media IPC for every fragment.
               * BEGIN/APPLY validate admission; READ confirms the result. */
              ret = 0;
            }
          else if (command == BKCONTROL_CONFIG_APPLY)
            {
              if (payload != 0) goto fail;
              if ((s->record_kind & 0x10000u) == 0 || s->ota_total == 0)
                { ret = -EBUSY; goto config_done; }
              if (s->ota_received != s->ota_total)
                { ret = -ENODATA; goto config_done; }
              if (s->quiescing) { ret = -EBUSY; goto config_done; }
              ret = s->config(s->context, (enum bkcontrol_command_e)command,
                  s->record_kind & 0xffffu, 0,
                  s->config_record, s->ota_total,
                  &status);
              /* Every apply consumes this staging transaction, including a
               * rejected request. Reconnection never replays its bytes. */
              clear_record(s);
            }
          else
            {
              if (payload != 0) goto fail;
              if (s->ota_total != 0 && (s->record_kind & 0x10000u) == 0)
                { ret = -EBUSY; goto config_done; }
              clear_record(s);
              ret = 0;
            }
config_done:
          if (ret > 0) ret = -EIO;
          if (ret < 0) status_unknown(&status);
        }
      else if (command >= BKCONTROL_OTA_BEGIN)
        {
          if (command == BKCONTROL_OTA_BEGIN)
            {
              if (payload != 4) goto fail;
              argument = get32(p+16);
              if (argument < 44 || argument > sizeof(s->ota_record)) goto fail;
              if (s->ota == NULL) { ret = -ENOTSUP; goto ota_done; }
              if (s->quiescing || s->ota_total != 0)
                { ret = -EBUSY; goto ota_done; }
              s->ota_total = argument; s->ota_received = 0; s->record_kind = 1; ret = 0;
            }
          else if (command == BKCONTROL_OTA_APPEND)
            {
              if (s->record_kind != 1) { ret = -EBUSY; goto ota_done; }
              if (payload == 0 || payload > 32 || s->ota_total == 0 ||
                  s->ota_received > s->ota_total ||
                  payload > s->ota_total-s->ota_received) goto fail;
              if (s->quiescing) { ret = -EBUSY; goto ota_done; }
              memcpy(s->ota_record+s->ota_received, p+16, payload); s->ota_received += payload; ret = 0;
            }
          else if (command == BKCONTROL_OTA_START)
            {
              if (s->record_kind != 1) { ret = -EBUSY; goto ota_done; }
              if (payload != 0 || s->ota == NULL || s->ota_total == 0 || s->ota_received != s->ota_total) goto fail;
              if (s->quiescing) { ret = -EBUSY; goto ota_done; }
              ret = s->ota(s->context, (enum bkcontrol_command_e)command, s->ota_record, s->ota_total, &status);
              if (!ret) clear_record(s);
            }
          else if (command == BKCONTROL_OTA_STATUS || command == BKCONTROL_OTA_CANCEL)
            {
              if (payload != 0) goto fail;
              if (s->ota == NULL) { ret = -ENOTSUP; goto ota_done; }
              ret = s->ota(s->context, (enum bkcontrol_command_e)command, NULL, 0, &status);
              if (command == BKCONTROL_OTA_CANCEL && !ret && s->record_kind == 1)
                clear_record(s);
            }
          else goto fail;
ota_done:  if (ret > 0) ret = -EIO;
        }
      else
        {
      if (command == BKCONTROL_VOLUME || command == BKCONTROL_PERSONA || command == BKCONTROL_MEMORY_SET)
        {
          if (payload != 4) goto fail;
          argument = get32(p+16);
          if (argument > (command == BKCONTROL_VOLUME ? 100u : command == BKCONTROL_PERSONA ? 4u : 1u)) goto fail;
        }
      else if (payload != 0) goto fail;
      ret = s->quiescing && command != BKCONTROL_STATUS &&
            command != BKCONTROL_INFO && command != BKCONTROL_CANCEL ? -EBUSY :
            s->execute(s->context, (enum bkcontrol_command_e)command, argument, &status);
      if (ret > 0) ret = -EIO;
      if (ret < 0)
        status_unknown(&status);
        }
    }
  memcpy(response, "SDC1", 4);
  put32(response+4, command | 0x80000000u);
  put32(response+8, s->sequence++);
  put32(response+12, 24);
  put32(response+16, (uint32_t)ret);
  if (command == BKCONTROL_CONFIG_READ)
    {
      put32(response + 20, ret == 0 ? status.config_total : 0);
      if (ret == 0) memcpy(response + 24, status.config_chunk, 16);
    }
  else if (command >= BKCONTROL_OTA_BEGIN && command <= BKCONTROL_OTA_CANCEL)
    {
      put32(response+20, status.ota.state);
      put32(response+24, status.ota.phase);
      put32(response+28, status.ota.progress);
      put32(response+32, status.ota.total);
      put32(response+36, (uint32_t)status.ota.result);
    }
  else if (command == BKCONTROL_INFO)
    {
      put32(response+20, status.device_info.major);
      put32(response+24, status.device_info.minor);
      put32(response+28, status.device_info.revision);
      put32(response+32, status.device_info.build);
      put32(response+36, status.device_info.security_counter);
    }
  else
    {
      put32(response+20, status.flags);
      put32(response+24, status.volume);
      put32(response+28, status.persona);
      put32(response+32, status.turn);
      put32(response+36, (uint32_t)status.error);
    }
  return 0;
fail:
  bkcontrol_session_close(s);
  return ret;
}
