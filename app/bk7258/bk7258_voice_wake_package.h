/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_WAKE_PACKAGE_H
#define __APP_BK7258_VOICE_WAKE_PACKAGE_H
#include <stddef.h>
#include <stdint.h>
#define BKVOICE_WAKE_PACKAGE_HEADER 136u
#define BKVOICE_WAKE_PACKAGE_HEADER_V2 140u
#define BKVOICE_WAKE_PACKAGE_HEADER_MAX BKVOICE_WAKE_PACKAGE_HEADER_V2
#define BKVOICE_WAKE_PACKAGE_MAX_MODEL 65536u
#ifndef BKVOICE_WAKE_PACKAGE_ROOT
#define BKVOICE_WAKE_PACKAGE_ROOT "/cpdata/shaniu/wake-models"
#endif
/* WKM1 is permanently frontend v1. WKM2 appends a big-endian uint32 version
 * at byte 136. Neither model shape nor a filename selects the frontend. */
struct bkvoice_wake_package_s { const uint8_t *model; size_t model_size; uint8_t sha256[32]; char label[33]; char phrase[65]; uint32_t frontend_version; };
struct bkvoice_wake_package_descriptor_s { char model_path[160]; char sha256_hex[65]; char label[32]; char phrase[64]; uint32_t frontend_version; };
const char *bkvoice_wake_package_frontend_id(uint32_t version);
size_t bkvoice_wake_package_header_size(uint32_t version);
/* Encode a validated nonempty descriptor. Returns header bytes or -errno. */
int bkvoice_wake_package_encode_header(void *record, size_t capacity,
  const struct bkvoice_wake_package_descriptor_s *descriptor, size_t model_size);
int bkvoice_wake_package_decode(const void *record, size_t size, struct bkvoice_wake_package_s *spec);
int bkvoice_wake_package_validate(const struct bkvoice_wake_package_s *spec);
int bkvoice_wake_package_stage(const struct bkvoice_wake_package_s *spec, struct bkvoice_wake_package_descriptor_s *descriptor);
int bkvoice_wake_package_load(struct bkvoice_wake_package_descriptor_s *active, struct bkvoice_wake_package_descriptor_s *previous, uint64_t *revision);
int bkvoice_wake_package_commit(const struct bkvoice_wake_package_descriptor_s *desired, const struct bkvoice_wake_package_descriptor_s *old, uint64_t expected_revision);
#endif
