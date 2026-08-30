/*
 * mock bootutil/fault_injection_hardening.h - minimal FIH surface for the
 * BK7258 BL2 security-counter backend on the host.
 *
 * The real MCUboot header pulls in mcuboot_config and profile logic; the
 * board backend only needs the fih_ret/fih_int types and the encode helper.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MOCK_BOOTUTIL_FAULT_INJECTION_HARDENING_H
#define MOCK_BOOTUTIL_FAULT_INJECTION_HARDENING_H

#include <stdint.h>

typedef int fih_ret;
typedef int fih_int;

#define FIH_SUCCESS (0)
#define FIH_FAILURE (-1)

static inline fih_int fih_int_encode(int value)
{
  return value;
}

static inline int fih_int_decode(fih_int value)
{
  return value;
}

#endif /* MOCK_BOOTUTIL_FAULT_INJECTION_HARDENING_H */
