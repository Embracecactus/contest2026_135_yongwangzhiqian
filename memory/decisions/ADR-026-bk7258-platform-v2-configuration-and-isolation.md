# ADR-026: BK7258 platform-v2 configuration freeze and isolation model

- Status: Superseded by ADR-028. Retained as historical evidence for the P0-P9a cutover.
- Date: 2026-08-16
- Supersedes: ADR-024 (the ADR-024 historical body is retained unchanged)
- Decision owner: Project owner

## Context

The pre-cutover BK7258 tree had a useful, already-reduced legacy catalog, but
its `configs/<profile>` directories combined board identity, role, boot format,
SDK selection and validation intent. Build, verifier and documentation
consumers also referred to profile names directly. The accepted cutover now
retains only the three reviewed seeds `bl2_mcuboot`, `t5ai_core_cp_base` and
`t5ai_core_ap_base`; the 27-row freeze/ledger remains historical coverage for
P9b equivalence and hardware review.

P0 froze the exact legacy tree and recorded its consumers. The later approved
product cutover creates no new profile family: product manifests, board
variants, role composition and the three seed fixtures are now the canonical
inputs, while the frozen 27-row object remains an immutable historical
reference.

## Decisions

### Official adaptation layers and configuration model

The public adaptation model is exactly:

```text
Architecture (upstream NuttX, unchanged here)
    <- Chip/SoC (BK7258-intrinsic mechanisms)
        <- Board (pins, bindings, external devices, bring-up)
```

Architecture is the upstream NuttX contract and any generic framework change
is recorded as `external_upstream_needed`; this repository does not patch the
public NuttX/common tree.  Chip owns BK7258-intrinsic startup/arch APIs, IRQ,
serial, timer, heap, on-chip controllers, AP topology/lifecycle,
mailbox/IPI/shared-memory, RPTUN transport, clock/power lower-halves and
board-independent cross-core mechanisms.  Board owns schematic/BOM facts,
pin and bus bindings, external peripheral/power registration, linker/board
configuration and the official board initialization phases.

`vendor_common_glue`, `build_adapter`, and `migration_pending` are internal
responsibility tags, not peer formal layers.  Existing `src/` entry/glue is
classified per symbol: BK7258 mechanisms target Chip, board policy targets
existing Board bring-up, and no platform/services/drivers/validation layer or
directory is created.  Production validation auto-start is recorded as
`migration_pending` away from Chip toward Board policy.

The official board phase contract is `board_early_initialize`,
`board_late_initialize`, `board_app_initialize`, and
`board_app_finalinitialize`.  Board selection is exactly one and is
fail-closed before configuration resolution.  Validation is a mode/capability
concern and never becomes a formal adaptation layer.

The product rule is `family + mode + role`: a reusable product family may have
bringup, application, validation or factory modes, with CP/AP/BL2 roles
resolved as one resource graph.  A new capability or validation suite does not
add a `configs/` directory.  A new profile is allowed only for a genuinely
different product mode, lifecycle, boot layout or architecture boundary and
requires a new reviewed ADR; the default policy is a few reviewed seeds/modes
rather than profile growth.

The resource graph spans CP, AP and BL2 and every lifecycle phase (bring-up,
runtime, suspend/resume, recovery and teardown).  Pair compatibility is a
graph property, not a filename convention.  Board selection is exactly one
and is fail-closed before configuration resolution.

### Build adapters, artifacts and seed boundaries

P1-P3 tools are adapters that generate Kconfig/.config metadata and invoke the
existing build semantics.  CMake is the recommended adapter; it is not an
alternative build system.  Classic Make remains a supported compatibility
adapter, and any isolation report must not claim more than is proven.

The cross-backend build contract produces `libarch.a`, normalized selected
board archive `libboard.a`, and the final openvela runtime image.  Classic
Make additionally creates upstream generic `libboards.a` as an internal
archive; CMake folds those generic board objects into `libboard.a`, so the
separate `libboards.a` file is not a cross-backend package requirement.  This
  dual-core port exposes the canonical role images as `vela_nuttx_cp.bin` and
  `vela_nuttx_ap.bin`, with `vela_nuttx_manifest.json` binding the two aliases
  to their internal sources and byte hashes. Single-role postbuild retains the
  generic `vela_nuttx.bin`; `app.bin`/`app1.bin` and CRC files remain internal
  or vendor Flash artifacts. A later `.bkpack` is an additive Beken delivery
  extension and never replaces the standard outputs or build flow.

Production BL2 remains the current
`board/bk7258/bootloader/bl2/Makefile` path for now.  P0 must not add a fake
NuttX BL2 seed.  The final number of product/configuration seeds follows the
few-seed policy and is accepted only for a genuinely different product mode,
lifecycle, boot layout or architecture boundary.

BL1, BL2, CP and AP each require isolated inputs and output directories.  No
shared `.config`, restore-after-build trick or global lock is an acceptable
substitute for isolation in the target design.

The final evidence establishes a four-role isolated compile-only baseline:
BL1/BL2/CP/AP each compile from one materialized read-only entity snapshot,
with role-private roots and a reconciled `COMPILE_ONLY` boot policy. This
baseline does not imply that BL1/BL2 artifacts are runnable or trusted, and it
does not include postbuild, signing, packaging, Flash or hardware execution.

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

- The P0 manifest pins the historical 27 profile paths, types, modes, bytes,
  metadata and pair groups; any mutation of that reference fails closed.
- The approved cutover retains exactly three seed fixtures and routes current
  product resolution through board/product/fragments rather than legacy
  profile names. The 27-row ledger remains evidence and P9b equivalence input;
  historical verification keeps its original names.
- Future P1 work must preserve the current SARADC physical endpoint as
  PARTIAL until the released/pressed/released hardware run exists.
- The accepted P0-P9a structural baseline now includes the reviewed product
  resolver, role composition and three-seed cutover; it does not authorize a
  trust-root change, production signing, hardware cutover or uncommitted
  runtime acceptance.
- The isolated postbuild emits and checks the canonical
  `vela_nuttx_cp.bin`/`vela_nuttx_ap.bin` aliases and
  `vela_nuttx_manifest.json`. Host-only bkpack fixtures verify that the final
  archive contract includes those members; production package delivery and
  signing remain `NOT_RUN`.
- The isolated executor provides a verified four-role `compile-runtime`
  phase from a materialized read-only snapshot with role-private roots and
  artifact records. It reconciles boot policy in `COMPILE_ONLY` mode and its
  isolated postbuild emits the canonical aliases, but does not close P9b or
  authorize signing, production package delivery, hardware, legacy-profile or
  validation migration. Signing requires separate authorization, and the
  compile-only boot artifacts are not a runnable or trusted delivery result.

## Reversal signals

Reopen this ADR before P1 if the migration requires changing a boot/trust
layout, modifying an official NuttX/apps/SDK tree, uploading to the private
mirror, treating a sealed-binary bundle as source-reproducible, or adding a
per-driver legacy profile.  Any such change requires a new explicit owner
decision and fresh evidence.
