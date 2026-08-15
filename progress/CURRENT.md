# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Current task

Publish the chip-first BK7258 JPEG decoder V4L2 memory-to-memory stage from
`feat/bk7258-jpeg-v4l2-m2m`, rebased onto upstream SARADC baseline `a9f832f`,
for owner review through the GitHub Web UI.  The implementation and bounded
signed-board verification are complete; no further build or download is
required unless publication review changes executable source or configuration.

## Verified baseline

- Upstream `dev-ai-contest-2026` at `a9f832f` contains the verified Audio DAC
  and DAC hardware-EQ stages plus the SARADC controller/IPC baseline:
  - [DAC hardware-EQ register lifecycle](verification/2026-08-15-bk7258-audio-dac-eq.md)
  - [Audio DAC and fixed-32 kHz baseline](verification/2026-08-15-bk7258-audio-dac.md)
  - [SARADC and T5-Board ADC-key baseline](verification/2026-08-15-bk7258-saradc-adc-key.md)
- SARADC remains correctly classified PARTIAL for the physical endpoint: the
  released ADC14 baseline passed, but a real SW5 released/pressed/released run
  is still pending.
- The JPEG branch has an exact signed `18.6.80` board PASS with protected
  security counter `0x12060050`:
  - [JPEG V4L2 M2M bounded board verification](verification/2026-08-15-bk7258-jpeg-v4l2-m2m.md)

## Completed in this stage

- Hardened the existing typed JPEG helper before exposing user input: bounded
  baseline parser, validated DQT/DHT/SOF/SOS/DRI/entropy structure, guarded
  `bytesused + 2048` input bounce, whole-line cache ownership, faulted-backend
  recovery and retryable uncertain teardown.
- Added standard NuttX `/dev/video1` V4L2 M2M: JPEG OUTPUT, tightly packed YUYV
  CAPTURE, USERPTR, single open and a dedicated one-thread hardware owner.
  STREAMOFF and close wait for work and return every queued buffer once.
- Added a chip-generic validator for the public ioctl sequence, second-open
  and MMAP rejection, valid decode, locally rejected truncation, recovery and
  unmatched-buffer drain.  No camera, LCD, pin, DMA2D or RGB dependency was
  introduced.
- Added the exact T5-Board validation pair and fail-closed profile, fixture,
  ELF/map-owner and SDK-provenance gates for both build backends.
- The first `18.6.79` board run proved JPEG but exposed RPTUN permanently at
  CONNECTING when optional test and supervisor services were absent.
- Fixed that chip-level ownership error with a generation-qualified,
  transport-owned NS CREATE/ACK endpoint.  It adds no worker and does not
  enable optional services.  CP 47/47 and AP 32/32 proof tests plus the full
  existing mailbox/PM/trust suite pass.
- Rebuilt and signed `18.6.80`, passed public-identity preflight, and wrote only
  CP `[0x11000,0x3f000)` plus AP `[0x165000,0x18c000)` through COM3.
- Two non-halting `BJMV` reads were identical: PASSED/result 0/COMPLETE/26,
  valid and recovery 1024-byte YUYV results with matching CRC/statistics,
  expected negative/drain flags, and no residual JPEG owner, orphan or PM
  vote.
- AP remained READY and its heartbeat advanced; CPU2 remained scheduler
  online.  Two RPTUN reads were CONNECTED with flags `0x7f3f`, error/pending
  zero and generation 1/1, closing the `18.6.79` state-publication gap.

## Exact next action

Use a Web PR with base `open-vela:dev-ai-contest-2026` and head
`Embracecactus:feat/bk7258-jpeg-v4l2-m2m`, review the bounded `18.6.80`
evidence, and merge.  Keep the separately merged SARADC endpoint result at
PARTIAL until a real SW5 transition run is completed.

## Remaining boundaries

- The verified fixture is one 914-byte baseline 4:2:2 JPEG at 32 x 16,
  USERPTR and one open owner.  Matching nonconstant CRC/statistics are a real
  hardware smoke, not reference-decoder pixel accuracy or broad conformance.
- Progressive, grayscale, multiscan, arithmetic and trailing-data input,
  MMAP, source change, scaling, stride selection, DMA2D/RGB565, camera/LCD,
  multiple owners, concurrent media and long stress are not covered.
- Formats freeze after the first buffer-size/REQBUFS query because the current
  NuttX codec upper half does not expose successful REQBUFS teardown; callers
  close and reopen to renegotiate.
- The public-trust gate proves compatibility with installed development public
  identities, not production OTP/eFuse secure boot.  This run did not perform
  Flash readback, slot-B/rollback validation or an independent cold power
  cycle.
- The final auto-debug run used `--no-console`; UART logs are not evidence for
  this PASS.  JPEG, cleanup, AP/CPU2 and RPTUN conclusions come from the two
  stable SWD snapshots recorded in the verification file.

## Fixed constraints

- Do not modify official NuttX/apps or Beken SDK source trees.
- Preserve validated SARADC, Audio, DAC-EQ, MIC, TF and unrelated profiles.
- Preserve unrelated untracked artifacts; do not inspect N17 or another
  historical trust domain.
- Never open COM4.  Use COM3 and J-Link only where the selected profile allows.
- Normal downloads require the non-halting trust preflight and apps-only write
  set.  Do not write BL1/BL2, manifests, slot B, OTP/eFuse, lifecycle,
  calibration or data regions without explicit authority.
- Keep controller mechanics in `chip/`; keep pins, polarity, attached devices
  and physical validation policy in the selected-board binding.
- GPT-5.6-Luna delegation remains disabled.
