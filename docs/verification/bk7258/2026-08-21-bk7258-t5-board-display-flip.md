# BK7258 T5-Board display page-flip checkpoint

Date: 2026-08-21

## Conclusion

The original Vela-Claw display path updated a single PSRAM scanout buffer in
place. A static driver-owned color pattern was stable on the same hardware,
while the dynamic UI visibly flickered. Replacing the UI path with two full
RGB565 framebuffer pages and an EOF-synchronized NuttX pan/VSYNC contract made
the display substantially better, but did not eliminate flicker in gray
regions. The phase is therefore an accepted partial improvement, not closure.

## Source and build evidence

- Branch base: `9033986abb67d11b3776437b3605e70f15436d54`
- Implementation: `ffa4281`
- Direct layout: `bk7258-5641c11040abf787`
- CP config SHA-256: `6d06b3102a10c32d3a0e9d7c40f780d149df0c056794bc22eec4d391e6066c62`
- AP config SHA-256: `af8e68c8ea79ea89bddbaf0b43224abbb0112581be0fd28e7c4a4f8be622c4ee`
- Final host-built AP Flash segment: `d08635fe2d8a2f1021172b9c5f21bde087f63d597b736210fa16e1830aabd8d2`
- Clean direct CP/AP build: PASS
- Final map/symbol checks: standard FB sync/pan consumers present; Vela display
  flush has no libc `memcpy` reference.

## Hardware evidence

- Transport: COM3/BKFIL, exact AP offset `0x165000`; no chip erase.
- Static-pattern AP: `f741da6e028d28f7d317301df0ac82d2eaec7cd9cc7ab005a18ce6131d824253`.
  AP reached READY and the owner observed no flicker.
- Double-full-frame predecessor AP:
  `28e1093f62870ba2c157dc29d64750283d6f872eb6b67a0608d0ce20190427ef`.
  AP reached READY/error 0. The owner observed a substantial improvement over
  the original single-scanout UI, with residual flicker most visible in gray
  blocks.
- The final `d08635fe...` artifact differs only by making the FB_SYNC Kconfig
  contract self-contained and removing an unused field; it was host-built but
  not reflashed in this checkpoint.

## Narrowed diagnosis

- The verified panel transport, 15 MHz RGB timing, data edge, 20/4 sync width,
  backlight and reset path can produce a stable static image.
- TuyaOpen uses the same panel sequence and board timing but presents complete
  frames through an EOF-controlled RGB pipeline. That comparison motivated the
  standard NuttX page-flip implementation; no Tuya framework was copied.
- 30 MHz caused a cropped three-quarter image and still flickered. SDK 2/2
  fallback prevented AP READY. Explicit framebuffer store width and SDK/GCC10
  substitutions did not fix the original behavior.

## Open gate

The remaining gray-level flicker requires a single-variable comparison of
ILI9488 VCOM/inversion/frame-control settings or page-flip cadence. Do not claim
display closure, signed-delivery acceptance or production hardware trust from
this checkpoint.
