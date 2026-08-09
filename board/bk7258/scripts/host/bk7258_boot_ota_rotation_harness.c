/* Host harness for the portable N15 symmetric metadata-bank core. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot_ota_rotation_core.h"
#include "bk7258_partition_layout.h"

#define DESCRIPTOR_SIZE              384u
#define DESCRIPTOR_CRC_OFFSET        380u
#define DESCRIPTOR_GENERATION        96u
#define DESCRIPTOR_TIMESTAMP        104u
#define DESCRIPTOR_PHYSICAL_OFFSET  108u
#define DESCRIPTOR_PHYSICAL_SIZE    112u
#define DESCRIPTOR_CP_LENGTH        136u
#define DESCRIPTOR_AP_LENGTH        152u
#define DESCRIPTOR_VERSION          160u
#define DESCRIPTOR_BASE_VERSION     184u

static unsigned int g_positive;
static unsigned int g_negative;

#define CHECK_POSITIVE(condition)                                           \
  do                                                                         \
    {                                                                        \
      if (!(condition))                                                      \
        {                                                                    \
          fprintf(stderr, "positive failure line %d: %s\n", __LINE__,       \
                  #condition);                                               \
          return 1;                                                          \
        }                                                                    \
      g_positive++;                                                          \
    }                                                                        \
  while (0)

#define CHECK_NEGATIVE(condition)                                           \
  do                                                                         \
    {                                                                        \
      if (!(condition))                                                      \
        {                                                                    \
          fprintf(stderr, "negative failure line %d: %s\n", __LINE__,       \
                  #condition);                                               \
          return 1;                                                          \
        }                                                                    \
      g_negative++;                                                          \
    }                                                                        \
  while (0)

static void putle16(uint8_t *value, uint16_t data)
{
  value[0] = (uint8_t)data;
  value[1] = (uint8_t)(data >> 8);
}

static void putle32(uint8_t *value, uint32_t data)
{
  value[0] = (uint8_t)data;
  value[1] = (uint8_t)(data >> 8);
  value[2] = (uint8_t)(data >> 16);
  value[3] = (uint8_t)(data >> 24);
}

static void putle64(uint8_t *value, uint64_t data)
{
  putle32(value, (uint32_t)data);
  putle32(value + 4, (uint32_t)(data >> 32));
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xffffffffu;

  while (len-- != 0)
    {
      uint32_t bit;

      crc ^= *data++;
      for (bit = 0; bit < 8; bit++)
        {
          uint32_t mask = 0u - (crc & 1u);
          crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

  return crc ^ 0xffffffffu;
}

static void encode_string(uint8_t output[24], const char *value)
{
  size_t length = strlen(value);

  memset(output, 0, 24);
  memcpy(output, value, length);
}

static uint32_t slot_start(enum bk7258_boot_ota_slot_e slot)
{
  return slot == BK7258_BOOT_OTA_SLOT_A ?
         BK7258_ROLE_SLOT_A_CP_OFFSET : BK7258_ROLE_SLOT_B_PAIR_OFFSET;
}

static void build_descriptor(uint8_t descriptor[DESCRIPTOR_SIZE],
                             uint64_t generation, uint32_t timestamp,
                             enum bk7258_boot_ota_slot_e target,
                             const char *version, const char *base_version)
{
  memset(descriptor, 0, DESCRIPTOR_SIZE);
  memcpy(descriptor, "BKOTA15B", 8);
  putle16(descriptor + 8, 1);
  putle16(descriptor + 10, DESCRIPTOR_SIZE);
  putle32(descriptor + 12, 0);
  putle64(descriptor + DESCRIPTOR_GENERATION, generation);
  putle32(descriptor + DESCRIPTOR_TIMESTAMP, timestamp);
  putle32(descriptor + DESCRIPTOR_PHYSICAL_OFFSET, slot_start(target));
  putle32(descriptor + DESCRIPTOR_PHYSICAL_SIZE,
          BK7258_ROLE_SLOT_B_PAIR_SIZE);
  putle32(descriptor + DESCRIPTOR_CP_LENGTH,
          9u * BK7258_FLASH_CRC_TOTAL_SIZE);
  putle32(descriptor + DESCRIPTOR_AP_LENGTH,
          BK7258_FLASH_CRC_TOTAL_SIZE);
  encode_string(descriptor + DESCRIPTOR_VERSION, version);
  encode_string(descriptor + DESCRIPTOR_BASE_VERSION, base_version);
  putle32(descriptor + DESCRIPTOR_CRC_OFFSET,
          crc32_bytes(descriptor, DESCRIPTOR_CRC_OFFSET));
}

static int build_pending_bank(
  uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE], uint64_t generation,
  enum bk7258_boot_ota_slot_e target, const char *version,
  const char *base_version)
{
  struct bk7258_boot_ota_rotation_pending_s pending;
  uint8_t descriptor[DESCRIPTOR_SIZE];
  uint8_t digest[BK7258_BOOT_OTA_ROTATION_SHA256_SIZE];

  memset(bank, 0xff, BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  memset(digest, (int)(generation & 0xffu), sizeof(digest));
  if (digest[0] == 0)
    {
      digest[0] = 1;
    }

  build_descriptor(descriptor, generation, (uint32_t)(1000u + generation),
                   target, version, base_version);
  pending.generation = generation;
  pending.timestamp = (uint32_t)(1000u + generation);
  pending.base_cp_physical_length = 9u * BK7258_FLASH_CRC_TOTAL_SIZE;
  pending.base_ap_physical_length = BK7258_FLASH_CRC_TOTAL_SIZE;
  pending.version = version;
  pending.base_version = base_version;
  pending.base_sha256 = digest;
  pending.descriptor = descriptor;
  pending.target_slot = target;
  return bk7258_boot_ota_rotation_build_pending(&pending, bank);
}

static int append_transition(
  uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE], uint64_t generation,
  enum bk7258_boot_ota_rotation_state_e expected,
  enum bk7258_boot_ota_rotation_state_e next,
  struct bk7258_boot_ota_rotation_transition_s *transition)
{
  uint8_t record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE];
  int ret;

  ret = bk7258_boot_ota_rotation_prepare_transition(
    bank, generation, expected, next, record, transition);
  if (ret == 0)
    {
      memcpy(bank + transition->record_offset, record, sizeof(record));
    }

  return ret;
}

static int inspect_pair(
  const uint8_t banks[2][BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  struct bk7258_boot_ota_rotation_bank_s info[2])
{
  int ret0 = bk7258_boot_ota_rotation_inspect(banks[0], &info[0]);
  int ret1 = bk7258_boot_ota_rotation_inspect(banks[1], &info[1]);

  return ret0 < 0 ? ret0 : ret1;
}

int main(void)
{
  uint8_t banks[2][BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  uint8_t scratch[BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  struct bk7258_boot_ota_rotation_bank_s info[2];
  struct bk7258_boot_ota_rotation_view_s view;
  struct bk7258_boot_ota_rotation_identity_s identity;
  struct bk7258_boot_ota_rotation_transition_s transition;
  enum bk7258_boot_ota_slot_e base;
  enum bk7258_boot_ota_slot_e target;
  int ret;

  memset(banks, 0xff, sizeof(banks));
  CHECK_POSITIVE(inspect_pair(banks, info) == 0 && info[0].erased &&
                 info[1].erased);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 !view.metadata_present &&
                 view.stable_slot == BK7258_BOOT_OTA_SLOT_A);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_state_slots(
                   BK7258_BOOT_OTA_ROTATION_PENDING_B, &base, &target) == 0 &&
                 base == BK7258_BOOT_OTA_SLOT_A &&
                 target == BK7258_BOOT_OTA_SLOT_B);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_state_slots(
                   BK7258_BOOT_OTA_ROTATION_PENDING_A, &base, &target) == 0 &&
                 base == BK7258_BOOT_OTA_SLOT_B &&
                 target == BK7258_BOOT_OTA_SLOT_A);

  CHECK_POSITIVE(build_pending_bank(banks[0], 1,
                                    BK7258_BOOT_OTA_SLOT_B,
                                    "2.0.0", "1.0.0") == 0);
  CHECK_POSITIVE(inspect_pair(banks, info) == 0 && info[0].trusted &&
                 info[0].state == BK7258_BOOT_OTA_ROTATION_PENDING_B);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 view.trial_required &&
                 view.stable_slot == BK7258_BOOT_OTA_SLOT_A &&
                 view.target_slot == BK7258_BOOT_OTA_SLOT_B);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_latest(banks[0], &identity) == 0 &&
                 identity.state == BK7258_BOOT_OTA_ROTATION_PENDING_B &&
                 identity.base_slot == BK7258_BOOT_OTA_SLOT_A &&
                 identity.target_slot == BK7258_BOOT_OTA_SLOT_B &&
                 identity.generation == 1 && identity.sequence == 1 &&
                 identity.descriptor != NULL &&
                 identity.base_sha256 != NULL);
  CHECK_POSITIVE(append_transition(
                   banks[0], 1, BK7258_BOOT_OTA_ROTATION_PENDING_B,
                   BK7258_BOOT_OTA_ROTATION_TRIAL_B, &transition) == 0 &&
                 transition.current_boot_trial);
  CHECK_POSITIVE(inspect_pair(banks, info) == 0 &&
                 bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 view.state == BK7258_BOOT_OTA_ROTATION_TRIAL_B &&
                 view.stable_slot == BK7258_BOOT_OTA_SLOT_A &&
                 !view.trial_required);
  CHECK_POSITIVE(append_transition(
                   banks[0], 1, BK7258_BOOT_OTA_ROTATION_TRIAL_B,
                   BK7258_BOOT_OTA_ROTATION_CONFIRMED_B, &transition) == 0);
  CHECK_POSITIVE(inspect_pair(banks, info) == 0 &&
                 bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 view.stable_slot == BK7258_BOOT_OTA_SLOT_B);

  CHECK_POSITIVE(build_pending_bank(banks[1], 2,
                                    BK7258_BOOT_OTA_SLOT_A,
                                    "3.0.0", "2.0.0") == 0);
  CHECK_POSITIVE(inspect_pair(banks, info) == 0 &&
                 bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 view.selected_bank == 1 && view.trial_required &&
                 view.stable_slot == BK7258_BOOT_OTA_SLOT_B);
  CHECK_POSITIVE(append_transition(
                   banks[1], 2, BK7258_BOOT_OTA_ROTATION_PENDING_A,
                   BK7258_BOOT_OTA_ROTATION_TRIAL_A, &transition) == 0 &&
                 transition.current_boot_trial);
  CHECK_POSITIVE(append_transition(
                   banks[1], 2, BK7258_BOOT_OTA_ROTATION_TRIAL_A,
                   BK7258_BOOT_OTA_ROTATION_CONFIRMED_A, &transition) == 0);
  CHECK_POSITIVE(inspect_pair(banks, info) == 0 &&
                 bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 view.stable_slot == BK7258_BOOT_OTA_SLOT_A &&
                 view.generation == 2);

  CHECK_POSITIVE(build_pending_bank(scratch, 3,
                                    BK7258_BOOT_OTA_SLOT_B,
                                    "4.0.0", "3.0.0") == 0);
  CHECK_POSITIVE(append_transition(
                   scratch, 3, BK7258_BOOT_OTA_ROTATION_PENDING_B,
                   BK7258_BOOT_OTA_ROTATION_TRIAL_B, &transition) == 0);
  CHECK_POSITIVE(append_transition(
                   scratch, 3, BK7258_BOOT_OTA_ROTATION_TRIAL_B,
                   BK7258_BOOT_OTA_ROTATION_ROLLBACK_A, &transition) == 0);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_inspect(scratch, &info[0]) == 0 &&
                 info[0].state == BK7258_BOOT_OTA_ROTATION_ROLLBACK_A);

  memcpy(scratch, banks[1], sizeof(scratch));
  scratch[0] ^= 0x01u;
  CHECK_NEGATIVE(bk7258_boot_ota_rotation_inspect(scratch, &info[1]) ==
                 -EBADMSG);
  CHECK_POSITIVE(bk7258_boot_ota_rotation_inspect(banks[0], &info[0]) == 0 &&
                 bk7258_boot_ota_rotation_select(info, &view) == 0 &&
                 view.generation == 1);

  memcpy(scratch, banks[0], sizeof(scratch));
  scratch[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE + 16] ^= 0x01u;
  CHECK_NEGATIVE(bk7258_boot_ota_rotation_inspect(scratch, &info[0]) ==
                 -EBADMSG);

  info[0].trusted = true;
  info[0].erased = false;
  info[0].generation = 7;
  info[1] = info[0];
  CHECK_NEGATIVE(bk7258_boot_ota_rotation_select(info, &view) == -EBADMSG);

  ret = bk7258_boot_ota_rotation_prepare_transition(
    banks[1], 99, BK7258_BOOT_OTA_ROTATION_CONFIRMED_A,
    BK7258_BOOT_OTA_ROTATION_PENDING_B, scratch, &transition);
  CHECK_NEGATIVE(ret == -EINVAL);
  ret = bk7258_boot_ota_rotation_prepare_transition(
    banks[1], 3, BK7258_BOOT_OTA_ROTATION_TRIAL_A,
    BK7258_BOOT_OTA_ROTATION_CONFIRMED_A, scratch, &transition);
  CHECK_NEGATIVE(ret == -ESTALE);
  ret = bk7258_boot_ota_rotation_prepare_transition(
    banks[1], 2, BK7258_BOOT_OTA_ROTATION_PENDING_A,
    BK7258_BOOT_OTA_ROTATION_TRIAL_A, scratch, &transition);
  CHECK_NEGATIVE(ret == -EPERM);

  memset(scratch, 0xff, sizeof(scratch));
  scratch[0] = 0;
  CHECK_NEGATIVE(bk7258_boot_ota_rotation_inspect(scratch, &info[0]) ==
                 -EBADMSG);
  CHECK_NEGATIVE(bk7258_boot_ota_rotation_state_slots(
                   BK7258_BOOT_OTA_ROTATION_ERASED, &base, &target) ==
                 -EINVAL);

  printf("BK7258 OTA rotation harness PASS: positive=%u negative=%u\n",
         g_positive, g_negative);
  return 0;
}
