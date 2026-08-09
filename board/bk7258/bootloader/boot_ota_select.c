/*
 * boot_ota_select.c - BK7258 N15-C raw-Flash and one-offset remap adapter.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is team-owned clean-room code.  It reproduces only the exact
 * v3.1.1.9 Flash-controller read/remap contracts needed by the portable
 * selector.  It never erases or programs Flash.  All four selection/remap
 * gates remain immutable zero in the N15-C closure build.
 */

#include <stddef.h>
#include <stdint.h>

#include "boot_ota_select.h"
#include "boot_ota_select_core.h"
#include "boot_ota_flash_program.h"
#include "boot_ota_rotation_select_core.h"
#include "boot_ota_rotation_trial_core.h"
#include "boot_n17_select.h"
#include "boot_sha256.h"
#include "boot_wdt.h"
#include "../chip/include/bk7258_partition_layout.h"

#ifndef BK7258_BOOT_OTA_SELECT_COMPILE_GATE
#  define BK7258_BOOT_OTA_SELECT_COMPILE_GATE 0u
#endif

#ifndef BK7258_BOOT_OTA_SELECT_RUNTIME_GATE
#  define BK7258_BOOT_OTA_SELECT_RUNTIME_GATE 0u
#endif

#ifndef BK7258_BOOT_OTA_REMAP_COMPILE_GATE
#  define BK7258_BOOT_OTA_REMAP_COMPILE_GATE 0u
#endif

#ifndef BK7258_BOOT_OTA_REMAP_RUNTIME_GATE
#  define BK7258_BOOT_OTA_REMAP_RUNTIME_GATE 0u
#endif

#ifndef BK7258_BOOT_OTA_TRIAL_COMPILE_GATE
#  define BK7258_BOOT_OTA_TRIAL_COMPILE_GATE 0u
#endif

#ifndef BK7258_BOOT_OTA_TRIAL_RUNTIME_GATE
#  define BK7258_BOOT_OTA_TRIAL_RUNTIME_GATE 0u
#endif

#ifndef BK7258_BOOT_N17_SELECT_COMPILE_GATE
#  define BK7258_BOOT_N17_SELECT_COMPILE_GATE 0u
#endif

#ifndef BK7258_BOOT_N17_SELECT_RUNTIME_GATE
#  define BK7258_BOOT_N17_SELECT_RUNTIME_GATE 0u
#endif

#ifndef BK7258_BL2_BOOT_POLICY_COMPILE_GATE
#  define BK7258_BL2_BOOT_POLICY_COMPILE_GATE 0u
#endif

#ifndef BK7258_BL2_BOOT_POLICY_RUNTIME_GATE
#  define BK7258_BL2_BOOT_POLICY_RUNTIME_GATE 0u
#endif

#define OTA_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define FLASH_CONTROLLER_BASE       0x44030000u
#define FLASH_OP_CTRL               (FLASH_CONTROLLER_BASE + 0x10u)
#define FLASH_DATA_FLASH_TO_SW      (FLASH_CONTROLLER_BASE + 0x18u)
#define FLASH_REMAP_BEGIN           (FLASH_CONTROLLER_BASE + 0x58u)
#define FLASH_REMAP_END             (FLASH_CONTROLLER_BASE + 0x5cu)
#define FLASH_REMAP_OFFSET          (FLASH_CONTROLLER_BASE + 0x60u)
#define FLASH_REMAP_ENABLE          (FLASH_CONTROLLER_BASE + 0x64u)
#define FLASH_OP_CMD                (FLASH_CONTROLLER_BASE + 0x54u)

#define FLASH_OP_SW                 (1u << 29)
#define FLASH_BUSY_SW               (1u << 31)
#define FLASH_ADDRESS_MASK          0x00ffffffu
#define FLASH_COMMAND_SHIFT         24u
#define FLASH_COMMAND_MASK          (0x1fu << FLASH_COMMAND_SHIFT)
#define FLASH_COMMAND_READ          5u
#define FLASH_READ_GRANULE          32u
#define FLASH_READ_WORDS            8u
#define FLASH_WAIT_BUDGET           0x01000000u

#define FLASH_SIZE                  BK7258_FLASH_SIZE
#define CP_APP_VECTOR               BK7258_ROLE_SLOT_A_CP_XIP_START
#define REMAP_ADDRESS_BEGIN         BK7258_ROLE_SLOT_A_CP_XIP_START
#define REMAP_ADDRESS_END           BK7258_ROLE_SLOT_A_AP_XIP_END
#define REMAP_ADDRESS_OFFSET        \
  (BK7258_FLASH_XIP_BASE + \
   (BK7258_ROLE_SLOT_B_PAIR_OFFSET / BK7258_FLASH_CRC_TOTAL_SIZE * \
    BK7258_FLASH_CRC_DATA_SIZE) - BK7258_ROLE_SLOT_A_CP_LOGICAL_OFFSET)

#define SCB_ICIALLU                 0xe000ef50u

/* The volatile loads retain the complete audited path in the final ELF.
 * These objects are const, live in Flash, and deliberately have no setter.
 */

__attribute__((used))
const uint32_t g_bk7258_boot_ota_select_compile_gate =
  BK7258_BOOT_OTA_SELECT_COMPILE_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_ota_select_runtime_gate =
  BK7258_BOOT_OTA_SELECT_RUNTIME_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_ota_remap_compile_gate =
  BK7258_BOOT_OTA_REMAP_COMPILE_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_ota_remap_runtime_gate =
  BK7258_BOOT_OTA_REMAP_RUNTIME_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_ota_trial_compile_gate =
  BK7258_BOOT_OTA_TRIAL_COMPILE_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_ota_trial_runtime_gate =
  BK7258_BOOT_OTA_TRIAL_RUNTIME_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_n17_select_compile_gate =
  BK7258_BOOT_N17_SELECT_COMPILE_GATE;

__attribute__((used))
const uint32_t g_bk7258_boot_n17_select_runtime_gate =
  BK7258_BOOT_N17_SELECT_RUNTIME_GATE;

__attribute__((used))
const uint32_t g_bk7258_bl2_boot_policy_compile_gate =
  BK7258_BL2_BOOT_POLICY_COMPILE_GATE;

__attribute__((used))
const uint32_t g_bk7258_bl2_boot_policy_runtime_gate =
  BK7258_BL2_BOOT_POLICY_RUNTIME_GATE;

/* 0x2800d000..0x2800ffff is below the CP application SRAM window and is
 * used only before handoff.  The linker fixes and bounds this 12 KiB area;
 * boot_ota_clear_workspace() restores it to zero before the app starts.
 */

__attribute__((section(".boot_ota_workspace"), aligned(32)))
static uint8_t g_boot_ota_metadata[BK7258_BOOT_OTA_ROTATION_BANK_SIZE];

__attribute__((section(".boot_ota_workspace"), aligned(32)))
static uint8_t g_boot_ota_scratch[BK7258_OTA_STAGE_SCRATCH_SIZE];

extern uint8_t __boot_ota_ramfunc_load_start[];
extern uint8_t __boot_ota_ramfunc_start[];
extern uint8_t __boot_ota_ramfunc_end[];

static void boot_dsb(void)
{
  __asm volatile ("dsb sy" ::: "memory");
}

__attribute__((noinline))
static uint32_t boot_gate_read(const uint32_t *gate)
{
  return *(const volatile uint32_t *)gate;
}

static void boot_isb(void)
{
  __asm volatile ("isb sy" ::: "memory");
}

static void boot_invalidate_icache(void)
{
  boot_dsb();
  OTA_REG32(SCB_ICIALLU) = 0;
  boot_dsb();
  boot_isb();
}

static void boot_ota_clear_workspace(void)
{
  uint8_t *ramfunc;
  size_t index;

  for (index = 0; index < sizeof(g_boot_ota_metadata); index++)
    {
      g_boot_ota_metadata[index] = 0;
    }

  for (index = 0; index < sizeof(g_boot_ota_scratch); index++)
    {
      g_boot_ota_scratch[index] = 0;
    }

  for (ramfunc = __boot_ota_ramfunc_start;
       ramfunc < __boot_ota_ramfunc_end; ramfunc++)
    {
      *ramfunc = 0;
    }

  boot_dsb();
}

static int boot_ota_install_ramfunc(void)
{
  const uint8_t *source = __boot_ota_ramfunc_load_start;
  uint8_t *destination = __boot_ota_ramfunc_start;
  uint8_t *verify;

  while (destination < __boot_ota_ramfunc_end)
    {
      *destination++ = *source++;
    }

  boot_dsb();
  boot_isb();
  boot_invalidate_icache();
  source = __boot_ota_ramfunc_load_start;
  for (verify = __boot_ota_ramfunc_start;
       verify < __boot_ota_ramfunc_end; verify++)
    {
      if (*verify != *source++)
        {
          boot_ota_clear_workspace();
          return -1;
        }
    }

  return 0;
}

static int flash_wait_idle(void)
{
  uint32_t remaining = FLASH_WAIT_BUDGET;

  while ((OTA_REG32(FLASH_OP_CTRL) & FLASH_BUSY_SW) != 0)
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

static int flash_read_aligned(uint32_t address,
                              uint8_t output[FLASH_READ_GRANULE])
{
  uint32_t command;
  uint32_t index;

  if ((address & (FLASH_READ_GRANULE - 1u)) != 0 ||
      address > FLASH_SIZE - FLASH_READ_GRANULE || flash_wait_idle() < 0)
    {
      return -1;
    }

  command = OTA_REG32(FLASH_OP_CMD);
  command &= ~(FLASH_ADDRESS_MASK | FLASH_COMMAND_MASK);
  command |= address | (FLASH_COMMAND_READ << FLASH_COMMAND_SHIFT);
  OTA_REG32(FLASH_OP_CMD) = command;
  OTA_REG32(FLASH_OP_CTRL) = OTA_REG32(FLASH_OP_CTRL) | FLASH_OP_SW;
  if (flash_wait_idle() < 0)
    {
      return -1;
    }

  for (index = 0; index < FLASH_READ_WORDS; index++)
    {
      uint32_t word = OTA_REG32(FLASH_DATA_FLASH_TO_SW);
      output[index * 4u] = (uint8_t)word;
      output[index * 4u + 1u] = (uint8_t)(word >> 8);
      output[index * 4u + 2u] = (uint8_t)(word >> 16);
      output[index * 4u + 3u] = (uint8_t)(word >> 24);
    }

  boot_wdt_feed();
  return 0;
}

int boot_ota_raw_read(void *arg, uint32_t address, uint8_t *buffer,
                      size_t len)
{
  uint8_t block[FLASH_READ_GRANULE];

  (void)arg;
  if (buffer == NULL || len == 0 || address >= FLASH_SIZE ||
      len > FLASH_SIZE - address)
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

static bool boot_ota_trial_compile_write(void *arg)
{
  (void)arg;
  return boot_gate_read(&g_bk7258_boot_ota_trial_compile_gate) != 0 ||
         boot_gate_read(&g_bk7258_bl2_boot_policy_compile_gate) != 0;
}

static bool boot_ota_trial_runtime_write(void *arg)
{
  (void)arg;
  return boot_gate_read(&g_bk7258_boot_ota_trial_runtime_gate) != 0 ||
         boot_gate_read(&g_bk7258_bl2_boot_policy_runtime_gate) != 0;
}

static int boot_ota_trial_lock(void *arg, uint32_t timeout_ms)
{
  (void)arg;
  return timeout_ms == 0 ? -1 : 0;
}

static void boot_ota_trial_unlock(void *arg)
{
  (void)arg;
  boot_dsb();
}

static int boot_ota_trial_write(void *arg, uint32_t address,
                                const uint8_t *data, size_t len)
{
  (void)arg;
  if (data == NULL || len != BK7258_BOOT_OTA_PROGRAM_GRANULE)
    {
      return -1;
    }

  return boot_ota_flash_program32(address, data);
}

static void boot_remap_disable(void)
{
  uint32_t enabled = OTA_REG32(FLASH_REMAP_ENABLE) & 1u;

  OTA_REG32(FLASH_REMAP_ENABLE) = OTA_REG32(FLASH_REMAP_ENABLE) & ~1u;
  boot_dsb();
  boot_isb();
  if (enabled != 0)
    {
      boot_invalidate_icache();
    }
}

static int boot_remap_secondary(void)
{
  uint32_t enable;

  boot_remap_disable();
  OTA_REG32(FLASH_REMAP_BEGIN) = REMAP_ADDRESS_BEGIN;
  OTA_REG32(FLASH_REMAP_END) = REMAP_ADDRESS_END;
  OTA_REG32(FLASH_REMAP_OFFSET) = REMAP_ADDRESS_OFFSET;
  boot_dsb();
  if (OTA_REG32(FLASH_REMAP_BEGIN) != REMAP_ADDRESS_BEGIN ||
      OTA_REG32(FLASH_REMAP_END) != REMAP_ADDRESS_END ||
      OTA_REG32(FLASH_REMAP_OFFSET) != REMAP_ADDRESS_OFFSET)
    {
      return -1;
    }

  enable = OTA_REG32(FLASH_REMAP_ENABLE) | 1u;
  OTA_REG32(FLASH_REMAP_ENABLE) = enable;
  boot_dsb();
  boot_isb();
  if ((OTA_REG32(FLASH_REMAP_ENABLE) & 1u) == 0)
    {
      boot_remap_disable();
      return -1;
    }

  boot_invalidate_icache();
  return 0;
}

static int boot_select_slot(enum bk7258_boot_ota_slot_e slot)
{
  if (slot == BK7258_BOOT_OTA_SLOT_A)
    {
      boot_remap_disable();
      return 0;
    }

  if (slot != BK7258_BOOT_OTA_SLOT_B ||
      boot_gate_read(&g_bk7258_boot_ota_remap_compile_gate) == 0 ||
      boot_gate_read(&g_bk7258_boot_ota_remap_runtime_gate) == 0)
    {
      return -1;
    }

  return boot_remap_secondary();
}

static void boot_ota_policy_init(struct bk7258_boot_ota_policy_s *policy)
{
  policy->preferred_slot = BK7258_BOOT_OTA_SLOT_A;
  policy->fallback_slot = BK7258_BOOT_OTA_POLICY_SLOT_NONE;
  policy->source = BK7258_BOOT_OTA_POLICY_FIXED;
  policy->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  policy->generation = 0;
}

static void boot_ota_bank_init(struct bk7258_boot_ota_rotation_bank_s *bank)
{
  bank->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  bank->base_slot = BK7258_BOOT_OTA_SLOT_A;
  bank->target_slot = BK7258_BOOT_OTA_SLOT_B;
  bank->valid_records = 0;
  bank->last_record_index = 0;
  bank->sequence = 0;
  bank->generation = 0;
  bank->erased = false;
  bank->trusted = false;
}

static bool boot_ota_bl2_policy_enabled(void)
{
  return boot_gate_read(&g_bk7258_bl2_boot_policy_compile_gate) != 0 &&
         boot_gate_read(&g_bk7258_bl2_boot_policy_runtime_gate) != 0;
}

static bool boot_ota_legacy_policy_enabled(void)
{
  return boot_gate_read(&g_bk7258_boot_ota_select_compile_gate) != 0 &&
         boot_gate_read(&g_bk7258_boot_ota_select_runtime_gate) != 0;
}

static int boot_ota_resolve_n15_policy(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  struct bk7258_boot_ota_policy_s *policy)
{
  struct bk7258_boot_ota_rotation_bank_s banks[2];
  struct bk7258_boot_ota_rotation_view_s view;
  struct bk7258_boot_ota_rotation_identity_s identity;
  struct bk7258_boot_ota_trial_ops_s trial_ops;
  struct bk7258_boot_ota_rotation_trial_result_s trial_result;
  enum bk7258_boot_ota_rotation_state_e next_state;
  const uint32_t bank_addresses[2] =
  {
    BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET,
    BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET
  };
  uint32_t index;
  int ret;

  for (index = 0; index < 2; index++)
    {
      boot_ota_bank_init(&banks[index]);
      ret = raw_ops->read(raw_ops->arg, bank_addresses[index],
                          g_boot_ota_metadata,
                          BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
      if (ret < 0 ||
          bk7258_boot_ota_rotation_inspect(g_boot_ota_metadata,
                                            &banks[index]) < 0)
        {
          boot_ota_bank_init(&banks[index]);
        }
    }

  ret = bk7258_boot_ota_rotation_select(banks, &view);
  if (ret < 0)
    {
      return ret;
    }

  if (!view.metadata_present)
    {
      return 0;
    }

  ret = raw_ops->read(raw_ops->arg, bank_addresses[view.selected_bank],
                      g_boot_ota_metadata,
                      BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  if (ret < 0 ||
      bk7258_boot_ota_rotation_latest(g_boot_ota_metadata, &identity) < 0 ||
      identity.state != view.state ||
      identity.generation != view.generation ||
      identity.base_slot == identity.target_slot)
    {
      return -1;
    }

  policy->source = BK7258_BOOT_OTA_POLICY_N15;
  policy->state = (uint8_t)identity.state;
  policy->generation = identity.generation;

  if (view.trial_required)
    {
      /* The pending -> trial append must complete before the new pair is
       * attempted.  If the append cannot be proven by readback, remain on the
       * stable base pair.  MCUboot will authenticate whichever pair is passed
       * to it; the legacy descriptor hash is deliberately not a second image
       * acceptance authority in this chain. */
      trial_ops.arg = NULL;
      trial_ops.compile_write_enabled = boot_ota_trial_compile_write;
      trial_ops.runtime_write_enabled = boot_ota_trial_runtime_write;
      trial_ops.lock = boot_ota_trial_lock;
      trial_ops.unlock = boot_ota_trial_unlock;
      trial_ops.read = boot_ota_raw_read;
      trial_ops.write = boot_ota_trial_write;
      next_state = identity.target_slot == BK7258_BOOT_OTA_SLOT_A ?
        BK7258_BOOT_OTA_ROTATION_TRIAL_A :
        BK7258_BOOT_OTA_ROTATION_TRIAL_B;
      if (boot_ota_install_ramfunc() == 0 &&
          bk7258_boot_ota_rotation_trial_transition(
            bank_addresses[view.selected_bank], identity.generation,
            identity.state, next_state, &trial_ops, 1,
            g_boot_ota_metadata, g_boot_ota_scratch, &trial_result) == 0 &&
          trial_result.current_boot_trial)
        {
          policy->preferred_slot = (uint8_t)identity.target_slot;
          policy->fallback_slot = (uint8_t)identity.base_slot;
          policy->state = (uint8_t)next_state;
        }
      else
        {
          policy->preferred_slot = (uint8_t)identity.base_slot;
          policy->fallback_slot = BK7258_BOOT_OTA_POLICY_SLOT_NONE;
        }
    }
  else if (identity.state == BK7258_BOOT_OTA_ROTATION_CONFIRMED_A ||
           identity.state == BK7258_BOOT_OTA_ROTATION_CONFIRMED_B)
    {
      policy->preferred_slot = (uint8_t)identity.target_slot;
      policy->fallback_slot = (uint8_t)identity.base_slot;
    }
  else
    {
      /* TRIAL means the one allowed trial boot was already consumed;
       * ROLLBACK explicitly chooses the base.  Neither state may fall back to
       * the rejected target merely because its signature remains valid. */
      policy->preferred_slot = (uint8_t)identity.base_slot;
      policy->fallback_slot = BK7258_BOOT_OTA_POLICY_SLOT_NONE;
    }

  return 0;
}

int boot_ota_resolve_policy(struct bk7258_boot_ota_policy_s *policy)
{
  struct bk7258_boot_ota_raw_ops_s raw_ops;
  uint8_t n17_slot;
  int n17_ret;
  int ret;

  if (policy == NULL)
    {
      return -1;
    }

  boot_ota_policy_init(policy);

  raw_ops.arg = NULL;
  raw_ops.read = boot_ota_raw_read;

  /* N17 observes the policy and both metadata banks before format-2 is
   * allowed to run.  An erased policy plus no format-3 journal returns zero
   * and keeps the deployed N15 behavior unchanged.  Any present N17 state
   * requires its own two gates, cryptographic wrapper and pair hash to pass;
   * it may never be interpreted as format-2 data.
   */

  n17_ret = bk7258_boot_n17_select(
    &raw_ops, g_boot_ota_metadata, g_boot_ota_scratch,
    g_boot_ota_scratch + BK7258_BOOT_OTA_METADATA_SIZE,
    sizeof(g_boot_ota_scratch) - BK7258_BOOT_OTA_METADATA_SIZE,
    boot_gate_read(&g_bk7258_boot_n17_select_compile_gate) != 0 &&
    boot_gate_read(&g_bk7258_boot_n17_select_runtime_gate) != 0,
    &n17_slot);
  if (n17_ret < 0)
    {
      boot_ota_clear_workspace();
      return -1;
    }

  if (n17_ret > 0)
    {
      if (n17_slot != BK7258_BOOT_OTA_SLOT_A &&
          n17_slot != BK7258_BOOT_OTA_SLOT_B)
        {
          boot_ota_clear_workspace();
          return -1;
        }

      policy->preferred_slot = n17_slot;
      policy->source = BK7258_BOOT_OTA_POLICY_N17;
      boot_ota_clear_workspace();
      boot_remap_disable();
      return 0;
    }

  /* N15 and N17 share the fixed pre-handoff workspace.  The N17 probe must
   * not leave journal bytes that a later format-2 validation could mistake
   * for scratch state.
   */

  boot_ota_clear_workspace();

  if (!boot_ota_bl2_policy_enabled() && !boot_ota_legacy_policy_enabled())
    {
      boot_ota_clear_workspace();
      boot_remap_disable();
      return 0;
    }

  boot_remap_disable();
  ret = boot_ota_resolve_n15_policy(&raw_ops, policy);
  boot_ota_clear_workspace();
  boot_remap_disable();
  return ret;
}

uint32_t boot_ota_select_app(uint32_t primary_app_vector)
{
  struct bk7258_boot_ota_policy_s policy;

  if (primary_app_vector != CP_APP_VECTOR ||
      boot_ota_resolve_policy(&policy) < 0 ||
      boot_select_slot((enum bk7258_boot_ota_slot_e)
                       policy.preferred_slot) < 0)
    {
      return 0;
    }

  return primary_app_vector;
}
