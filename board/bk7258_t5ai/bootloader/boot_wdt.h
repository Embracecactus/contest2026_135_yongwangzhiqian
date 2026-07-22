/*
 * boot_wdt.h - BK7258 Tier-1 bootloader WDT (watchdog timer) helpers.
 *
 * Freestanding (no libc, stack-only, no .bss/.data).  Provides WDT init
 * and feed for the product-grade bootloader: if the cold-init DPLL sequence
 * or app validation hangs, the WDT resets the chip within ~8 seconds.
 *
 * Mirrors the Armino SDK WDT unlock/feed protocol (wdt_reg.h, wdt_ll.h,
 * wdt_hal.c) and the vendor bootloader's sub_2000FE4 / sub_2001010
 * (bk-official-bootloader-reverse.md §2.4).
 *
 * BK7258 has two watchdogs:
 *   APB_WDT @ 0x44800000 (ctrl @ +0x10, status @ +0x04)
 *   AON_WDT @ 0x44000600 (ctrl @ +0x00, same key protocol)
 *
 * ctrl register (wdt_struct.h):
 *   period[15:0]  — timeout value (WDT clock ticks; SDK default 8000 ≈ 8s)
 *   key[23:16]    — unlock/feed key (0x5A first, 0xA5 second)
 *
 * Key sequence (two consecutive writes to ctrl):
 *   1st write: (0x5A << 16) | period   — unlock
 *   2nd write: (0xA5 << 16) | period   — apply (WDT armed)
 *
 * Feed: clear APB_WDT status bit[1:0], then reinit (same key+period).
 */

#ifndef __BOOTLOADER_BOOT_WDT_H
#define __BOOTLOADER_BOOT_WDT_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* MMIO.                                                              */
/* ------------------------------------------------------------------ */

#define REG32(addr)       (*(volatile uint32_t *)(addr))

/* SYS block (for SYS_CTRL WDT clock/reset config). */
#define SYS_CTRL_REG      0x44000008u

/* APB WDT (NMI WDT / main WDT). */
#define WDT_APB_BASE      0x44800000u
#define WDT_APB_CTRL      (WDT_APB_BASE + 0x10u)   /* period + key */
#define WDT_APB_STATUS    (WDT_APB_BASE + 0x04u)   /* status (write 0 to clear bit[1:0]) */

/* AON WDT (always-on WDT). */
#define WDT_AON_BASE      0x44000600u
#define WDT_AON_CTRL      WDT_AON_BASE              /* same ctrl layout */

/* Key values (wdt_reg.h:35-36). */
#define WDT_KEY1          0x5Au
#define WDT_KEY2          0xA5u

/* Timeout period.  8000 ≈ 8 seconds (matches SDK CONFIG_INT_WDT_PERIOD_MS).
 * The WDT clock is ~1 kHz (ROSC 32 kHz / 32 or similar), so period in
 * the register is approximately milliseconds.  8 s covers cold-init
 * (worst-case ~100 ms) with 80× margin; a real hang resets in ≤ 8 s. */
#define WDT_PERIOD        8000u

/* SYS_CTRL bits[5:0] = 0x26: vendor bootloader's WDT clock/reset config
 * (bk-official-bootloader-reverse.md §2.4, sub_2000FE4: bic.w r1,r3,#0x3F
 * then orr.w ip,r1,#0x26). */
#define WDT_SYSCTRL_MASK  0x3Fu
#define WDT_SYSCTRL_VAL   0x26u

/* ------------------------------------------------------------------ */
/* Public (static inline, stack-only, no libc).                        */
/* ------------------------------------------------------------------ */

/* Initialize both WDTs with the vendor key sequence.
 * Called once at the start of c_main().  After this, the WDT is armed
 * and will reset the chip in ~8 s unless boot_wdt_feed() is called. */
static inline void boot_wdt_init(void)
{
    uint32_t v;
    uint32_t ctrl1;
    uint32_t ctrl2;

    /* 1. SYS_CTRL: configure WDT clock source + reset enable
     * (mirrors vendor sub_2000FE4: bic #0x3F, orr #0x26). */
    v = REG32(SYS_CTRL_REG);
    v = (v & ~WDT_SYSCTRL_MASK) | WDT_SYSCTRL_VAL;
    REG32(SYS_CTRL_REG) = v;

    ctrl1 = (uint32_t)(WDT_KEY1 << 16) | (WDT_PERIOD & 0xFFFFu);
    ctrl2 = (uint32_t)(WDT_KEY2 << 16) | (WDT_PERIOD & 0xFFFFu);

    /* 2. Unlock + arm APB_WDT (two-step key). */
    REG32(WDT_APB_CTRL) = ctrl1;
    REG32(WDT_APB_CTRL) = ctrl2;

    /* 3. Unlock + arm AON_WDT (same protocol). */
    REG32(WDT_AON_CTRL) = ctrl1;
    REG32(WDT_AON_CTRL) = ctrl2;
}

/* Feed (kick) the WDT.  Clears the APB_WDT status and re-arms with the
 * same period, mirroring vendor sub_2001010 (clear status + reinit).
 * Call this at key points in c_main to prevent premature reset. */
static inline void boot_wdt_feed(void)
{
    uint32_t ctrl1;
    uint32_t ctrl2;

    /* Clear APB_WDT status bit[1:0]. */
    REG32(WDT_APB_STATUS) = REG32(WDT_APB_STATUS) & ~0x3u;

    ctrl1 = (uint32_t)(WDT_KEY1 << 16) | (WDT_PERIOD & 0xFFFFu);
    ctrl2 = (uint32_t)(WDT_KEY2 << 16) | (WDT_PERIOD & 0xFFFFu);

    /* Re-arm APB_WDT (= feed). */
    REG32(WDT_APB_CTRL) = ctrl1;
    REG32(WDT_APB_CTRL) = ctrl2;
}

#endif /* __BOOTLOADER_BOOT_WDT_H */