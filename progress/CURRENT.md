# Current Progress

Last updated: 2026-08-21
Updated by: Codex

## Objective

Finish the T5-Board Vela-Claw display stabilization without changing the
verified BL1/BL2/MCUboot chain or masking the remaining gray-level flicker.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `fix/bk7258-vela-claw-poweron-flicker`
- Base: `origin/dev-ai-contest-2026@9033986abb67d11b3776437b3605e70f15436d54`
- Display implementation commit: `ffa4281` (`fix(bk7258): synchronize
  Vela-Claw framebuffer flips`); not pushed.
- Owner-untracked logs, `bootloader.tmp`, doc-stress helpers and
  `build_package.sh` remain untouched.

## Implemented

- `VELA_CLAW_UI` now selects the standard NuttX framebuffer sync contract.
- The BK7258 framebuffer exposes two contiguous full RGB565 pages only when
  `FB_SYNC` is enabled (`320x960`, 614400 bytes total).
- `FBIOPAN_DISPLAY` requests are consumed at RGB EOF; the ISR validates the
  page offset, switches the LCD base, removes the queued pan, signals the
  matching flip semaphore and publishes VSYNC.
- LVGL 9 renders directly into the two full framebuffer pages and waits for
  `FBIO_WAITFORVSYNC`; the former half-frame PSRAM draw buffer and memcpy into
  the active scanout were removed. The LVGL 8 fallback remains unchanged.

## Verified result

- [Display page-flip hardware checkpoint](verification/2026-08-21-bk7258-t5-board-display-flip.md)
- Final clean direct CP/AP build passed with layout
  `bk7258-5641c11040abf787`, AP config SHA-256 `af8e68c8...622c4ee` and AP
  Flash segment SHA-256 `d08635fe...abd8d2`.
- Final ELF contains `bk7258_lcd_waitforvsync`, `fb_peek_paninfo`,
  `fb_remove_paninfo` and `fb_notify_vsync`; the Vela UI object no longer
  references `memcpy` for display flush.
- A functionally equivalent predecessor AP (`28e1093f...90427ef`) was written
  at `0x165000` without chip erase. AP reached `READY` with error 0. The owner
  observed a large reduction in flicker, but gray regions still visibly
  flicker; this is not a completed visual fix.
- With the same LCD/panel/timing/IRQ path and Vela UI disabled, the static
  four-color validation pattern was completely stable. This confines the
  remaining defect to dynamic UI/display behavior rather than backlight,
  panel reset or basic RGB scanout.
- `git diff --check` passed before the implementation commit.

## Rejected explanations / boundaries

- BL1/BL2/MCUboot was excluded by a direct/raw AP comparison.
- Replacing framebuffer memcpy with explicit 32/16-bit stores did not remove
  flicker. Historical GCC10/SDK bundle inputs produced the same linked AP
  bytes for this path.
- A 30 MHz-only image still flickered and displayed only the right-hand
  three quarters. Keep the Tuya-matching 15 MHz timing.
- Removing the project sync-width restore left the SDK fallback at 2/2 and
  prevented AP from reaching READY. Keep the working 20/4 register value.
- The current display changes have not received a signed build/package run.
  Current hardware runs a direct unsigned diagnostic image.
- J-Link could not connect because of its reset-pin state. No OTP/eFuse,
  lifecycle, debug-lock or chip-erase operation was performed.

## Exact next action

Resume from `ffa4281` and isolate the remaining gray-level flicker. Start with
the ILI9488 VCOM/inversion/frame-control behavior and page-flip cadence; do not
raise the RGB pixel clock or redo the already rejected boot, SDK, memcpy,
backlight, reset, GPIO15 or sync-width experiments. Rebuild and Flash only the
AP segment for each single-variable comparison, then run a final signed
end-to-end build only after the visual result is stable.
