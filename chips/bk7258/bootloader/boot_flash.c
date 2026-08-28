/*
 * boot_flash.c - freestanding BK7258 boot-stage raw-Flash access.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "boot_flash.h"
#include "boot_wdt.h"
#include "bk7258_image_layout.h"
#include <bk7258_partitions.h>

#define BL1_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define FLASH_CONTROLLER_BASE       0x44030000u
#define FLASH_OP_CTRL               (FLASH_CONTROLLER_BASE + 0x10u)
#define FLASH_DATA_SW_TO_FLASH      (FLASH_CONTROLLER_BASE + 0x14u)
#define FLASH_DATA_FLASH_TO_SW      (FLASH_CONTROLLER_BASE + 0x18u)
#define FLASH_COMMAND_CONFIG        (FLASH_CONTROLLER_BASE + 0x1cu)
#define FLASH_READ_ID_DATA          (FLASH_CONTROLLER_BASE + 0x20u)
#define FLASH_STATUS_DATA           (FLASH_CONTROLLER_BASE + 0x24u)
#define FLASH_CONFIG                (FLASH_CONTROLLER_BASE + 0x28u)
#define FLASH_OP_CMD                (FLASH_CONTROLLER_BASE + 0x54u)

#define FLASH_OP_SW                 (1u << 29)
#define FLASH_WP_VALUE              (1u << 30)
#define FLASH_BUSY_SW               (1u << 31)
#define FLASH_ADDRESS_MASK          0x00ffffffu
#define FLASH_COMMAND_SHIFT         24u
#define FLASH_COMMAND_MASK          (0x1fu << FLASH_COMMAND_SHIFT)
#define FLASH_COMMAND_READ          5u
#define FLASH_COMMAND_RDSR          3u
#define FLASH_COMMAND_WRSR          4u
#define FLASH_COMMAND_RDSR2         6u
#define FLASH_COMMAND_WRSR2         7u
#define FLASH_COMMAND_PROGRAM       12u
#define FLASH_COMMAND_ERASE_SECTOR  13u
#define FLASH_COMMAND_READ_ID       20u
#define FLASH_READ_GRANULE          32u
#define FLASH_READ_WORDS            8u
#define FLASH_ERASE_SIZE            4096u
#define FLASH_WAIT_BUDGET           0x01000000u

#define FLASH_WRSR_DATA_SHIFT       10u
#define FLASH_WRSR_DATA_MASK        (0xffffu << FLASH_WRSR_DATA_SHIFT)
#define FLASH_WRSR_COMMAND_HIGH     0x31u
#define FLASH_WRSR_COMMAND_SELECT   (1u << 16)

#define FLASH_ID_GD25WQ64E          0x00c86517u

struct bk7258_boot_flash_profile_s
{
  uint32_t id;
  uint8_t status_bytes;
  uint8_t cmp_bit;
  uint8_t protect_shift;
  uint8_t protect_mask;
  uint8_t default_protect;
};

/* The T5Board hardware checkpoint reports C86517.  Keep boot-stage mutation
 * deliberately narrower than the runtime SDK table until each additional
 * status-register command set has been exercised on real hardware. */
static const struct bk7258_boot_flash_profile_s g_flash_profiles[] =
{
  {FLASH_ID_GD25WQ64E, 2u, 14u, 2u, 0x1fu, 0x0eu}
};

static int flash_wait_idle(void)
{
  uint32_t remaining = FLASH_WAIT_BUDGET;

  while ((BL1_REG32(FLASH_OP_CTRL) & FLASH_BUSY_SW) != 0)
    {
      if ((remaining & 0xffffu) == 0)
        {
          boot_wdt_feed();
        }

      if (remaining == 0)
        {
          return -1;
        }

      remaining--;
    }

  return 0;
}

static int flash_start(uint32_t command, uint32_t address, bool write)
{
  uint32_t control;

  if (flash_wait_idle() < 0 || address > FLASH_ADDRESS_MASK)
    {
      return -1;
    }

  BL1_REG32(FLASH_OP_CMD) =
    (address & FLASH_ADDRESS_MASK) |
    ((command << FLASH_COMMAND_SHIFT) & FLASH_COMMAND_MASK);
  control = BL1_REG32(FLASH_OP_CTRL);
  control &= ~(FLASH_OP_SW | FLASH_WP_VALUE);
  if (write)
    {
      control |= FLASH_WP_VALUE;
    }

  BL1_REG32(FLASH_OP_CTRL) = control | FLASH_OP_SW;
  return flash_wait_idle();
}

static int flash_read_id(uint32_t *id)
{
  if (id == NULL || flash_start(FLASH_COMMAND_READ_ID, 0u, false) < 0)
    {
      return -1;
    }

  *id = BL1_REG32(FLASH_READ_ID_DATA) & 0x00ffffffu;
  return 0;
}

static const struct bk7258_boot_flash_profile_s *flash_profile(uint32_t id)
{
  size_t index;

  for (index = 0; index < sizeof(g_flash_profiles) /
                             sizeof(g_flash_profiles[0]); index++)
    {
      if (g_flash_profiles[index].id == id)
        {
          return &g_flash_profiles[index];
        }
    }

  return NULL;
}

static int flash_read_status(
  const struct bk7258_boot_flash_profile_s *profile, uint32_t *status)
{
  uint32_t value;

  if (profile == NULL || status == NULL ||
      flash_start(FLASH_COMMAND_RDSR, 0u, false) < 0)
    {
      return -1;
    }

  value = BL1_REG32(FLASH_STATUS_DATA) & 0xffu;
  if (profile->status_bytes > 1u)
    {
      if (flash_start(FLASH_COMMAND_RDSR2, 0u, false) < 0)
        {
          return -1;
        }

      value |= (BL1_REG32(FLASH_STATUS_DATA) & 0xffu) << 8;
    }

  *status = value;
  return 0;
}

static int flash_write_status(
  const struct bk7258_boot_flash_profile_s *profile, uint32_t status)
{
  uint32_t config;

  if (profile == NULL)
    {
      return -1;
    }

  config = BL1_REG32(FLASH_CONFIG);
  config &= ~FLASH_WRSR_DATA_MASK;
  config |= (status << FLASH_WRSR_DATA_SHIFT) & FLASH_WRSR_DATA_MASK;
  BL1_REG32(FLASH_CONFIG) = config;

  if (profile->status_bytes == 1u)
    {
      return flash_start(FLASH_COMMAND_WRSR, 0u, true);
    }

  return flash_start(FLASH_COMMAND_WRSR2, 0u, true);
}

static uint32_t flash_status_with_protection(
  const struct bk7258_boot_flash_profile_s *profile, uint32_t status,
  uint32_t protection)
{
  status &= ~((uint32_t)profile->protect_mask << profile->protect_shift);
  status |= (protection & profile->protect_mask) << profile->protect_shift;
  if (profile->cmp_bit < profile->status_bytes * 8u)
    {
      status &= ~(1u << profile->cmp_bit);
    }

  return status;
}

static int flash_write_status_checked(
  const struct bk7258_boot_flash_profile_s *profile, uint32_t status)
{
  uint32_t observed;

  if (flash_write_status(profile, status) < 0 ||
      flash_read_status(profile, &observed) < 0)
    {
      return -1;
    }

  return observed == status ? 0 : -1;
}

static int flash_unprotect(
  const struct bk7258_boot_flash_profile_s **profile_out,
  uint32_t *saved_status)
{
  const struct bk7258_boot_flash_profile_s *profile;
  uint32_t id;
  uint32_t status;
  uint32_t unprotected;

  if (profile_out == NULL || saved_status == NULL || flash_read_id(&id) < 0)
    {
      return -1;
    }

  profile = flash_profile(id);
  if (profile == NULL || flash_read_status(profile, &status) < 0)
    {
      return -1;
    }

  unprotected = flash_status_with_protection(profile, status, 0u);

  if (unprotected != status &&
      flash_write_status_checked(profile, unprotected) < 0)
    {
      return -1;
    }

  *profile_out = profile;
  /* Restore the pinned SDK's FLASH_UNPROTECT_LAST_BLOCK policy rather than
   * the observed value.  If a prior reset interrupted an unprotect window,
   * the observed value is already unsafe and must not become the new policy. */
  *saved_status = flash_status_with_protection(
    profile, status, profile->default_protect);
  return 0;
}

static int flash_restore_protection(
  const struct bk7258_boot_flash_profile_s *profile, uint32_t saved_status)
{
  uint32_t current;

  if (flash_read_status(profile, &current) < 0)
    {
      return -1;
    }

  return current == saved_status ? 0 :
    flash_write_status_checked(profile, saved_status);
}

int bk7258_boot_flash_restore_default_protection(void)
{
  const struct bk7258_boot_flash_profile_s *profile;
  uint32_t id;
  uint32_t status;
  uint32_t expected;

  if (flash_read_id(&id) < 0 || (profile = flash_profile(id)) == NULL ||
      flash_read_status(profile, &status) < 0)
    {
      return -1;
    }

  expected = flash_status_with_protection(
    profile, status, profile->default_protect);
  return status == expected ? 0 : flash_write_status_checked(profile, expected);
}

static bool flash_mutation_range_allowed(uint32_t address, size_t len)
{
  uint32_t end = address + (uint32_t)len;

#define BK7258_FLASH_WITHIN(start, size) \
  (address >= (start) && end <= (start) + (size))
  return BK7258_FLASH_WITHIN(BK7258_CP_RAW_PHYSICAL_START,
                             BK7258_CP_RAW_PHYSICAL_SIZE) ||
         BK7258_FLASH_WITHIN(BK7258_AP_RAW_PHYSICAL_START,
                             BK7258_AP_RAW_PHYSICAL_SIZE) ||
         BK7258_FLASH_WITHIN(BK7258_AB_SECONDARY_CP_RAW_START,
                             BK7258_CP_RAW_PHYSICAL_SIZE) ||
         BK7258_FLASH_WITHIN(BK7258_AB_SECONDARY_AP_RAW_START,
                             BK7258_AP_RAW_PHYSICAL_SIZE);
#undef BK7258_FLASH_WITHIN
}

static int flash_read_aligned(uint32_t address,
                              uint8_t output[FLASH_READ_GRANULE])
{
  uint32_t command;
  uint32_t index;

  if ((address & (FLASH_READ_GRANULE - 1u)) != 0 ||
      address > BK7258_FLASH_SIZE - FLASH_READ_GRANULE ||
      flash_wait_idle() < 0)
    {
      return -1;
    }

  command = BL1_REG32(FLASH_OP_CMD);
  command &= ~(FLASH_ADDRESS_MASK | FLASH_COMMAND_MASK);
  command |= address | (FLASH_COMMAND_READ << FLASH_COMMAND_SHIFT);
  BL1_REG32(FLASH_OP_CMD) = command;
  BL1_REG32(FLASH_OP_CTRL) = BL1_REG32(FLASH_OP_CTRL) | FLASH_OP_SW;
  if (flash_wait_idle() < 0)
    {
      return -1;
    }

  for (index = 0; index < FLASH_READ_WORDS; index++)
    {
      uint32_t word = BL1_REG32(FLASH_DATA_FLASH_TO_SW);
      output[index * 4u] = (uint8_t)word;
      output[index * 4u + 1u] = (uint8_t)(word >> 8);
      output[index * 4u + 2u] = (uint8_t)(word >> 16);
      output[index * 4u + 3u] = (uint8_t)(word >> 24);
    }

  boot_wdt_feed();
  return 0;
}

int bk7258_boot_flash_read(uint32_t address, uint8_t *buffer, size_t len)
{
  uint8_t block[FLASH_READ_GRANULE];

  if (buffer == NULL || len == 0 || address >= BK7258_FLASH_SIZE ||
      len > BK7258_FLASH_SIZE - address)
    {
      return -1;
    }

  while (len != 0)
    {
      uint32_t aligned = address & ~(FLASH_READ_GRANULE - 1u);
      uint32_t offset = address - aligned;
      size_t count = FLASH_READ_GRANULE - offset;
      size_t index;

      if (count > len)
        {
          count = len;
        }

      if (flash_read_aligned(aligned, block) < 0)
        {
          return -1;
        }

      for (index = 0; index < count; index++)
        {
          buffer[index] = block[offset + index];
        }

      address += (uint32_t)count;
      buffer += count;
      len -= count;
    }

  return 0;
}

int bk7258_boot_flash_program(uint32_t address, const uint8_t *buffer,
                              size_t len)
{
  const struct bk7258_boot_flash_profile_s *profile;
  uint8_t current[FLASH_READ_GRANULE];
  uint8_t block[FLASH_READ_GRANULE];
  uint32_t saved_status;
  int result = 0;

  if (buffer == NULL || len == 0 || address >= BK7258_FLASH_SIZE ||
      len > BK7258_FLASH_SIZE - address ||
      !flash_mutation_range_allowed(address, len) ||
      flash_unprotect(&profile, &saved_status) < 0)
    {
      return -1;
    }

  while (len != 0)
    {
      uint32_t aligned = address & ~(FLASH_READ_GRANULE - 1u);
      uint32_t offset = address - aligned;
      size_t count = FLASH_READ_GRANULE - offset;
      size_t index;

      if (count > len)
        {
          count = len;
        }

      if (bk7258_boot_flash_read(aligned, current, sizeof(current)) < 0)
        {
          result = -1;
          break;
        }

      for (index = 0; index < sizeof(block); index++)
        {
          block[index] = current[index];
        }

      for (index = 0; index < count; index++)
        {
          uint8_t value = buffer[index];
          if ((value | current[offset + index]) != current[offset + index])
            {
              result = -1;
              break;
            }

          block[offset + index] = value;
        }

      if (result < 0)
        {
          break;
        }

      if (flash_wait_idle() < 0)
        {
          result = -1;
          break;
        }

      for (index = 0; index < FLASH_READ_WORDS; index++)
        {
          uint32_t word = (uint32_t)block[index * 4u] |
                          ((uint32_t)block[index * 4u + 1u] << 8) |
                          ((uint32_t)block[index * 4u + 2u] << 16) |
                          ((uint32_t)block[index * 4u + 3u] << 24);
          BL1_REG32(FLASH_DATA_SW_TO_FLASH) = word;
        }

      if (flash_start(FLASH_COMMAND_PROGRAM, aligned, true) < 0)
        {
          result = -1;
          break;
        }

      address += (uint32_t)count;
      buffer += count;
      len -= count;
      boot_wdt_feed();
    }

  if (flash_restore_protection(profile, saved_status) < 0)
    {
      boot_wdt_fail_reset();
    }

  return result;
}

int bk7258_boot_flash_erase(uint32_t address, size_t len)
{
  const struct bk7258_boot_flash_profile_s *profile;
  uint8_t verify[FLASH_READ_GRANULE];
  uint32_t saved_status;
  int result = 0;

  if (len == 0 || (address & (FLASH_ERASE_SIZE - 1u)) != 0 ||
      (len & (FLASH_ERASE_SIZE - 1u)) != 0 ||
      address >= BK7258_FLASH_SIZE || len > BK7258_FLASH_SIZE - address ||
      !flash_mutation_range_allowed(address, len) ||
      flash_unprotect(&profile, &saved_status) < 0)
    {
      return -1;
    }

  while (len != 0)
    {
      uint32_t offset;

      if (flash_start(FLASH_COMMAND_ERASE_SECTOR, address, true) < 0)
        {
          result = -1;
          break;
        }

      for (offset = 0; offset < FLASH_ERASE_SIZE;
           offset += sizeof(verify))
        {
          uint32_t index;

          if (bk7258_boot_flash_read(address + offset, verify,
                                     sizeof(verify)) < 0)
            {
              result = -1;
              break;
            }

          for (index = 0; index < sizeof(verify); index++)
            {
              if (verify[index] != 0xffu)
                {
                  result = -1;
                  break;
                }
            }

          if (result < 0)
            {
              break;
            }
        }

      if (result < 0)
        {
          break;
        }

      address += FLASH_ERASE_SIZE;
      len -= FLASH_ERASE_SIZE;
      boot_wdt_feed();
    }

  if (flash_restore_protection(profile, saved_status) < 0)
    {
      boot_wdt_fail_reset();
    }

  return result;
}
