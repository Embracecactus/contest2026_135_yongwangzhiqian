# ADR-024: Model BK7258 firmware configurations as physical-board profiles

- Status: Accepted
- Date: 2026-08-13
- Decision owner: Project owner

## Context

After ADR-023 separated the shared BK7258 platform from T5AI-Core and
T5-Board wiring, `board/bk7258/configs` still contained 42 historical
defconfigs.  Many names represented one debugging checkpoint rather than a
reusable firmware configuration.  Board identity, CP/AP pairing and raw versus
MCUboot packaging were inferred from filenames and hard-coded shell lists.

That shape allowed a CP for one PCB to be paired with an AP for another, made
old experiments look like supported products and duplicated nearly identical
configuration state.  It also confused a physical board with the several
valid applications that may run on it.

## Decision

1. A physical board owns immutable electrical facts under
   `board/bk7258/boards/<variant>`; a build profile selects one coherent
   feature set for that board.  One board may have multiple profiles.
2. Keep the flat `configs/<profile>/defconfig` layout required by the NuttX
   configure flow.  Encode the physical board in the profile name and in
   adjacent machine-readable metadata; do not add another nested board tree.
3. Every maintained profile has `profile.conf` schema 1 with physical board,
   CP/AP/BL2 role, raw/MCUboot boot mode, profile class and compatibility ID.
4. CP and AP may be packaged only when board, boot mode, symmetric transport
   features and compatibility ID agree.  Metadata is authoritative; filename
   whitelists and substring-based boot decisions are prohibited.
5. Use profile classes `runnable`, `validation` and `ci`.  CI profiles require
   an explicit allow gate and are never silently promoted to release images.
6. Add a profile only for a reusable product/service set or a bounded
   validation responsibility.  A historical checkpoint alone is not enough.
7. Keep T5AI-Core base as the compatibility default.  T5-Board profiles select
   their board explicitly.  `bl2_mcuboot` remains a standalone boot-stage
   profile rather than pretending to be a CP/AP pair.
8. Signed profiles require external signing/authorization keys.  Keys and
   secrets never enter defconfig, profile metadata or repository memory.
9. Serialize physical dual-image builds with a workspace-level lock because
   openvela reconfigures the shared `nuttx/` and `apps/` trees in place.
   Metadata-only profile checks remain non-mutating and lock-free.

## Consequences

- The maintained catalog is reduced from 42 defconfigs to 18: eight
  T5AI-Core profiles, nine T5-Board profiles and one standalone BL2 profile.
- Product, validation and CI intent is visible without reading every Kconfig
  line.  The packer can reject cross-board, cross-boot and incompatible pairs
  before compiling or requesting signing keys.
- T5-Board app CP can intentionally pair with several compatible AP
  application/validation profiles through one shared compatibility ID.
- Fixed pins, polarity, fitted devices and bus electrical limits are not
  duplicated into defconfigs.  Runtime I2C/SPI/UART controls keep the ownership
  defined by ADR-023.
- Historical documents may retain retired profile names as evidence, but
  current build instructions must route through the canonical catalog.
- A full build remains a shared-workspace operation.  Parallel callers wait
  instead of corrupting generated Kconfig, `Make.defs` or output artifacts.

## Rejected alternatives

1. One defconfig per physical board.  Rejected because a board legitimately
   supports different application, camera, Wi-Fi and validation images.
2. Keep every historical defconfig.  Rejected because checkpoint names do not
   define a supported compatibility or lifecycle contract.
3. Infer compatibility from names.  Rejected because naming cannot safely
   encode board identity, signing mode, symmetric IPC features and intentional
   one-to-many application pairings.
4. Put fixed pin and peripheral facts in each profile.  Rejected because it
   recreates the duplicated board database removed by ADR-023.
