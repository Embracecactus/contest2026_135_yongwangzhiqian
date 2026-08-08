#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_bl1_policy.h"
#include "boot_bl1_handoff_core.h"

#define PRIMARY_MANIFEST_ADDR  0x02003000u
#define RECOVERY_MANIFEST_ADDR 0x02004000u

static const struct bk7258_bl1_boot_flag_layout_s g_layout =
{
  PRIMARY_MANIFEST_ADDR,
  RECOVERY_MANIFEST_ADDR
};

static void put_le32(uint8_t *value, uint32_t word)
{
  value[0] = (uint8_t)(word >> 0);
  value[1] = (uint8_t)(word >> 8);
  value[2] = (uint8_t)(word >> 16);
  value[3] = (uint8_t)(word >> 24);
}

static void expect_order(const uint8_t order[2], uint8_t first,
                         uint8_t second)
{
  assert(order[0] == first);
  assert(order[1] == second);
}

static void expect_control_zero(
  const struct bk7258_bl1_boot_flag_s *control)
{
  const struct bk7258_bl1_boot_flag_s zero_control = { 0 };

  assert(memcmp(control, &zero_control, sizeof(*control)) == 0);
}

static void fill_valid_record(uint8_t record[BK7258_BL1_BOOT_FLAG_RECORD_SIZE],
                              uint32_t boot_flag)
{
  memset(record, 0, BK7258_BL1_BOOT_FLAG_RECORD_SIZE);
  put_le32(record + 0x00u, BK7258_BL1_BOOT_FLAG_MAGIC);
  put_le32(record + 0x04u, boot_flag);
  put_le32(record + 0x08u, PRIMARY_MANIFEST_ADDR);
  put_le32(record + 0x0cu, RECOVERY_MANIFEST_ADDR);
  put_le32(record + 0x10u, 1u); /* pll_ena */
  put_le32(record + 0x14u, 1u); /* security_boot_supported */
  put_le32(record + 0x18u, 0u); /* security_boot_ena */
  put_le32(record + 0x1cu, 0u); /* security_boot_print_dis */
  put_le32(record + 0x20u, 1u); /* jtag_dis */
  put_le32(record + 0x24u, 0u); /* sw_fih_delay_ena */
}

static void test_control_decode_and_validate(void)
{
  /* The ten words below are the values encoded by the official BK7258
   * bl1_control.json scaffold.  Its erased boot flag and zero Manifest
   * addresses intentionally make it non-bootable, but the recovered field
   * order must still decode exactly. */
  static const uint8_t official_scaffold[BK7258_BL1_BOOT_FLAG_RECORD_SIZE] =
  {
    0x63, 0x54, 0x72, 0x4c, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  static const size_t boolean_offsets[] =
    { 0x10u, 0x14u, 0x18u, 0x1cu, 0x20u, 0x24u };
  struct bk7258_bl1_boot_flag_layout_s bad_layout;
  struct bk7258_bl1_boot_flag_s control;
  uint8_t record[BK7258_BL1_BOOT_FLAG_RECORD_SIZE];
  uint8_t oversized_record[BK7258_BL1_BOOT_FLAG_RECORD_SIZE + 1u];
  enum bk7258_bl1_boot_flag_parse_status_e status;
  size_t index;

  assert(bk7258_bl1_boot_flag_decode(official_scaffold,
                                  sizeof(official_scaffold),
                                  &control) == 0);
  assert(control.magic == BK7258_BL1_BOOT_FLAG_MAGIC);
  assert(control.boot_flag == UINT32_MAX);
  assert(control.primary_manifest_addr == 0u);
  assert(control.recovery_manifest_addr == 0u);
  assert(control.pll_ena == 1u);
  assert(control.security_boot_supported == 1u);
  assert(control.security_boot_ena == 0u);
  assert(control.security_boot_print_dis == 0u);
  assert(control.jtag_dis == 1u);
  assert(control.sw_fih_delay_ena == 0u);
  assert(!bk7258_bl1_boot_flag_validate(&control, &g_layout));

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  status = bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                    &control);
  assert(status == BK7258_BL1_BOOT_FLAG_PARSE_VALID);
  assert(control.primary_manifest_addr == PRIMARY_MANIFEST_ADDR);
  assert(control.recovery_manifest_addr == RECOVERY_MANIFEST_ADDR);

  for (index = 0; index < sizeof(record); index++)
    {
      memset(&control, 0xa5, sizeof(control));
      status = bk7258_bl1_boot_flag_parse(record, index, &g_layout, &control);
      assert(status == (index == 0u ? BK7258_BL1_BOOT_FLAG_PARSE_ABSENT :
                                     BK7258_BL1_BOOT_FLAG_PARSE_INVALID));
      expect_control_zero(&control);
    }

  memcpy(oversized_record, record, sizeof(record));
  oversized_record[sizeof(record)] = 0u;
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(oversized_record,
                                  sizeof(oversized_record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  expect_control_zero(&control);

  memset(record, 0xff, sizeof(record));
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_ERASED);
  expect_control_zero(&control);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  record[0] = 0x4cu; /* Big-endian magic bytes are not silently accepted. */
  record[1] = 0x72u;
  record[2] = 0x54u;
  record[3] = 0x63u;
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  expect_control_zero(&control);

  fill_valid_record(record, 0u);
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  fill_valid_record(record, 3u);
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  put_le32(record + 0x08u, PRIMARY_MANIFEST_ADDR + 4u);
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  put_le32(record + 0x08u, RECOVERY_MANIFEST_ADDR);
  put_le32(record + 0x0cu, PRIMARY_MANIFEST_ADDR);
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);

  bad_layout = g_layout;
  bad_layout.primary_manifest_addr |= 1u;
  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &bad_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  bad_layout = g_layout;
  bad_layout.recovery_manifest_addr = bad_layout.primary_manifest_addr;
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &bad_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);

  for (index = 0;
       index < sizeof(boolean_offsets) / sizeof(boolean_offsets[0]); index++)
    {
      fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
      put_le32(record + boolean_offsets[index], 2u);
      assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                      &control) ==
             BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
    }

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  put_le32(record + 0x14u, 0u);
  put_le32(record + 0x18u, 1u);
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  expect_control_zero(&control);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), NULL, &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  expect_control_zero(&control);

  bad_layout = g_layout;
  bad_layout.recovery_manifest_addr = 0u;
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &bad_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  expect_control_zero(&control);

  bad_layout = g_layout;
  bad_layout.recovery_manifest_addr |= 1u;
  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &bad_layout,
                                  &control) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
  expect_control_zero(&control);

  memset(&control, 0xa5, sizeof(control));
  assert(bk7258_bl1_boot_flag_decode(NULL, sizeof(record), &control) < 0);
  {
    struct bk7258_bl1_boot_flag_s sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    assert(memcmp(&control, &sentinel, sizeof(control)) == 0);
  }
  assert(bk7258_bl1_boot_flag_decode(record, sizeof(record) + 1u,
                                  &control) < 0);
  assert(bk7258_bl1_boot_flag_decode(record, sizeof(record), NULL) < 0);
  assert(bk7258_bl1_boot_flag_parse(record, sizeof(record), &g_layout, NULL) ==
         BK7258_BL1_BOOT_FLAG_PARSE_INVALID);
}

static void test_control_order(void)
{
  uint8_t record[BK7258_BL1_BOOT_FLAG_RECORD_SIZE];
  uint8_t order[2] = { 0xffu, 0xffu };
  enum bk7258_bl1_boot_flag_status_e status;
  static const uint32_t invalid_flags[] =
    { 0u, 3u, 0xfffffffeu, 0xffffffffu };
  size_t index;

  status = bk7258_bl1_boot_flag_slot_order(NULL, 0u, NULL, order);
  assert(status == BK7258_BL1_BOOT_FLAG_ABSENT);
  expect_order(order, BK7258_BL1_SLOT_PRIMARY,
               BK7258_BL1_SLOT_SECONDARY);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  for (index = 0; index < BK7258_BL1_BOOT_FLAG_RECORD_SIZE; index++)
    {
      status = bk7258_bl1_boot_flag_slot_order(record, index, &g_layout, order);
      assert(status == (index == 0u ? BK7258_BL1_BOOT_FLAG_ABSENT :
                                     BK7258_BL1_BOOT_FLAG_INVALID));
      expect_order(order, BK7258_BL1_SLOT_PRIMARY,
                   BK7258_BL1_SLOT_SECONDARY);
    }

  memset(record, 0xff, sizeof(record));
  status = bk7258_bl1_boot_flag_slot_order(record, sizeof(record), &g_layout,
                                         order);
  assert(status == BK7258_BL1_BOOT_FLAG_ERASED);
  expect_order(order, BK7258_BL1_SLOT_PRIMARY,
               BK7258_BL1_SLOT_SECONDARY);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_SECONDARY);
  put_le32(record, BK7258_BL1_BOOT_FLAG_MAGIC ^ 1u);
  status = bk7258_bl1_boot_flag_slot_order(record, sizeof(record), &g_layout,
                                         order);
  assert(status == BK7258_BL1_BOOT_FLAG_INVALID);
  expect_order(order, BK7258_BL1_SLOT_PRIMARY,
               BK7258_BL1_SLOT_SECONDARY);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_PRIMARY);
  status = bk7258_bl1_boot_flag_slot_order(record, sizeof(record), &g_layout,
                                         order);
  assert(status == BK7258_BL1_BOOT_FLAG_VALID_PRIMARY);
  expect_order(order, BK7258_BL1_SLOT_PRIMARY,
               BK7258_BL1_SLOT_SECONDARY);

  fill_valid_record(record, BK7258_BL1_BOOT_FLAG_SECONDARY);
  status = bk7258_bl1_boot_flag_slot_order(record, sizeof(record), &g_layout,
                                         order);
  assert(status == BK7258_BL1_BOOT_FLAG_VALID_SECONDARY);
  expect_order(order, BK7258_BL1_SLOT_SECONDARY,
               BK7258_BL1_SLOT_PRIMARY);

  for (index = 0; index < sizeof(invalid_flags) / sizeof(invalid_flags[0]);
       index++)
    {
      put_le32(record + 4u, invalid_flags[index]);
      status = bk7258_bl1_boot_flag_slot_order(record, sizeof(record), &g_layout,
                                             order);
      assert(status == BK7258_BL1_BOOT_FLAG_INVALID);
      expect_order(order, BK7258_BL1_SLOT_PRIMARY,
                   BK7258_BL1_SLOT_SECONDARY);
    }

  assert(bk7258_bl1_boot_flag_slot_order(record, sizeof(record), &g_layout,
                                       NULL) ==
         BK7258_BL1_BOOT_FLAG_INVALID);
}

static void test_manifest_floor(void)
{
  assert(bk7258_bl1_security_counter_decode(0x00000000u) == 0u);
  assert(bk7258_bl1_security_counter_decode(0x00000001u) == 1u);
  assert(bk7258_bl1_security_counter_decode(0x00000003u) == 2u);
  assert(bk7258_bl1_security_counter_decode(0x00000007u) == 3u);
  assert(bk7258_bl1_security_counter_decode(0xffffffffu) == 32u);

  /* Match the recovered BootROM behavior for a malformed/non-contiguous
   * bitmap: bits after the first zero do not raise the accepted version. */
  assert(bk7258_bl1_security_counter_decode(0x00000005u) == 1u);
  assert(bk7258_bl1_security_counter_decode(0x80000000u) == 0u);

  assert(bk7258_bl1_manifest_effective_floor(0u) == 1u);
  assert(!bk7258_bl1_manifest_version_allowed(0u, 0u));
  assert(bk7258_bl1_manifest_version_allowed(1u, 0u));
  assert(!bk7258_bl1_manifest_version_allowed(4u, 5u));
  assert(bk7258_bl1_manifest_version_allowed(5u, 5u));
  assert(bk7258_bl1_manifest_version_allowed(6u, 5u));
  assert(!bk7258_bl1_manifest_version_allowed(5u, 6u));
  assert(!bk7258_bl1_manifest_version_allowed(UINT32_MAX - 1u,
                                               UINT32_MAX));
  assert(bk7258_bl1_manifest_version_allowed(UINT32_MAX, UINT32_MAX));
  assert(bk7258_bl1_manifest_version_floor_readonly() == 0u);
}

static void test_handoff_vector(void)
{
  const struct bk7258_bl1_handoff_window_s window =
    { 0x28020000u, 0x28040000u, 0x28020000u, 0x28040000u };
  struct bk7258_bl1_vector_s authorized =
    { 0x28040000u, 0x28020201u };
  struct bk7258_bl1_vector_s loaded = authorized;
  struct bk7258_bl1_handoff_window_s bad_window = window;

  assert(bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));

  loaded.msp ^= 4u;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));
  loaded = authorized;
  loaded.reset ^= 2u;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));

  loaded = authorized;
  authorized = loaded;
  loaded.msp = authorized.msp = window.stack_start - 4u;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));
  loaded.msp = authorized.msp = window.stack_end;
  assert(bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));
  loaded.msp = authorized.msp = window.stack_end | 2u;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));

  loaded = authorized = (struct bk7258_bl1_vector_s)
    { 0x28040000u, 0x28020001u };
  assert(bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));
  loaded.reset = authorized.reset = 0x2803ffffu;
  assert(bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));
  loaded.reset = authorized.reset = 0x28040001u;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));
  loaded.reset = authorized.reset = 0x28020200u;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, &window));

  bad_window.stack_end = bad_window.stack_start;
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded,
                                           &bad_window));
  assert(!bk7258_bl1_handoff_vector_valid(NULL, &loaded, &window));
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, NULL, &window));
  assert(!bk7258_bl1_handoff_vector_valid(&authorized, &loaded, NULL));
}

int main(void)
{
  test_control_decode_and_validate();
  test_control_order();
  test_manifest_floor();
  test_handoff_vector();
  return 0;
}
