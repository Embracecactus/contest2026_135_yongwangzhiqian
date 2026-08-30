# BK7258 single-CLI boot/storage source checkpoint

Date: 2026-08-20

## Conclusion

The new architecture now has one CLI, six internal domains, three
application-independent storage topologies, project-owned out-of-tree BL1/BL2
definitions and explicit public/private signing boundaries. No old framework,
registry, product, plan, executor or standalone packer is an active input.

This is a source/host checkpoint. It is not a final signed, prebuilt-toolchain
or hardware acceptance result.

## Evidence

- Layout parse PASS:
  - on-chip: `bk7258-091dd617ad7d1d49`;
  - removable: `bk7258-5641c11040abf787`;
  - fixed: `bk7258-381e2cdd1286ac59`.
- Before the compiler source was changed to the manifest-pinned OpenVela
  prebuilt, official CP/AP CMake plus the new project BL1 completed. BL1 raw
  text/data/bss was `3558/0/0` bytes and all outputs were under `out/`.
- `image.py` emitted final direct artifacts and explicitly preserved
  Manifest A/B and BL2 A/B.
- Two byte-identical unsigned project-BL1 packages were created and verified:
  SHA-256 `d39ad4c657bc9d6b84ceb1ac446e1e3b900decc0d96eeb003b8885a99773e42e`.
- Host-only temporary P-256 keys generated both build-local public C sources
  and one 256-byte Beken-shaped BL1 Manifest. Temporary private keys were
  deleted afterward.
- Current Python sources compile; both out-of-tree Makefiles parse with all
  explicit inputs; team manifest XML, project-memory structure and
  `git diff --check` pass.

## Explicitly not verified

- The official ARM prebuilt checkout at pinned commit `948af44a...` is not
  fully synchronized; no build has run after eliminating the `/usr/bin`
  compiler fallback.
- The rewritten project BL2 Makefile and signed release/public verification
  have not run end to end.
- Removable and fixed block storage backends have not been built or exercised.
- No firmware was flashed and no irreversible hardware state was touched.
