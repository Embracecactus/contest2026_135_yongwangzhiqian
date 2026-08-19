/*
 * mock bootutil/security_cnt.h - host mock of the MCUboot security-counter
 * ABI (mirrors the upstream bootutil header shape the board backend uses).
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MOCK_BOOTUTIL_SECURITY_CNT_H
#define MOCK_BOOTUTIL_SECURITY_CNT_H

#include <stdint.h>
#include "bootutil/fault_injection_hardening.h"

#ifdef __cplusplus
extern "C"
{
#endif

  fih_ret boot_nv_security_counter_init(void);
  fih_ret boot_nv_security_counter_get(uint32_t image_id,
                                       fih_int *security_cnt);
  int32_t boot_nv_security_counter_update(uint32_t image_id,
                                          uint32_t img_security_cnt);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_BOOTUTIL_SECURITY_CNT_H */
