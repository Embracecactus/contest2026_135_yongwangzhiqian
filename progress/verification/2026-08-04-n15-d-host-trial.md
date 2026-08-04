# N15-D one-trial host/source/ELF verification

- Verified: 2026-08-04T03:31:30+08:00
- Branch: `feat/bk7258-n15-ota`
- Source HEAD: `a8900189c2e63a49a1f7dbf3334f5358db0d425f`
- Status: **host/source/ELF-verified; no board write or remap authorized**
- SDK: exact official Beken v3.1.1.9 only

## Outcome

N15-D now implements the one-trial state transition required by ADR-005 in
repository-owned code. A validated `PENDING_B` record is appended as
`TRIAL_STARTED` in 32-byte Flash-driver-compatible chunks. Every chunk is
read back immediately, then the complete record and the complete metadata
chain are revalidated before the current boot may use B. A later reset sees
the persisted `TRIAL_STARTED` state and returns to A. CP-side confirmation or
explicit rollback appends `CONFIRMED_B` or `ROLLBACK_A` respectively.

This checkpoint deliberately leaves all runtime mutation and remap gates
closed. It proves the portable model, target source closure and final ELF; it
does not claim a physical Flash write, trial boot, confirmation or rollback.

## Implementation boundary

- `boot_ota_select_core.[ch]` is the single structural parser and transition
  builder. It rejects dirty gaps, torn records, identity drift, illegal state
  transitions, stale generation and sequence overflow.
- `boot_ota_trial_core.[ch]` is portable and has no NuttX, SDK, MMIO, heap,
  libc or mutable-global dependency. Caller callbacks own gates, locking and
  raw Flash operations.
- `boot_ota_flash_program.[ch]` is a 644-byte SRAM-only writer for the exact
  metadata range. It accepts only aligned 32-byte writes and exact Flash ID
  `0xC86517`, preserves non-protection status bits, verifies every write and
  feeds the watchdog.
- `boot_ota_select.c` consumes `PENDING_B` only when trial-write and remap
  compile/runtime gates are all armed. It copies and verifies the writer in
  SRAM, appends/read-backs `TRIAL_STARTED`, remaps only for that current boot,
  and clears the boot workspace before handoff.
- `bk7258_ota_trial.c` is the CP-only wrapper for confirm/rollback under the
  shared Flash guard. Confirmation additionally requires the hardware remap
  enable bit, preventing an A fallback boot from confirming B.
- The CP dependency path remains repository-relative so NuttX `mkdeps` can
  resolve both portable cores without modifying official NuttX files.

## Portable fault matrix

Command:

```text
python3 board/bk7258_t5ai/scripts/verify_bk7258_ota_trial.py \
  --self-test \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9
```

Result:

```text
BK7258 N15-D trial verification PASS: positive=4 negative=113 reset_boundaries=48 writes_enabled=false
```

Coverage includes:

- byte-exact trial, confirm and rollback records plus deterministic rebuild;
- compile/runtime gate, lock, initial read, program and read-back failures;
- dirty gaps, corrupt CRC, identity drift, stale generation, illegal state,
  full sector and uint64 sequence overflow;
- reset after every one of 16 program chunks for all three transitions;
- torn writes, short writes/reads, mismatches and final full-read failures;
- post-fault selector decisions proving no unconfirmed B persists across a
  reset and no mixed generation becomes selectable.

The harness and portable cores compile with `-Wall -Wextra -Werror`; GCC
`-fanalyzer` also passes. Exact v3.1.1.9 Flash driver/LL, OTA source and AB
boot binary hashes are pinned by the verifier.

## Final Boot/CP ELF closure

The exact full build report is
`nuttx/bk7258-dual/bk7258-ota-trial.json` in the generated workspace.

- Boot ELF SHA-256:
  `1ca41e2b1fefb6f013bccf92dd84c54675f233c3ad774c57ab176a9ef55d6a6e`;
- Boot logical image: 12,176 bytes, SHA-256
  `d6acda720f9b337dbc2c5f0e3282042b460c46911f7bcb7ae4f25dcdddd1175f`;
- Boot CRC image: 69,632 bytes, SHA-256
  `5c82d084ca2828e5cdb564efa6f4b22f251e39273a10a77b55ff4ce33e132826`;
- SRAM writer VMA/LMA: `0x2800c000` / `0x02002d0c`, size `0x284`;
- 12 verified literal loads, zero XIP literal targets, zero external
  relocation/call escape from the SRAM closure;
- all six selection/remap/trial compile/runtime gate words are zero;
- final CP ELF SHA-256:
  `6b5601ccea132c9750f6d8579a04506ffc00b8d2cbb8ae02ff77c543b0042615`;
- required transition, wrapper and Flash-permission symbols are present;
  `CONFIG_BK7258_OTA_TRIAL_WRITE` and its runtime setter are absent.

The ELF verifier was corrected to decode real `$t`/`$d` literal-pool regions
and every PC-relative `ldr`; it no longer mistakes aligned Thumb instruction
bytes for XIP pointers.

## Complete exact-SDK build

The third clean dual-image run used:

```text
BK7258_SDK_SOURCE=/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9
CP_CONFIG_NAME=cp_nsh_psram AP_CONFIG_NAME=ap_smp_psram
N15_OTA_GENERATION=18 N15_OTA_VERSION=n15-d-host
N15_OTA_BASE_VERSION=n15-c-host N15_OTA_TIMESTAMP=0 JOBS=8
board/bk7258_t5ai/scripts/build_dual_image.sh
```

It passed exact SDK checksums, layout/factory verification, N15-A/B/C/D,
Boot `-Werror`, CP/AP builds, RPTUN layout, BLE-GATT, PSRAM and final packing.
The PSRAM baseline verifier now explicitly permits the read-only
`CONFIG_BK7258_OTA_TRIAL=y` stage switch while continuing to compare every
PSRAM parameter unchanged.

Official `nuttx/` and `apps/` have zero tracked diff after the build. Their
remaining status entries are untracked generated/pre-existing directories;
the SDK source tree is not a Git checkout, so the exact manifest and source
hash gates are the authoritative immutability evidence.

## Remaining N15 work

- Freeze and test metadata-sector reclamation and pending publication under
  the CP Flash guard.
- Freeze the health-confirmation policy and connect it to a bounded,
  observable validation profile.
- Run authorized hardware trial/confirm/revert, reset/corruption/power-loss
  injection, timing/wear measurements and the full retained regression.
- Continue to label CRC32/FNV/SHA-256 as integrity only. Signatures, key
  provisioning and anti-rollback remain outside the first N15 boundary.
