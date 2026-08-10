# Current Progress

Last updated: 2026-08-10 GMT+8
Updated by: Codex

## Active objective

The active BK7258 boot chain is:

```text
legacy BootROM -> board-owned BL1 -> signed Manifest
-> pinned NuttX MCUboot BL2 -> signed same-slot CP/AP pair -> NuttShell
```

The old N15/N17 self-developed OTA lifecycle has been retired from active
code. Its selector, journal, staging, publication, trial/rollback, fault
injection, release-key and validation-script implementations are removed.
Historical ADRs and verification records remain evidence of work that was
previously performed; they are not descriptions of the current firmware.

## Current boot and partition baseline

- Runtime SDK: official BK7258 v3.1.1.9 only, linked through board wrappers.
- NuttX and official SDK source trees remain unchanged.
- BL1 is still a complete source implementation: clock/reset/watchdog setup,
  ECDSA-P256/SHA-256 Manifest authorization, primary-then-secondary BL2
  fallback, vector/copy checks, SRAM policy publication and handoff.
- BL2 is still the pinned NuttX MCUboot implementation. It verifies MCUboot
  image/TLV metadata and accepts only a version/counter-compatible CP/AP pair
  from the same physical slot.
- BL1 has one production object closure. It no longer links any N15/N17
  lifecycle selector, Flash writer, software journal or release key.
- The active CSV keeps contiguous CP/AP A and B pairs, two read-only BL1
  Manifest sectors, two read-only BL2 copies, LittleFS and the official
  calibration tail. It has no OTA metadata bank or authorization-policy
  sector.
- Flash MTD exposes ordinary data partitions plus read-only MCUboot/Manifest
  regions; there is currently no firmware update writer or installer.

## Verification at this checkpoint

On 2026-08-10, a clean 32-job `cp_nsh_mcuboot + ap_smp_mcuboot` build passed
with ephemeral, non-repository signing keys and SDK v3.1.1.9 checksums:

- MCUboot version `18.1.4`, image security counter `21`.
- Manifest-enforced BL1 and NuttX MCUboot BL2 both compiled and linked.
- CP/AP images were signed, 32+2 CRC encoded and assembled into the final
  factory package.
- Generated partition validation, SDK partition-wrapper host validation and
  final factory-layout validation passed.
- Factory prefix ends at `0x4fb000`; LittleFS remains
  `0x600000..0x700000`; the official tail starts at `0x7fa000`.

Artifact SHA-256:

- `bl_crc.bin`: `2c3f02cc91002fbcef97d00d6edd88cdde50fd732d2799a2eb07e15321d4a374`
- `bl2_crc.bin`: `1ce5a10153e51452eb7871f7e57c009522d80c2a1850f2ab5b69ae5c2a1af79e`
- `all-app-factory.bin`: `b483434eb51194f22d7ccc1859a53d6fc5b7561acd413739c0013531f411530b`

This checkpoint is build/host evidence. The previously recorded board proof
for Manifest rejection, secondary-BL2 fallback and signed CP/AP boot remains
valid for the preserved chain, but the newly cleaned image has not been
flashed in this checkpoint. Canonical board evidence:
[Secure Boot remaining gates](verification/2026-08-08-bk7258-secureboot-remaining-gates.md).

## Other verified platform state

- CP NuttX, AP SMP, RPTUN/RPMsg/RPMsgFS, Bluetooth, Wi-Fi STA, PSRAM and the
  established peripheral wrappers remain outside this cleanup and are not
  intentionally changed.
- T5AI-Core is the default physical board. T5-Board wiring is separated under
  `boards/t5_board`; its ILI9488 RGB LCD displayed the expected color bars.
- Driver backlog conclusions and board evidence are recorded in
  [AP peripheral board evidence](verification/2026-08-09-bk7258-ap-peripheral-board-evidence.md).

## Honest boundary

The chain is software-rooted. It does not prove that BK7258 BootROM consumes
the repository Manifest and does not provide OTP/eFuse-backed root trust or
persistent hardware anti-rollback. No OTP/eFuse, secure-lifecycle or debug
lock bit has been written.

The current source provides authenticated boot, not a complete field-update
lifecycle. Restoring the deleted custom N15/N17 OTA state machines would be a
regression. A future updater must be designed against NuttX MCUboot semantics
and the frozen CP/AP same-slot contract.

## Next step

1. Review and commit this cleanup separately from unrelated driver/bundle
   changes already present in the worktree.
2. Flash and smoke-test the cleaned MCUboot image at the next safe hardware
   checkpoint: Primary BL2, Secondary fallback negative case and CP/AP boot.
3. Only when field update is requested, design its transport, inactive-slot
   writer, confirmation and rollback flow directly around MCUboot; do not
   restore the retired N15/N17 custom journal.
4. Keep hardware Secure Boot provisioning deferred until separate authority;
   never write OTP/eFuse as part of ordinary validation.

## Fixed constraints

- Do not modify NuttX or official SDK source except temporary debugging that
  is fully restored.
- Do not mix BK7259, v4.x or BK7236 runtime artifacts into the product path.
- Private signing keys must never enter the repository, logs or memory.
- Hardware mutation, commit, push and PR actions keep their normal authority
  boundaries.
