/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/arch/chip/bk7258_amp.h
 *
 * Host shim for the AP boot-state ABI.  The implementation only needs the
 * `generation` / `state` fields (read by bk7258_rptun_mbox_dispatch() and
 * bk7258_rptun_mbox_probe()) plus bk7258_ap_boot_state() returning a shared
 * struct the test can poke.  All the real layout static_asserts are dropped;
 * this is a behavioral model, not a memory map.
 ****************************************************************************/

#ifndef __MOCK_ARCH_CHIP_BK7258_AMP_H
#define __MOCK_ARCH_CHIP_BK7258_AMP_H

#include <stdint.h>

struct bk7258_ap_boot_state_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t generation;     /* gate used by dispatch() */
  uint32_t command;
  uint32_t state;          /* checked against BK7258_AP_STATE_READY */
  uint32_t error;
  uint32_t last_event;
  uint32_t reserved[32];
};

struct bk7258_ap_supervisor_health_token_s
{
  uint32_t generation;
  uint32_t sample_sequence;
  uint32_t flags;
  uint32_t healthy_age_ms;
  uint32_t sample_age_ms;
};

struct bk7258_ap_image_desc_s
{
  uint32_t slot_start;
  uint32_t slot_end;
  uint32_t vector_addr;
};

int bk7258_ap_control_initialize(const struct bk7258_ap_image_desc_s *image);
int bk7258_ap_start(uint32_t timeout_ms);

volatile struct bk7258_ap_boot_state_s *bk7258_ap_boot_state(void);

#ifdef CONFIG_BK7258_AP_SUPERVISOR
int bk7258_ap_supervisor_health_token(
  uint32_t expected_generation, uint32_t max_age_ms,
  struct bk7258_ap_supervisor_health_token_s *token);
#endif

#define BK7258_AP_STATE_READY  2u
#define BK7258_AP_EVENT_NONE   0u

#define BK7258_AP_ERROR_NONE            0u
#define BK7258_AP_ERROR_BAD_BOOT_STATE  1u
#define BK7258_AP_ERROR_PSRAM           24u
#define BK7258_AP_ERROR_PERIPHERALS     26u

/* Real chip constants consumed by the BL2 handoff vector validation. */
#define BK7258_CP_RAM_BASE   0x28010000u
#define BK7258_CP_RAM_SIZE   0x00040000u

/* Real shared-memory bounds consumed by the public RPTUN layout header. */

#define BK7258_RPTUN_SHMEM_BASE  0x28097000u
#define BK7258_RPTUN_SHMEM_SIZE  0x00008000u
#define BK7258_SHARED_RAM_BASE   0x2809f000u

#endif /* __MOCK_ARCH_CHIP_BK7258_AMP_H */
