# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Current task

Publish the chip-first BK7258 SARADC stage from branch
`feat/bk7258-saradc-adc-key`, rebased onto merged DAC-EQ commit `4248440`, for
owner review through the GitHub Web UI.  The generic controller/IPC path is
physically verified; the T5-Board SW5 released -> pressed -> released run
remains an explicitly documented follow-up rather than a driver PASS claim.

## Verified baseline

- Upstream `dev-ai-contest-2026` contains the verified DAC hardware-EQ stage
  as commit `4248440`:
  [DAC hardware-EQ register lifecycle](verification/2026-08-15-bk7258-audio-dac-eq.md)
- The SARADC `18.6.77+0` pair was built and exercised on pre-EQ base
  `a512602`; its profile does not enable Audio/EQ.  It completed
  manifest/provenance, profile, partition, ELF/map owner and pair gates.
- The rebased tree completed a clean signed `18.6.79` paired host build with
  the merged EQ and SARADC profile/map gates; it was not downloaded.
- The apps-only COM3 flow passed the non-halting trust preflight and wrote only
  CP `[0x011000,0x042000)` and AP `[0x165000,0x185000)`.
- [SARADC and T5-Board ADC-key baseline verification](verification/2026-08-15-bk7258-saradc-adc-key.md)

## Completed in this stage

- The AP chip layer publishes configurable `/dev/adcN`; it has no T5/P12/key
  assumption.  Each synchronous trigger uses the supported v3.1.1.9
  `bk_adc_read` IPC path and performs symmetric controller ownership/cleanup.
- The CP server owns boot-lifetime GPIO/ADC runtime initialization and uses the
  NuttX SDK IRQ bridge.  Build gates prove the final symbol owners rather than
  accepting same-named vendor archive implementations.
- A generic validator samples exclusively through the public NuttX ADC ABI.
  The selected T5-Board layer alone binds P12=ADC14 and active-low SW5 policy.
- Dedicated CP/AP validation profiles and bidirectional compatibility gates
  reject a missing server and all known P12 conflicts.
- The released-baseline board run reached `WAIT_ACTIVE`: 1,565 ADC14 samples,
  baseline 8,121..8,190 with median 8,189, no transport/FIFO/channel/range
  errors and full upper/lower-half delivery.
- Because SW5 remained released, the run correctly ended `-ETIMEDOUT` with no
  transition.  Final driver state was RESET, resources zero; all per-trigger
  acquire/release, init/deinit, start/stop and delivery counters were 1,565
  each.  This is `PARTIAL`, not an ADC-key physical PASS.

## Exact next action

Review and merge the published branch through a Web PR against
`dev-ai-contest-2026`, retaining the `PARTIAL` endpoint boundary.  A complete
endpoint result still requires a fresh SARADC image, COM3 capture and one real
released -> pressed -> released SW5 run with two transitions, zero errors and
zero driver resources.  Further block-diagram driver work is paused by the
owner and must not be mixed into this publication.

## Remaining boundaries

- Current hardware evidence proves the controller, IPC, ADC14 released
  baseline and cleanup, not the physical SW5 low transition or return high.
- The stage covers one external channel and synchronous single-trigger reads;
  it does not cover absolute voltage accuracy, other channels, continuous
  sampling, battery measurement, temperature/PM coexistence or long soak.
- The SARADC profile does not exercise Audio or DAC-EQ; their hardware result
  remains a separately verified upstream baseline.
- P12 is a single physical owner: ADC14 conflicts with UART0 flow control,
  GPIO consumers, TOUCH0 and USB0_DP.  UART0 without flow control uses P10/P11.

## Fixed constraints

- Do not modify official NuttX/apps or Beken SDK source trees.
- Preserve validated Audio, DAC-EQ, MIC, TF and unrelated profile behavior.
- Preserve unrelated untracked artifacts; do not inspect N17 or another
  historical trust domain.
- Never open COM4.  Use COM3 and J-Link only where the selected profile allows.
- Normal downloads require the non-halting trust preflight and apps-only write
  set.  Do not write BL1/BL2, manifests, slot B, OTP/eFuse, lifecycle,
  calibration or data regions without explicit authority.
- Keep controller mechanics in `chip/`; keep pins, polarity, attached devices
  and physical validation policy in the selected-board binding.
- GPT-5.6-Luna delegation remains disabled.
