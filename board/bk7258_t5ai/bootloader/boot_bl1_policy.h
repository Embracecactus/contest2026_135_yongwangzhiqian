/* Reversible BL1 slot-order and Manifest-version policy.
 *
 * The complete 0x28-byte record matches the public Beken boot_flag partition
 * description and the BK7236 BootROM consumer recovered from the official
 * reference binary.  It is distinct from the preceding 4 KiB bl1_control
 * vector page.  This module does not choose a Flash location, write the
 * boot_flag partition, or access OTP.
 */
#ifndef BK7258_BOOT_BL1_POLICY_H
#define BK7258_BOOT_BL1_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "boot_bl1_boot_flag_core.h"

enum bk7258_bl1_slot_e
{
  BK7258_BL1_SLOT_PRIMARY = 0,
  BK7258_BL1_SLOT_SECONDARY = 1
};

enum bk7258_bl1_boot_flag_status_e
{
  BK7258_BL1_BOOT_FLAG_ABSENT = 0,
  BK7258_BL1_BOOT_FLAG_ERASED,
  BK7258_BL1_BOOT_FLAG_INVALID,
  BK7258_BL1_BOOT_FLAG_VALID_PRIMARY,
  BK7258_BL1_BOOT_FLAG_VALID_SECONDARY
};

enum bk7258_bl1_boot_flag_status_e
bk7258_bl1_boot_flag_slot_order(const uint8_t *record, size_t size,
                              const struct bk7258_bl1_boot_flag_layout_s *layout,
                              uint8_t order[2]);

/* Decode the BL1 unary OTP counter exactly as recovered from the BK7236
 * BootROM: count consecutive one bits from bit 0 and stop at the first zero.
 * The BK7258 board backend overrides the weak read hook with its verified
 * read-only OTP shadow address.  Version programming is outside this API. */
uint32_t bk7258_bl1_security_counter_decode(uint32_t bitmap);

uint32_t bk7258_bl1_manifest_version_floor_readonly(void);

uint32_t bk7258_bl1_manifest_effective_floor(uint32_t readonly_floor);

int bk7258_bl1_manifest_version_allowed(uint32_t manifest_version,
                                        uint32_t readonly_floor);

#endif /* BK7258_BOOT_BL1_POLICY_H */
