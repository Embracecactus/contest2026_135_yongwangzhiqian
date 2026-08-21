# ADR-030: Provision AIDK with the project boot chain

- Status: Accepted
- Date: 2026-08-21
- Decision owner: Project owner

## Context

The connected AIDK AI Toy still runs its factory firmware.  The owner confirms
that the recoverable factory firmware has already been backed up and explicitly
authorizes replacing the factory software trust identity instead of matching
its public fingerprints before first provisioning.  The current session did
not independently locate that full-Flash backup; the authorization relies on
the owner's attestation and must not be reported as a newly verified backup.

The target system is the current project-owned BK7258 chain and fixed-block
storage topology defined by ADR-016, ADR-018 through ADR-021, ADR-028 and
ADR-029.

## Decision

- First AIDK provisioning writes the project BL1, Manifest A/B, project BL2
  A/B and MCUboot CP/AP A/B artifacts produced from
  `bk7258_ab_fixed_block.csv`.
- Do not require a factory-root fingerprint or factory-package trust match
  before this one-time conversion.  The retained factory backup is the
  rollback source.
- Build through the sole public `bk7258.py` command surface.  Compile explicit
  public development roots into BL1/BL2, sign the matching package through the
  trust domain, and require package/image/public-signature verification before
  Flash.
- First provisioning is a manifest-declared full-chain write, not an
  apps-only update.  Later updates must match the project roots installed by
  this provisioning or explicitly repeat a full-chain root rotation.
- Do not program OTP/eFuse, enable debug lock, perform chip erase, format the
  fixed SD NAND, or write the USB-exposed resource volume.

## Consequences

- Positive: AIDK uses the same recoverable BL1 -> BL2 -> MCUboot A/B design as
  the project instead of retaining an unowned factory trust identity.
- Positive: No factory key discovery or identity inference is needed.
- Negative: The factory boot chain is overwritten; restoration depends on the
  owner-confirmed backup and the BKFIL recovery path.
- Negative: Development private keys must remain outside Git and project
  memory.  Losing them makes a later apps-only signed update impossible and
  requires another authorized full-chain root rotation.

## Evidence and validation

- Owner authorization in the active session on 2026-08-21.
- The bounded current-workspace inventory found only the separately verified
  WAV/AVI/schematic backup; it did not locate the owner-confirmed full-Flash
  recovery artifact.  This is an evidence boundary, not a provisioning gate
  under the owner's explicit instruction.
- [AIDK MCUboot framework verification](../../progress/verification/2026-08-16-bk7258-aidk-mcuboot-framework.md)
- [AIDK board-resource baseline](../../progress/verification/2026-08-21-bk7258-aidk-board-resource-baseline.md)

## Reversal signals

- The owner revokes full-chain replacement.
- The generated full-chain package fails layout, image or public-signature
  verification.

## Open questions

- Select the development-key retention policy after the first hardware boot:
  retain outside the repository for apps-only updates, or delete and require
  explicit full-chain rotation for later builds.
