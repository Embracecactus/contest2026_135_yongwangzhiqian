# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Current task

Publish the completed BK7258 T5-Board AP Audio DAC stage from branch
`feat/bk7258-audio-dac`, based on merged upstream commit `a8cc60b`, for the
owner to review and merge through the GitHub Web UI.

## Verified baseline

- Upstream `dev-ai-contest-2026` contains the completed T5-Board TF four-bit
  stage as squash commit `a8cc60b`; its tree is identical to published commit
  `f3ad658f`.
- The T5-Board Audio DAC stage is now physically verified with signed MCUboot
  pair `18.6.76`, security counter `130`:
  - [Audio DAC and fixed-32 kHz verification](verification/2026-08-15-bk7258-audio-dac.md)
  - [T5-Board TF/SDIO verification](verification/2026-08-14-bk7258-t5-board-tf.md)
  - [Pre-flash trust-chain verification](verification/2026-08-14-bk7258-preflash-trust-chain.md)
- The block-diagram audit distinguishes a registered and validated NuttX
  device from an SDK symbol, an internal helper, or a controller without a
  board binding.  A peripheral not fitted on T5-Board may still be
  implemented, but hardware claims require a meaningful physical endpoint.

## Completed in this stage

- `CONFIG_BK7258_AUD` is a DAC-only NuttX playback lower half at
  `/dev/audio/pcm0p`.  It uses the pinned v3.1.1.9 repeat-GDMA/ring-buffer ABI,
  accepts primed APBs before `START`, returns every accepted APB, and performs
  ordered mute/DMA/DAC/PA teardown.
- The selected board owns the speaker electrical binding.  T5-Board uses P28
  (`SPK_CTL`) and T5AI-Core uses P39 (`MUTE_N`); both are active-high with
  board-owned PA delays.  The shared chip wrapper contains no board pin.
- The first published contract is deliberately fixed to mono PCM S16,
  16 kHz, 320 samples per 20 ms DMA frame and eight explicit APBs.  The
  validator executes one explicit `STOP` session and one naturally drained
  `FINAL` session through the public NuttX audio ABI.
- Runtime priority is bounded as producer 246 > audio refill worker 245 >
  board-default transport 225.  The Audio session owns a 480 MHz SDK-tier PM
  vote from `RESERVE` through successful `RELEASE`.
- CP physical CPU0 and AP-primary physical CPU1 use an external fixed 32 kHz
  scheduler SysTick.  For timer accounting, DVFS refreshes their role-local
  DWT conversion while the scheduler source remains fixed.  The dedicated
  non-RTT CP profile also executed one coordinated standby and restored the CP
  periodic timer through the hard-IRQ arch-timer compensation proxy.
- The final Audio v5 diagnostic passed twice without mutation: 63 enqueues and
  63 dequeues, two completes, two symmetric DMA/DAC/PA lifecycles, zero
  underruns, zero residual resources, zero tick/CLOCK_MONOTONIC regressions,
  and two AP-primary `120 -> 480 -> 120 MHz` cycles.
- CP one-shot PM evidence is stable: one entry, one wake, 14,406 us sleep,
  one compensated whole tick, final reason `ENTERED`, and no active AP or SDK
  votes.  AP, CPU2 and RPTUN heartbeats continued after restore.

## Exact next action

Open a Web PR from `feat/bk7258-audio-dac` to `dev-ai-contest-2026`, review and
merge it.  After the merge is confirmed, start the next bounded block-diagram
driver stage without carrying forward generated bootloader, legacy SDK-tree or
hardware-log artifacts.

## Remaining boundaries

- T5-Board routes the class-D output to connector P6 but has no fitted
  loudspeaker.  DMA/DAC/PA/lifecycle correctness is automatic PASS; audible
  output still requires an external speaker or instrument on P6.
- The hardware result covers only mono S16/16 kHz/320-sample frames/eight
  explicit APBs, two lifecycle cycles and one CP standby.  It is not evidence
  for shorter frames, compressed/file-backed feeders, Wi-Fi/BT load, long
  soak, shared/mmap audio buffers or full duplex.
- Fixed 32 kHz removes the observed Audio-DVFS SysTick regression.  The
  bounded v5 probes sample transition points; they do not prove that every
  possible `CLOCK_MONOTONIC` read on every AP SMP core is globally monotonic.
- AP coordinated standby still lacks AON elapsed-time compensation.  This
  stage verifies CP one-shot compensation and post-wake AP liveness, not
  complete CP/AP standby time continuity.
- Microphone and speaker validation profiles remain mutually exclusive.
  Shared full-duplex AUD clock ownership is a later phase.

## Fixed constraints

- Do not modify official NuttX/apps or Beken SDK source trees.
- Preserve validated MIC, TF one-/four-bit and unrelated profile behavior.
- Preserve unrelated untracked artifacts; do not inspect N17 or another
  historical trust domain.
- Never open COM4.  Operate COM3 and J-Link directly when required.
- Normal firmware downloads must use the non-halting trust preflight and
  apps-only write set.  Do not write BL1/BL2, manifests, secondary slot,
  OTP/eFuse, lifecycle, calibration or data regions without explicit authority.
- Keep board wiring and PA polarity in the selected-board binding; keep DAC,
  DMA and NuttX audio mechanics in the shared chip wrapper.
- GPT-5.6-Luna delegation remains disabled.
