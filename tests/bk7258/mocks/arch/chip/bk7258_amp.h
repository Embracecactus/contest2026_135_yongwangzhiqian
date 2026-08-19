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

volatile struct bk7258_ap_boot_state_s *bk7258_ap_boot_state(void);

#define BK7258_AP_STATE_READY  2u
#define BK7258_AP_EVENT_NONE   0u

/* Real chip constants consumed by the BL2 handoff vector validation. */
#define BK7258_CP_RAM_BASE   0x28010000u
#define BK7258_CP_RAM_SIZE   0x00040000u

#endif /* __MOCK_ARCH_CHIP_BK7258_AMP_H */
