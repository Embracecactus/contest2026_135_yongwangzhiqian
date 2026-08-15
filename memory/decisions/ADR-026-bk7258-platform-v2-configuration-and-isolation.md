# ADR-026: BK7258 platform-v2 configuration freeze and isolation model

- Status: Proposed for owner review; the P0 guard decisions are implemented pending acceptance, and P1+ decisions remain open
- Date: 2026-08-15
- Supersedes: ADR-024 (the ADR-024 historical body is retained unchanged)
- Decision owner: Project owner

## Context

The current BK7258 tree has a useful, already-reduced legacy catalog, but its
remaining `configs/<profile>` directories still combine board identity, role,
boot format, SDK selection and validation intent.  Build, verifier and
documentation consumers also refer to profile names directly.  The next
platform migration must stop this surface from growing while preserving the
merged T5/SARADC/JPEG/Audio/TF/Wi-Fi behavior and its evidence boundaries.

P0 therefore freezes the exact legacy tree and records its consumers.  It does
not create product manifests, generated seeds, board variants or a new build
backend.  Those are P1 decisions to be reviewed against this record.

## Decisions

### Dependency and configuration model

The target dependency direction is:

```text
chip (SoC startup, IRQ, SMP, controllers, minimal mailbox substrate)
    <- platform (CP/AP/BL2 lifecycle, RPTUN, coordinated PM)
    <- services (approved platform/chip interfaces)
    <- generic/external drivers (approved NuttX/chip APIs)
    <- boards (platform-data/resource resolver and exactly-one wiring)
    <- products and validation/apps (public interfaces)
```

Chip is limited to SoC startup, IRQ/SMP/controller mechanics and the minimal
mailbox substrate.  It has no dependency on boards, platform composition,
services or validation.  Platform depends on chip and owns CP/AP/BL2 lifecycle,
RPTUN and coordinated PM; those responsibilities do not belong to optional
services.  Services depend only on approved platform/chip interfaces.  Generic
and external drivers contain no board assumptions and use approved NuttX/chip
APIs.  Boards compose wiring through platform data and a resource resolver,
with exactly one binding or a fail-closed result.  Validation and apps consume
public interfaces and never become chip dependencies.

The product rule is `family + mode + role`: a reusable product family may have
bringup, application, validation or factory modes, with CP/AP/BL2 roles
resolved as one resource graph.  A new driver or validator extends a
capability pack and suite.  It does not add a `configs/` directory.  A new
profile is allowed only for a genuinely different product mode, lifecycle,
boot layout or architecture boundary and requires a new reviewed ADR.

The resource graph spans CP, AP and BL2 and every lifecycle phase (bring-up,
runtime, suspend/resume, recovery and teardown).  Pair compatibility is a
graph property, not a filename convention.  Board selection is exactly one
and is fail-closed before configuration resolution.

### Build backend and seed boundaries

P1 may evaluate CMake as the canonical backend candidate because the current
tree has a CMake parity record.  CMake is not accepted as canonical by this
ADR; acceptance requires P1 proof that the same resolved inputs, SDK lock,
role isolation and artifact identity are consumed by every backend.

The Classic flow remains compatibility-only unless a complete copy-on-write
(COW) isolation proof is produced.  Production BL2 remains the current
minimal `board/bk7258/bootloader/bl2/Makefile` path for now.  P0 must not add a
fake NuttX BL2 seed.  The final number of product/configuration seeds is not
pre-fixed; it is determined by the closed migration matrix and reviewed
resource graph.

BL1, BL2, CP and AP each require isolated inputs and output directories.  No
shared `.config`, restore-after-build trick or global lock is an acceptable
substitute for isolation in the target design.

Unsigned/build identity reproducibility is a separate property from signed
artifact reproducibility.  Unsigned resolved configuration, manifests,
payloads and package ordering must be byte-reproducible.  A signed artifact
may intentionally vary when an authorized signing operation is randomized;
that does not weaken the unsigned identity contract and must be stated in its
manifest.

### SDK source, mirror and object registry

The original CP v3.1.1.9 source-tar provenance is recorded.  AP v3.1.1.9 and
the derived SDIO4 bundle have no provided source archive/hash and are
sealed-binary manifest/provenance records only; no source reproducibility
claim is fabricated.  CP/AP UART-patched objects and final `libdriver` hashes
remain distinct recorded replacements.  BL2 consumes no v3.1.1.9 runtime SDK.
A source mirror and a binary/object registry are distinct: the registry
records immutable object identity, ABI and capabilities, while a future mirror
records authorized source origin and provenance.

The only owner-designated future private mirror source origin/destination is:

`https://github.com/Embracecactus/vendor-bk-avdk-smp.git`

This designation grants no redistribution right.  A future P2 upload may
contain zero SDK bytes unless the applicable Beken/third-party licence and
explicit authorization permit GitHub cloud storage, collaborators, CI and
backups, with notices and an SBOM.  No SDK bytes, archive, key, token, private
path or host-specific location is stored by this migration.

### Hardware-gated migration

The P9a shadow phase may observe and compare the proposed binding without
changing ownership.  It must pass an owner-approved hardware gate before the
separately authorized P9b cutover changes the live owner.  P9a and P9b are
distinct decisions and evidence records; P9a success is not authorization for
P9b, Flash, trust-root, lifecycle or board changes.

## Consequences

- The P0 manifest pins all current legacy profile paths, types, modes, bytes,
  metadata and pair groups; any growth, case change, special file or mutation
  fails closed.
- The 27-row migration ledger is planning metadata only.  It does not create
  a product, board or configuration seed.
- Existing profile consumers remain until P1 migration proves equivalent
  resolved configurations and artifacts.  Historical evidence keeps its
  original names.
- Future P1 work must preserve the current SARADC physical endpoint as
  PARTIAL until the released/pressed/released hardware run exists.
- P0 records a proposed guard and migration ledger; it does not accept the
  future product resolver, backend choice or hardware cutover.  Those remain
  open for owner review.

## Reversal signals

Reopen this ADR before P1 if the migration requires changing a boot/trust
layout, modifying an official NuttX/apps/SDK tree, uploading to the private
mirror, treating a sealed-binary bundle as source-reproducible, or adding a
per-driver legacy profile.  Any such change requires a new explicit owner
decision and fresh evidence.
