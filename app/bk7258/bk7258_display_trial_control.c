/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_display_trial_control.h"
#include "bk7258_display_service.h"
#include <errno.h>
#include <string.h>

/* Serialized product owner; no credential, persistence or renderer ownership. */
static uint8_t g_trial_last_request[32];
static uint32_t g_trial_last_id;
static bool g_trial_have_request;
static uint32_t trial_get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint64_t trial_get64(const uint8_t *p)
{ return (uint64_t)trial_get32(p)<<32 | trial_get32(p+4); }
static void trial_put32(uint8_t *p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static void trial_put64(uint8_t *p, uint64_t n)
{ trial_put32(p,n>>32); trial_put32(p+4,n); }

int bkdisplay_trial_control(enum bkcontrol_command_e command, uint32_t offset,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status,
  uint64_t now_ms)
{
  static const char *const expressions[] = {
    "neutral", "happy", "shy", "sad", "surprised", "thinking",
    "listening", "speaking", "sleepy"
  };
  if (status == NULL) return -EINVAL;
  if (command == BKCONTROL_CONFIG_BEGIN)
    return size == 32 && offset == 0 ? 0 : -EMSGSIZE;
  struct bkdisplay_trial_status_s current;
  int ret = bk7258_display_trial_status(&current);
  if (ret < 0) return ret;
  if (command == BKCONTROL_CONFIG_READ)
    {
      if (offset != 0 && offset != 16) return -ERANGE;
      uint8_t data[32] = {'E','T','S','1'};
      uint64_t remaining = 0;
      if (current.state >= BKDISPLAY_TRIAL_PENDING && current.state <= BKDISPLAY_TRIAL_ACTIVE)
        remaining = now_ms == 0 ? UINT64_MAX :
                    now_ms < current.deadline_ms ? current.deadline_ms - now_ms : 0;
      trial_put32(data+4,current.state);
      trial_put32(data+8,current.id);
      trial_put32(data+12,(uint32_t)current.error);
      trial_put64(data+16,remaining);
      if (g_trial_have_request && current.id == g_trial_last_id)
        memcpy(data+24,g_trial_last_request+16,8);
      status->config_total = sizeof(data);
      memcpy(status->config_chunk,data+offset,16);
      return 0;
    }
  if (command != BKCONTROL_CONFIG_APPLY || offset || record == NULL ||
      size != 32 || memcmp(record,"ETC1",4) || trial_get32(record+28) ||
      !trial_get64(record+16)) return -EINVAL;
  uint32_t action = trial_get32(record+4);
  uint32_t expected = trial_get32(record+8);
  uint32_t duration = trial_get32(record+12);
  uint32_t expression = trial_get32(record+24);
  if ((action != 1 && action != 2) ||
      (action == 1 && (!duration || expression < 1 || expression > 9)) ||
      (action == 2 && (!expected || duration || expression))) return -EINVAL;
  if (g_trial_have_request && !memcmp(record+16,g_trial_last_request+16,8))
    {
      if (memcmp(record,g_trial_last_request,32)) return -EEXIST;
      return current.id == g_trial_last_id ? 0 : -ESTALE;
    }
  if (expected != current.id) return -ESTALE;
  uint32_t id = expected;
  ret = action == 1 ? bk7258_display_trial_checked(expressions[expression-1],
                         duration, expected, &id) : bk7258_display_cancel_trial(expected);
  if (ret < 0) return ret;
  memcpy(g_trial_last_request,record,32);
  g_trial_last_id = id;
  g_trial_have_request = true;
  return 0;
}
