/* SPDX-License-Identifier: Apache-2.0 */
/* Board-owned read-only security-counter backend for MCUboot.
 *
 * Official v3.1.1.9 BK7258 otp1.csv places the 64-byte BL2 counter at
 * physical OTP 0x200.  The verified Dubhe shadow window starts at physical
 * OTP 0x100, so the read-only alias is 0x4b111100.  Beken's TF-M counter
 * implementation represents a version by the number of programmed bits.
 *
 * This backend never initializes the OTP controller and never writes it.  A
 * compile-time floor is retained as a reversible development/test minimum;
 * the effective floor is the greater of that value and the OTP bitmap count.
 * Since the update hook remains read-only, this does not claim monotonic
 * advancement or production anti-rollback provisioning.
 */

#include <stddef.h>
#include <stdint.h>

#include <bootutil/security_cnt.h>

#ifndef BK7258_BL2_SECURITY_COUNTER_FLOOR
#  define BK7258_BL2_SECURITY_COUNTER_FLOOR 0u
#endif

#define BK7258_DUBHE_OTP_SHADOW_BASE                 0x4b111000u
#define BK7258_DUBHE_OTP_BL2_SECURITY_COUNTER_OFFSET 0x100u
#define BK7258_DUBHE_OTP_BL2_SECURITY_COUNTER_SIZE   64u

#define BK7258_BL2_OTP_REG32(addr) \
  (*(volatile uint32_t *)(uintptr_t)(addr))

static uint32_t bk7258_bl2_security_counter_readonly(void)
{
  uint32_t count = 0u;
  uint32_t word;
  uint32_t bit;
  size_t index;

  for (index = 0u;
       index < BK7258_DUBHE_OTP_BL2_SECURITY_COUNTER_SIZE / sizeof(uint32_t);
       index++)
    {
      word = BK7258_BL2_OTP_REG32(
        BK7258_DUBHE_OTP_SHADOW_BASE +
        BK7258_DUBHE_OTP_BL2_SECURITY_COUNTER_OFFSET +
        index * sizeof(uint32_t));
      for (bit = 0u; bit < 32u; bit++)
        {
          count += (word >> bit) & 1u;
        }
    }

  return count;
}

static uint32_t bk7258_bl2_security_counter_floor(void)
{
  uint32_t otp_floor = bk7258_bl2_security_counter_readonly();

  return otp_floor > BK7258_BL2_SECURITY_COUNTER_FLOOR ?
         otp_floor : BK7258_BL2_SECURITY_COUNTER_FLOOR;
}

fih_ret boot_nv_security_counter_init(void)
{
  return FIH_SUCCESS;
}

fih_ret boot_nv_security_counter_get(uint32_t image_id,
                                     fih_int *security_cnt)
{
  (void)image_id;

  if (security_cnt == NULL)
    {
      return FIH_FAILURE;
    }

  *security_cnt = fih_int_encode((int)bk7258_bl2_security_counter_floor());
  return FIH_SUCCESS;
}

int32_t boot_nv_security_counter_update(uint32_t image_id,
                                        uint32_t img_security_cnt)
{
  /* Direct-XIP still calls the MCUboot update hook after a valid boot.  This
   * board phase is deliberately read-only: accept an image already at/above
   * the effective floor, but never attempt to advance OTP. */
  (void)image_id;
  return img_security_cnt >= bk7258_bl2_security_counter_floor() ? 0 : -1;
}
