# ADR-028: BK7258 single CLI and data-owned build

Status: Accepted

Date: 2026-08-20

## Context

The former BK7258 tool tree exposed framework, product/catalog, registry,
SDK set/lock, resource graph, executor, snapshot and many wrapper CLIs. The
same SDK version/profile and Flash geometry were repeated in Python, shell,
Make, CMake, schemas and tests. Maintainers had to understand host planning
metadata before reaching the real build, image and package operations.

## Decision

- `tools/bk7258/bk7258.py build|sdk|package|verify` is the only public tool.
- Internal implementation is limited to six domains: build, SDK, layout,
  image, package and trust. A new file requires an independent format,
  transaction or security boundary.
- Every build input is explicit. There is no product preset, implicit CP/AP
  pair, boot default, partition default, output override, repository override
  or key search.
- The team manifest pins both the SDK checkout and OpenVela ARM prebuilt.
  OpenVela, SDK rebuild, project BL1 and project BL2 use that one toolchain;
  there is no host-PATH or developer toolchain fallback.
- The team manifest is the SDK source/version authority. SDK profiles are
  discovered by `<role>[-<variant>].config`; each profile records one accepted
  bundle-tree hash and any NuttX-owned closure omissions in comments.
- The selected eight-column CSV owns partition geometry, Artifact and Policy.
  Build-local derivatives include an official six-column SDK CSV, C header and
  linker fragment. Generated layout files are not tracked.
- `STORAGE_TOPOLOGY` is one of on-chip persistent, removable block or fixed
  block. It is application- and board-name-independent; resolved system
  Kconfig must match the explicitly selected CSV.
- OpenVela's official build entry and NuttX basenames are preserved. The
  project does not retain a dual-image shell or board postbuild hook.
- Image bytes are finalized before packaging. `.bkpack` is a deterministic,
  independently verifiable project container; its canonical JSON is delivery
  evidence, not configuration authority.
- Private-key handling is explicit and isolated in the trust domain. Public
  evidence is embedded in the package; no separate trust contract is kept.
- The official Beken bootloader is reference-only. Project BL1 and the
  board-verified freestanding project BL2 build out of tree; pinned upstream
  MCUboot remains the CP/AP signature verifier.
- Debug, UART, J-Link and Flash transport remain in the existing Windows
  hardware-debug SOP, outside `_lib`.

## Consequences

- Historical build plans, executor manifests and `.bkpack` files are not
  compatibility inputs; they can be regenerated or retired.
- Adding/moving/resizing a supported partition changes only the CSV. Adding a
  new artifact format changes the owning image algorithm.
- SDK upgrades change the manifest pin and rebuild profile bundles; Make,
  CMake and Python contain no version switch.
- A raw build cannot be described as signed or hardware-verified. Signed
  delivery requires a matching boot profile, compiled public anchors and
  explicit keys.

## Supersedes

This decision supersedes the active architecture portions of ADR-024 and
ADR-026 that relied on product catalogs, build plans or an isolated executor.
Those records remain historical evidence.
