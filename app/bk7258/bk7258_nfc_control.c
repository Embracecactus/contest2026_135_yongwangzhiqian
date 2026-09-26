/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "bk7258_nfc_control.h"
#include "bk7258_nfc_service.h"
#include <errno.h>
#include <string.h>
static uint64_t nfc_get64(const uint8_t *p)
{
  uint64_t value = 0;
  for (unsigned int i = 0; i < 8; i++) value = (value << 8) | p[i];
  return value;
}
static uint32_t nfc_get32(const uint8_t *p)
{
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}
static void nfc_put64(uint8_t *p, uint64_t value)
{
  for (int i = 7; i >= 0; i--) { p[i] = value; value >>= 8; }
}
static void nfc_put32(uint8_t *p, uint32_t value)
{
  for (int i = 3; i >= 0; i--) { p[i] = value; value >>= 8; }
}
int bknfc_control(enum bkcontrol_command_e command, uint32_t offset,
                  const uint8_t *record, size_t size,
                  struct bkcontrol_status_s *status)
{
  if (!status) return -EINVAL;
  if (command == BKCONTROL_CONFIG_READ)
    {
      uint8_t data[112] = {'N', 'C', 'S', '1'};
      struct bknfc_job_status_s job;
      if (offset % 16 || offset >= sizeof(data)) return -ERANGE;
      bk7258_nfc_job_status(&job);
      nfc_put32(data + 4, job.phase);
      nfc_put32(data + 8, (uint32_t)job.error);
      nfc_put64(data + 16, job.operation);
      nfc_put64(data + 24, job.revision);
      nfc_put64(data + 32, job.operation_floor);
      for (unsigned int i = 0; i < 8; i++) nfc_put64(data + 48 + i * 8, job.durations[i]);
      status->config_total = sizeof(data);
      memcpy(status->config_chunk, data + offset, sizeof(status->config_chunk));
      return 0;
    }
  if (command == BKCONTROL_CONFIG_BEGIN) return size == 40 ? 0 : -EMSGSIZE;
  if (command != BKCONTROL_CONFIG_APPLY || offset || !record || size != 40 ||
      memcmp(record, "NCF1", 4) || nfc_get32(record + 12)) return -EINVAL;
  struct bknfc_job_request_s request = {
    .action = nfc_get32(record + 4), .slot = nfc_get32(record + 8),
    .revision = nfc_get64(record + 16), .operation = nfc_get64(record + 24),
    .duration_ms = nfc_get64(record + 32)
  };
  if (request.action == 4)
    {
      if (request.slot || request.revision || request.duration_ms) return -EINVAL;
      return bk7258_nfc_job_cancel(request.operation);
    }
  return bk7258_nfc_job_submit(&request);
}
