# ADR-008: Design hardware-root-compatible OTA authentication but defer OTP activation

- Status: Accepted for N17 architecture; implementation and hardware provisioning not started
- Date: 2026-08-06
- Decision owner: Project owner
- Approval evidence: owner accepted the recommended phased trust model and
  explicitly required the board to remain recoverable while later drivers are
  adapted

## Context and drivers

N15 provides power-loss-aware inactive-slot staging, paired CP/AP validation,
one-trial boot, confirmation and rollback. Its CRC32, RBL CRC/FNV and SHA-256
checks detect damage but do not authenticate a publisher. Metadata format 2
also fills every byte of its 512-byte journal record and embedded 384-byte
candidate descriptor. Adding a signature to either structure would repeat an
immutable release identity in every mutable lifecycle record.

The exact official Beken SDK v3.1.1.9 source exposes ECDSA, OTP/eFuse and
secure-boot tooling. Its BK7258 OTP map names BL1/BL2 public-key hashes and
security counters. However, the delivered board configuration has
`secureboot_en=FALSE`, its BL1 control template has `security_boot_ena=0`, and
the project has not proved the BootROM manifest geometry, the current board's
OTP contents, counter encoding, provisioning sequence or recovery procedure.
OTP/eFuse writes, lifecycle changes and debug locks are irreversible.

The project still needs additional peripheral and product-driver adaptation.
Permanently enabling an unverified secure-boot chain could make the only
development board unrecoverable and stop that work.

## Considered options

1. Enable official secure boot and program OTP/eFuse before implementing the
   N17 update format.
2. Treat a public key compiled into the team bootloader as the final product
   trust anchor.
3. Freeze one hardware-root-compatible signed-release format now, initially
   verify it from the team bootloader without OTP mutation, and activate the
   BootROM/OTP root only under a later independent provisioning gate.

## Decision

Adopt option 3.

### N17-S: recoverable software-root phase

- Use ECDSA P-256 over SHA-256 with a fixed canonical binary encoding. The
  wire format uses a fixed-width 64-byte `r || s` signature rather than DER.
- Separate an immutable signed release manifest from the mutable N15
  lifecycle journal. A release manifest authenticates the CP/AP pair as one
  product release; the journal only records local staging and boot state.
- The signed fields bind a format/domain identifier, product/board/chip and
  partition-layout identity, signature algorithm and key ID, security
  counter, release version, CP/AP lengths and their SHA-256 digests. The exact
  format must reserve forward-compatible fields and reject non-canonical
  values.
- Target slot, metadata bank, sequence, generation, timestamp,
  `PENDING/TRIAL/CONFIRMED/ROLLBACK` state and base-version bookkeeping are
  device-local mutable data and are not signed.
- Each executable slot must have an authenticated manifest. The format-3
  lifecycle journal references manifests by digest rather than embedding the
  signed object. ADR-009 freezes one 4 KiB sector per slot at `0x50b000` and
  `0x50c000`, followed by a one-way policy sector at `0x50d000`.
- Once the N17 authentication policy is armed, erased/corrupt metadata may
  recover only to an image whose manifest and actual CP/AP payload both
  authenticate. The existing header-only slot-A recovery is forbidden.
- The first phase may pin the verification public key in the repository-owned
  bootloader. It may claim publisher authentication against the normal OTA
  write path and software downgrade prevention, but it must not claim
  resistance to an attacker who can replace the complete Flash, bootloader
  and embedded key.
- Keep release version and security counter independent. A candidate counter
  below the accepted floor is rejected. ADR-010 freezes confirmation and
  floor advancement as one format-3 journal commit so a reset cannot make
  both old and new signed pairs unbootable.
- Private signing keys never enter the repository, firmware, logs or project
  memory. `key_id` and format space are reserved for rotation, but no recovery
  key may bypass the anti-rollback floor.

### N17-H: separately authorized hardware-root phase

N17-H may bind the same release format to BootROM secure boot, an OTP-backed
root-key hash and a trusted monotonic security counter only after all of these
are complete:

1. Exact v3.1.1.9 source and official-support evidence freezes the BK7258
   BootROM/BL1 manifest, OTP item allocation, counter representation,
   permission/lifecycle effects and J-Link/UART recovery consequences.
2. A read-only inventory establishes the board's relevant OTP/eFuse state
   without exposing device-unique secrets.
3. The team bootloader and factory layout are proved compatible with the
   hardware chain before any irreversible setting changes.
4. Provisioning inputs, offline key custody, interruption handling and a
   recovery image are independently reviewed.
5. The project owner grants a new, explicit, range/field-specific permission
   for each irreversible board operation.

Until that gate is accepted, no build, test, script or operator workflow may
write OTP/eFuse, enable secure boot, change lifecycle state, disable JTAG or
describe the product as hardware-rooted or hardware anti-rollback protected.

## Consequences and risks

- Positive: the OTA package and signed manifest need not be redesigned when a
  hardware root is later activated.
- Positive: the development board remains recoverable for the remaining
  driver adaptation.
- Positive: immutable release authorization no longer gets conflated with
  N15's mutable, power-loss-aware boot journal.
- Negative: N17-S alone cannot resist complete-Flash replacement or a
  replacement bootloader and must carry a narrower security claim.
- Negative: format 2 becomes historical evidence after the secure policy is
  armed; migration must install and verify signed manifests for every allowed
  recovery slot before rejecting format 2.
- Risk: the immutable v3.1.1.9 mbedTLS archive contains P-256 verification but
  is not a proven freestanding Tier-1 dependency. N17 must compare a bounded
  wrapper/link closure with a small reviewed verifier without modifying SDK
  or NuttX source.

## Validation

N17 architecture review must complete, in order:

1. Freeze the threat model and permitted security claims for N17-S and N17-H.
2. Freeze the canonical manifest bytes, signature coverage, key encoding and
   independent host test vectors.
3. Prove exact A/B manifest placement and format-2-to-format-3 migration with
   no collision with vendor user configuration, LittleFS or calibration.
4. Model every boot decision, including corrupt signature, altered CP/AP,
   stale counter, damaged journal, trial failure and signed recovery.
5. Prove the Tier-1 verifier's Flash/RAM/stack and dependency closure before
   implementation is enabled.
6. Keep all OTP/eFuse and board-mutation gates closed. Hardware provisioning
   is not an N17-S acceptance test.

Validation item 2's ABI portion is complete: the owner accepted the frozen
[512-byte N17-S manifest ABI](../../docs/bk7258-t5ai/nuttx-port/n17-signed-manifest-abi.md)
on 2026-08-06, and its public-only host vector passes independent OpenSSL
verification plus 3,600 fail-closed negative checks. The future Tier-1 C
parser must still demonstrate conformance to that same vector.

Validation item 3's architecture and host-layout portions are also complete.
The owner accepted [ADR-009](ADR-009-n17-dedicated-manifest-policy-sectors.md),
[ADR-010](ADR-010-n17-format3-lifecycle-journal.md) and
[ADR-011](ADR-011-n17-fail-closed-format2-migration.md) together on
2026-08-06. The canonical partition source/generated consumers and public
Manifest vector now carry the accepted layout identity. The portable
format-3 model also passes canonical parsing, exhaustive bit/torn-write,
dual-bank, counter and migration reset checks. Bootloader implementation and
all board operations remain gated.

## Reversal signals

- Official BK7258 evidence shows its secure-boot root cannot authenticate the
  team Tier-1 chain without an incompatible permanent layout change.
- The selected verifier cannot fit the boot envelope or be isolated from
  heap/RTOS/SDK initialization dependencies.
- A signed slot-neutral release identity cannot be reconciled with the paired
  CP/AP remap and recovery invariants.

## Open issues

- Bootloader verification implementation, dependency closure and public-key
  representation.
- Tier-1 C format-3 conformance and final source/ELF resource checks.
- Official OTP security-counter encoding, endurance and permission model.
- Root/release key rotation and recovery-key authorization after the first
  single-key release.
