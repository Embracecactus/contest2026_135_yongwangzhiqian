# Current Progress

Last updated: 2026-08-21
Updated by: Codex

## Objective

Standard MCUboot multi-image OTA for BK7258 is adapted and physically verified
on T5Board, with CP/AP treated as one launchable and recoverable pair.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `fix/bk7258-vela-claw-poweron-flicker`
- HEAD: `89384390ea87757665633003a4affd20fdfc5009`
- OTA implementation and evidence are uncommitted dirty-tree changes.
- Owner-untracked logs, `bootloader.tmp`, doc-stress helpers and
  `build_package.sh` remain untouched.

## Accepted architecture

- [ADR-031](../memory/decisions/ADR-031-bk7258-standard-mcuboot-paired-direct-xip-ota.md)
  accepts standard per-image MCUboot trailers plus a board-owned same-slot
  pair gate; no private selector or journal is restored.
- BL1 owns Manifest/BL2 A/B only. BL2 orders complete CP/AP pairs, exposes one
  pair per `boot_go()`, and uses upstream direct-XIP revert.
- CP derives active/inactive from retained remap registers. It alone stages
  the inactive physical pair and confirms the active pair's two trailers.

## Completed checkpoint

- BL2 has bounded 32+2 CRC trailer RMW, exact `copy_done` authority, readback,
  Flash protection restore and WDT fail-reset. Boot mutation accepts the
  T5Board-proven C86517 command set.
- Runtime OTA uses task/range Flash guards, AP-first staging, CP sector-zero
  final commit, and AP-then-CP health confirmation.
- `bkota status|stage <cp> <ap>|confirm` is built into CP NSH.
- Full releases use imgtool `--confirm`; `package create --ota-apps` uses
  `--pad`, `pending-v1` and dynamic `target=inactive`. Legacy full signed
  packages remain verifiable.
- Clean real ARM CP/AP/BL2/BL1 build passed with rollback floor 3; BL2 copy
  size is 13,536 bytes.
- The owner authorized a full development-root rotation. New public roots and
  all package hashes are recorded in the linked verification record; private
  material remains outside Git and project memory.
- T5Board completed A confirmed → B pending → B runtime confirm → A pending →
  unconfirmed A revert to B. Final state is confirmed B v1.0.1+4 active with
  confirmed A v1.0.0+3 fallback.
- `usr_config` and calibration tail are byte-identical before/after. Two
  115200 root-chain readbacks are byte-identical and match package prefixes.
- Detailed evidence: [standard paired OTA hardware verification](verification/2026-08-21-bk7258-t5-board-paired-ota-adaptation.md).

## Residual scope

- `bk7258_ota_stage_pair()` is linked but the file-backed `bkota stage` command
  was not run: CP currently exposes no block/file source. ROM-loader inactive
  writes supplied the signed trial pairs; runtime confirm and boot revert were
  physically exercised.
- `bkota` is intentionally a trusted manual seam: it neither parses `.bkpack`
  nor derives service health. A product field agent must verify package/layout
  policy before staging and enforce CP/AP health before calling confirm.
- Host/unit tests are intentionally deferred to another model by owner order.

## Exact next action

Hand the dirty implementation and hardware checkpoint to the owner's selected
test/review model. That model should add or adapt host coverage without
repeating the root rotation or destructive hardware steps. After review, run
the existing clean build/package gates and prepare the normal commit handoff.

## Current prohibitions

- This model must not add, modify or run tests.
- Do not repeat root rotation, chip erase, OTP/eFuse/lifecycle writes, debug
  lock, or calibration/persistent-data writes.
- Do not record private-key content or paths in Git, logs or project memory.
- Do not modify or remove the owner-untracked files listed above.
