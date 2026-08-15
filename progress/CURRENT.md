# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Snapshot

- Canonical upstream baseline is `origin/dev-ai-contest-2026` at
  `eecfc7dda46d2f2eefb2af59c67cc96028eb41d9`.
- The board/chip/partition/binding refactor is merged. Active work is the
  additive package phase on `agent/bk7258-bkpack`; the implementation is
  committed and published for Web PR creation. No firmware was flashed and
  no hardware was accessed.
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
- The focused package suite passed 17 tests. Python syntax, shell syntax and
  `git diff --check` passed. A real previously built signed directory was
  repacked with the current schema; its 30 payload members plus manifest
  verified with SHA-256
  `8fe38a44ba1b63e9bd3805214eb9331e19c2a4e4182784f98dc131c4275fc0a1`.

## Remaining debt

- Validation implementation still lives beside some chip drivers. Removing
  boot-time validation ownership remains the separate validation-runner
  cutover; no legacy profile is deleted meanwhile.
- `pack-prepare`/`pack-verify` remain metadata-only planning tools. Signed
  dual builds now additionally emit a deterministic, verified
  `firmware.bkpack`; it is a Beken ZIP-compatible delivery archive, not an
  official openvela format or a Flash loader.
- A fresh complete signed build and an actual Windows manual download of this
  exact implementation have not run. No Linux/macOS/native Windows loader is
  claimed or planned in this phase.

## Exact next action

Open and merge the Web PR, then build one fresh signed pair and hand its
`firmware.bkpack` to the owner for a Windows extraction/manual-loader check.
The generated `WINDOWS_FLASH.txt` is the only download handoff; do not add a
generic cross-platform Flash framework.

## Boundaries

- Preserve and exclude the two BL2 temporary files and both unrelated log
  directories; do not stage, delete or infer evidence from them.
- Do not inspect N17 or another historical trust domain, add SDK bytes, or
  mutate a private SDK mirror.
- Do not delete or modify the 27 frozen profiles until P9b is explicitly
  authorized and profile-specific parity evidence exists.
- Any later AI Coding log export must be selected and scrubbed before it is
  added under the existing `logs/` tree.
