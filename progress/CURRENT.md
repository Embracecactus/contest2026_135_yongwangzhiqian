# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Current task

Publish the bounded BK7258 DAC hardware-EQ register-lifecycle stage from
`feat/bk7258-dac-eq`, based on merged upstream commit `a512602d`, for owner
review and merge through the GitHub Web UI.

## Verified baseline

- Upstream `dev-ai-contest-2026` contains the physically verified T5-Board
  Audio DAC/fixed-32 kHz stage as commit `a512602d`.
- The private DAC hardware-EQ extension is physically verified with signed
  MCUboot pair `18.6.78`, security counter `0x1206004e`:
  - [DAC hardware-EQ register lifecycle](verification/2026-08-15-bk7258-audio-dac-eq.md)
  - [Audio DAC and fixed-32 kHz baseline](verification/2026-08-15-bk7258-audio-dac.md)
- A separate `feat/bk7258-saradc-adc-key` worktree retains the generic
  SARADC/ADC-key implementation and an 18.6.77 released-baseline PARTIAL.
  Its physical SW5 released/pressed/released transition remains pending and
  is not part of this branch.

## Completed in this stage

- `CONFIG_BK7258_AUD_DAC_EQ` adds one chip-generic, AP-only DAC extension.  It
  uses the already pinned v3.1.1.9 `bk_aud_dac_eq_config/deconfig` symbols;
  no SDK rebuild, software `libeq.a`, or board preset is introduced.
- A versioned 88-byte private ioctl deep-copies four banks of five signed-22
  raw coefficients.  It is accepted only before stream hardware exists;
  running changes and malformed payloads fail closed.  The standard NuttX
  `AUDIO_FU_EQUALIZER` capability remains unadvertised.
- EQ apply occurs after DAC initialization while muted and before DMA/DAC/PA
  start.  Deconfiguration and field readback occur after stream quiescence
  and before DAC deinitialization.  Cleanup failure retains ownership for an
  explicit retry instead of reporting a false release.
- The retained T5-Board Audio profile was upgraded in place to compatibility
  `t5_board_audio_dac_validation_mcuboot_v2`; the build gate pins the complete
  pair, required ELF symbols, final map owners, and absence of legacy/software
  EQ implementations.
- The Audio v6 diagnostic passed twice without mutation: 63 enqueue/dequeue,
  two completes, symmetric two-cycle DMA/DAC/PA/DVFS ownership, zero underrun,
  zero residual resources, and zero tick/CLOCK_MONOTONIC regressions.
- EQ reject/config/apply/deconfig/readback totals were `16/4/2/2/4`, with zero
  readback failures and final shadow/request/applied/resource state cleared.
  CP one-shot standby also remained symmetric and AP/CPU2/RPTUN heartbeats
  continued after restore.

## Exact next action

Push `feat/bk7258-dac-eq` to the owner fork and open a Web PR against
`dev-ai-contest-2026`.  After that branch is under review, resume the pending
SW5 physical SARADC transition or start the next chip-first JPEG
decoder/DMA2D stage without mixing either change into this PR.

## Remaining boundaries

- The hardware run deliberately applies enabled all-zero raw banks.  It
  proves enable/readback/deconfigure lifecycle, not nonzero bank packing,
  Q format, transfer function, pass-through response, stability or audio
  quality.  Post-release powered-down bank reads are not coefficient proof.
- Immediate rollback and retained ownership after SDK/readback failure are
  statically closed but were not fault-injected on hardware.
- The ioctl is a BK7258 private control ABI and excludes multi-session mode.
  No generic NuttX equalizer payload exists here, so applications must not
  infer `AUDIO_FU_EQUALIZER` support.
- The underlying Audio result remains bounded to mono S16/16 kHz/320-sample
  frames/eight explicit APBs and an in-memory tone.  P6 has no fitted speaker,
  so no acoustic or frequency-response claim is made.

## Fixed constraints

- Do not modify official NuttX/apps or Beken SDK source trees.
- Preserve validated MIC, TF, Audio DAC and unrelated profile behavior.
- Preserve unrelated untracked artifacts; do not inspect N17 or another
  historical trust domain.
- Never open COM4.  Operate COM3 and J-Link directly when required.
- Normal downloads require the non-halting public-trust preflight and
  apps-only write set.  Do not write BL1/BL2, manifests, secondary slot,
  OTP/eFuse, lifecycle, calibration or data regions without explicit authority.
- Keep chip mechanics and private ABI in `chip/`; selected boards may own only
  physical wiring and deliberately reviewed presets.
- GPT-5.6-Luna delegation remains disabled.
