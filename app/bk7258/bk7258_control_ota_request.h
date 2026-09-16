/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CONTROL_OTA_REQUEST_H
#define __APP_BK7258_CONTROL_OTA_REQUEST_H
#include <stddef.h>
#include <stdint.h>
#define BKCONTROL_OTA_URL_MAX 255u
#define BKCONTROL_OTA_CA_MAX 3072u
#define BKCONTROL_OTA_RECORD_MAX 3371u
struct bkcontrol_ota_request_s {
  char url[BKCONTROL_OTA_URL_MAX + 1u];
  char ca_pem[BKCONTROL_OTA_CA_MAX + 1u];
  uint8_t ipv4[4];
  uint8_t catalog_sha256[32];
};
int bkcontrol_ota_request_parse(const uint8_t *record, size_t size,
                                struct bkcontrol_ota_request_s *out);
#endif
