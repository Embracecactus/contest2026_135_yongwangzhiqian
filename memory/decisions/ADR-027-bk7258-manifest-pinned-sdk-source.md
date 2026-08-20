# ADR-027: Manifest-pinned BK7258 SDK source

- Status: Accepted
- Date: 2026-08-20
- Decision owner: Project owner
- Supersedes: ADR-026 only for the proposed BK7258 SDK source-mirror and
  developer-path model; ADR-026 build/configuration isolation remains in force
  until separately retired.

## Context

The three supported physical boards use the same BK7258 SDK baseline.  The
previous bundle registry retained content hashes, but the source provenance
pointed at an extracted host archive and a proposed private mirror that did
not exist.  The project owner created a public fork from Beken's official
`release/v3.1.1.9` tag.  Its OpenVela branch changes only the two reviewed CP
and AP default configuration files and was verified byte-for-byte against the
source tree used to produce the current bundles after excluding build output
and Python caches.

Representative OpenVela ports use in-tree HALs, manifest-managed vendor
repositories, exact-commit build-time HAL checkouts, external SDK paths or
downloaded binary bundles.  BK7258 is too large and role-specific to copy into
the board port, while an arbitrary host path does not provide reproducible
source identity.

## Decision

- `contest2026_135_yongwangzhiqian.xml` owns one SDK project at
  `vendor/beken/bk_avdk_smp` through the existing `git` remote.
- The manifest pins full commit
  `cb080de1655d579c7593ecf504c440997c4c137b`, whose tree is
  `cd5a35d8d59629d6a48a0b9122fabbf94e45cc60` and whose annotated tag is
  `openvela/v3.1.1.9-1`.
- The upstream baseline remains Beken `release/v3.1.1.9` at
  `c3b560f3b972db7bf3883edaffa3b49060a865cd`.
- Repository synchronization is the only normal network acquisition step.
  Firmware builds, SDK bundle verification, signing and packaging remain
  network-free.
- OpenVela SDK profiles remain owned by this contest repository and are not
  copied into the SDK fork.  The SDK commit/tree, ordered profile hashes,
  compiler identity and output bundle manifests form separate identity layers.
- Imported CP/AP bundles remain ignored derived artifacts.  Their tracked
  manifests and provenance remain the byte-level runtime-link evidence.
- Product, build-plan and package metadata derive one minimal CP/AP evidence
  map directly from those files.  Board-specific SDK set/lock documents and
  their propagated identities are not part of the architecture.
- The SDK registry, framework receipt and manifest/provenance pairs are
  retired; each profile carries one accepted deterministic bundle-tree hash.
- `bk7258.py sdk verify|install|manifest|rebuild` is the canonical
  bundle-facing interface. Rebuild uses the manifest source/version, a
  temporary project/build tree and a locked recoverable three-file-set swap.
- Older SDK bundle selection and its tracked checksum/provenance records are
  retired; only v3.1.1.9 CP/AP and the AP-only SDIO4 profile remain supported.
- `bk7258.py sdk rebuild --source` is the sole explicit migration override;
  the old environment/path spellings are retired.
- SDK source is not copied into `board/bk7258`, and build output, caches,
  credentials and private keys are never added to the manifest project.

## Consequences

- A synchronized workspace has one deterministic SDK path and no
  developer-specific source path is required.
- All three boards share one source contract; role/profile differences do not
  create board-specific SDK source locks or package identities.
- Changing the manifest commit, SDK tree, profile or compiler is a reviewed
  SDK update and requires regenerated bundle provenance plus proportional
  host and board verification.
- The existing framework/isolated executor may be retired later, but source
  commit/tree checking, role separation, profile identity and bundle hashes
  must survive in the smaller replacement tools.
- This decision does not authorize SDK modification, production signing,
  hardware mutation or automatic `repo sync` during a build.

## Alternatives rejected

- Copying the full SDK into the logical board tree: excessive duplication and
  unclear upstream ownership.
- Tracking only prebuilt libraries: preserves bytes but cannot reproduce them.
- Accepting a version string or moving branch: not deterministic.
- Requiring every maintainer to supply an arbitrary absolute SDK path: weak
  onboarding and no common source identity.
- Using Beken `release/v4.0.1`: current v4 trees target BK7259 and do not carry
  a complete BK7258 CP/AP platform.

## Reversal signals

Revisit this decision if OpenVela adopts an authoritative BK7258 SDK project,
Beken publishes a newer tagged BK7258 release that passes the full migration
gate, redistribution terms change, or the port eliminates all vendor SDK
source and binary dependencies.
