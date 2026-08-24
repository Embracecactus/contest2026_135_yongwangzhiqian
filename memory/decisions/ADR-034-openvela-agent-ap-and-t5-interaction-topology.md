# ADR-034: Official openvela Agent on the T5Board AP

Status: accepted
Date: 2026-08-24

## Decision

- Retire the repository-local `app/vela_claw` implementation.
- Run the official `packages/ai_agent` application on the AP core, where the
  NuttX network device, PSRAM, LCD, GT1151 touch and microphone live.
- Keep the CP core responsible for platform diagnostics, OTA and the UART0
  NSH console.
- Keep UART1 disabled while the board switch selects J-Link SWD.  Use LCD and
  touch as the primary Agent interaction and SWD/RTT for AP diagnostics.
- Use LCD/touch in the primary profile.  Camera remains a later runtime-mux
  phase because the current DVP and RGB/touch bindings share physical pins.
- Configure the inserted TF card as one-bit, present-before-boot media without
  card-detect; do not enable four-bit mode or hotplug assumptions.

## Consequences

- The Agent profile uses NuttX generic LVGL framebuffer/touchscreen ports.
- Large Agent task stacks use a reserved 320 KiB AP PSRAM system-heap region.
- The memory-constrained Agent profile uses one RGB565 framebuffer; the
  BK7258 driver still supports and has exercised the generic two-page ABI.
- The Agent uses a dedicated partition CSV instead of changing the base board
  layout.  The CSV remains the single source of truth for partition addresses
  and sizes and must be selected explicitly when building the Agent package.
- CP owns the on-chip persistent filesystem at `/data`; AP consumes it through
  RPMsgFS at `/cpdata`.  Fonts and other Agent resources are provisioned there
  without moving the immutable factory/calibration tail.
- The board chip layer supplies narrow recorder and PCM-player ABI bridges
  over NuttX audio while the full media framework remains disabled.
- Local Vela Claw source/config/manifest hooks are removed rather than kept as
  a second application architecture.
