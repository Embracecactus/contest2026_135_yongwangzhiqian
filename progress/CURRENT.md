# Current Progress

Last updated: 2026-08-16
Updated by: Codex

## Snapshot

- Canonical upstream baseline is `origin/dev-ai-contest-2026` at
  `dee20b901ad852e5115fd76d9346bdf00d0c9e0e`.
- The working tree contains the reviewed Classic selector lifecycle fix and
  the AIDK AI Toy product materialization/build integration. These changes are
  not yet committed or pushed.
- P0-P9a remain merged. No P9b frozen-profile cutover is authorized, and the
  27 frozen legacy profiles remain unchanged.
- No firmware was flashed and no hardware was accessed in this phase.

## Current structural result

- `board/bk7258` remains the single logical OpenVela board. T5AI-Core,
  T5-Board and AIDK AI Toy remain physical variants selected exactly once.
- AIDK now uses the shared BK7258 BL1/BL2 plus MCUboot A/B product contract;
  the reverse-engineered factory FAL/raw layout is reference evidence only and
  is not inherited by the project product.
- AIDK CP/AP profiles are materialized from reviewed compatibility seeds into
  a temporary logical-board tree. No persistent legacy config directory was
  added, and archived build metadata uses stable product identifiers rather
  than deleted temporary paths.
- The generated CP image uses UART0 at 115200 8N1 without flow control. SWD
  and boot hold are disabled. The minimal AP image links the AIDK binding and
  does not enable Audio, MIC, LCD, DVP, SDIO or TF bindings.
- Public role artifacts are `vela_cp.bin` and `vela_ap.bin`. The Beken
  `app*.bin`, CRC-expanded and Flash-padded images remain internal packaging
  members. Users receive `firmware.bkpack` and must follow its generated Flash
  plan rather than selecting an internal image manually.

## Verification

- The focused AIDK/framework suite passed 20 tests after the final metadata
  fix. Four focused bkpack container tests also passed, including the gate
  that public `vela_*` aliases are disjoint from every Flash plan. Shell
  syntax passed, and five transport tests passed including fail-closed Windows
  loader marker handling. The final publication gate therefore passed 29
  tests; `git diff --check` also passed.
- A complete signed AIDK CP/AP build using the existing BL1/BL2, partition,
  MCUboot pair, factory-layout and bkpack pipeline completed as version
  `18.6.81`.
- `firmware.bkpack` verification passed with 30 members and deterministic
  payload hashes. Its SHA-256 is
  `aaaf1d34fec14418dd068cc6ad304e1088b6d58532cb7b46794e0559970baa03`.
- The package records `vela_cp.bin -> cp-raw.bin` and
  `vela_ap.bin -> ap-raw.bin` as byte-exact standard aliases. Its generated
  Windows guide rejects direct flashing of those logical images and selects
  only Flash-padded payloads for download.
- `build-profile.txt` and archived CP/AP configs contain stable logical product
  identifiers and no `/tmp/bk7258-aidk-profiles...` references.
- Detailed evidence is in
  [AIDK MCUboot framework verification](verification/2026-08-16-bk7258-aidk-mcuboot-framework.md).

## Remaining debt

- The working changes need a final scope review before publication.
- The package is intentionally `authenticated=false`,
  `hardware_verified=false` and `target_preflight_required=true`. AIDK has no
  accepted initial-provisioning/preflash trust flow yet, so this artifact is
  build-verified but not Flash-authorized.
- Legacy root copies named `app.bin`, `app1.bin`, `app_crc.bin`,
  `app1_crc.bin` and `all-app.bin` remain for active Beken build-script
  compatibility. Removing them belongs to the later P9b legacy cutover; they
  are not public OpenVela artifacts.
- AIDK GPIO, PWM, SC7A20, Audio/MIC and SD NAND runtime validation has not
  started. Driver work must not be confused with the boot/package baseline.

## Exact next action

Complete an independent scope review of the current framework diff, excluding
generated BL2 files and unrelated logs. After publication, define an explicit
AIDK initial-provisioning trust flow before any COM9 write; then add the first
standard NuttX AIDK binding for P40 LED1 and P12 KEY1.

## Boundaries

- Preserve and exclude the two BL2 temporary files and both unrelated log
  directories; do not stage, delete or infer evidence from them.
- Do not inspect N17 or another historical trust domain, add SDK bytes, or
  mutate a private SDK mirror.
- Do not delete or modify the 27 frozen profiles until P9b is explicitly
  authorized and profile-specific parity evidence exists.
- Do not flash AIDK, bypass target preflight, or treat the factory demo image
  as the project's boot or partition contract.
