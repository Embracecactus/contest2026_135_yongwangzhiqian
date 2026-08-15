# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Snapshot

- Canonical upstream baseline is `origin/dev-ai-contest-2026` at
  `06997c665fd57b8337f94f068f428583a67d7d2b`.
- Active work is the uncommitted structural refactor on
  `agent/bk7258-board-contract`; no firmware was flashed and no hardware was
  accessed.
- P0-P9a remain merged. No P9b frozen-profile cutover is authorized.

## Current structural result

- `board/bk7258` remains the one logical OpenVela board. CP/AP linker scripts
  remain at `board/bk7258/scripts`; no physical variant duplicates them.
- Classic Make, CMake and composition/config IR now require exactly one
  matching physical selector among T5AI-Core, T5-Board and AIDK AI Toy.
- Physical audio, LCD, touch, camera and TF sources are selected by the
  existing board `src/` build, not by `chip/`.
- GPIO, MIC, SDIO, Audio, DVP sensor policy and board lifecycle now cross a
  versioned typed binding boundary. Generic chip sources no longer include
  `arch/board/board.h`, read physical-board macros or embed GC2145 facts.
- Board capabilities fail closed for Audio, MIC, user GPIO, SWD and native USB;
  AIDK cannot silently enable an unclaimed binding.
- SoC XIP/CRC geometry is isolated in `chip/include/bk7258_memorymap.h`.
  Generated product partitions and image aliases are owned by
  `board/bk7258/include`; the obsolete chip partition-layout header is gone.
- The 27 frozen legacy profiles and their migration ledger were not changed.

## Verification

- Eighteen framework/AIDK unit tests passed after adding forged dual/zero/
  mismatched board-selector negatives for IR and resolved config documents.
- Partition generation/check, partition verifier, SDK partition host wrapper,
  RPTUN header contract and `git diff --check` passed.
- Static gates found no physical-board source/path selection in chip
  Make/CMake/Kconfig and no product partition definitions under `chip/`.
- Full Classic/CMake firmware builds and board runtime regression were not run;
  the present conclusion is structural/host `CODE_PASS`, not hardware PASS.

## Remaining debt

- Validation implementation still lives beside some chip drivers. Removing
  boot-time validation ownership remains the separate validation-runner
  cutover; no legacy profile is deleted meanwhile.
- The standard artifact names and package/range contract are modeled, while
  `pack-prepare`/`pack-verify` remain metadata-only. The existing signed dual
  packer still emits a package directory; it does not yet emit one portable,
  self-verifying `firmware.bkpack` or a cross-platform flash CLI.

## Exact next action

Review and commit this bounded board/chip/partition/binding phase. The next
implementation phase is the additive package adapter: preserve normal NuttX /
OpenVela outputs, materialize deterministic CP/AP artifact names, and wrap
BL1/BL2/CP/AP plus the signed manifest into one verified `.bkpack` without
changing frozen profiles.

## Boundaries

- Preserve and exclude the two BL2 temporary files and both unrelated log
  directories; do not stage, delete or infer evidence from them.
- Do not inspect N17 or another historical trust domain, add SDK bytes, or
  mutate a private SDK mirror.
- Do not delete or modify the 27 frozen profiles until P9b is explicitly
  authorized and profile-specific parity evidence exists.
- Any later AI Coding log export must be selected and scrubbed before it is
  added under the existing `logs/` tree.
