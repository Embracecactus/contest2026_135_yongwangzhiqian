# N15-A host pair-bundle verification

- Status: host-verified
- Verified: 2026-08-04T00:00:24+08:00
- Branch: `feat/bk7258-n15-ota`
- Source HEAD before this uncommitted work: `a8900189c2e63a49a1f7dbf3334f5358db0d425f`
- SDK: official Beken v3.1.1.9 only
- Hardware writes: none

## Scope and result

N15-A adds a repository-owned deterministic builder and fail-closed verifier
for one CP/AP candidate generation. It clean-room reproduces the exact plain
v3.1.1.9 RBL header/container and 32-byte + CRC16 encoding without changing or
calling into official SDK/NuttX source at runtime.

The canonical host schema is `bk7258-cp-ap-pair-v1`. Both component entries
must carry the same non-zero uint64 generation and candidate version. The
manifest binds the accepted layout ID, fixed logical offsets, XIP vectors,
lengths and SHA-256 digests. RBL retains the official algorithm-0 values:

- logical pair/container size: `0x250000`;
- CP logical offset/capacity: `0x000000 / 0x140000`;
- AP logical offset/capacity: `0x140000 / 0x110000`;
- RBL header: 96 bytes at logical `0x24f000`;
- RBL `app_partition`: `app`;
- RBL `current_version`: official fixed
  `00010203040506070809`;
- physical CRC-expanded candidate: exactly `0x275000`, targeting only
  `0x286000..0x4fb000` in later stages.

The host bundle is deliberately non-deployable by itself. Its manifest fixes
`boot_selectable`, `staging_writes_enabled`, `remap_enabled`,
`trial_metadata_mutation_enabled`, and `board_write_authorized` to `false`.
CRC32, FNV-1a and SHA-256 are described as integrity checks only; publisher
authentication and anti-rollback remain false.

## Independent format evidence

- The verifier pins SHA-256 for the exact v3.1.1.9 RBL packager, OTA packing
  wrapper, CRC implementation and plain `ota_rbl.config`.
- `inspect_bk7258_rbl.py --self-test` compares the team encoder against a
  header generated independently by official `gethead()` for a fixed body and
  timestamp.
- The pair self-test compares team 32+2 encoding against an independently
  generated official `bk_crc16.crc16_data()` packet.
- Official `ota_rbl.config` is checked as `gzip=0` and `aes=0`; no demo key or
  IV is copied into the project.

## Test matrix

The N15-A self-test passed two positive cases and thirteen fail-closed negative
cases:

1. repeated deterministic build and canonical bundle verification;
2. physical CRC packet corruption;
3. logical RBL header corruption;
4. pair-body corruption;
5. physical target-address drift;
6. declared length drift;
7. layout-ID drift;
8. invalid version syntax;
9. mixed CP/AP generation;
10. CP reset-vector address drift;
11. caller-expected version mismatch;
12. caller-expected generation mismatch;
13. AP body overflow into the reserved RBL tail;
14. duplicate JSON key rejection.

The total is two positives because deterministic build A and build B are each
verified; the first numbered item above describes that paired positive gate.

## Exact v3.1.1.9 clean-build evidence

`cp_nsh_psram + ap_smp_psram` was rebuilt through
`build_dual_image.sh` with `BK7258_SDK_SOURCE` pointing to the read-only exact
v3.1.1.9 source. The following gates passed:

- CP/AP SDK archive checksum checks;
- accepted A/B layout and official source verification;
- N15-A `positive=2 negative=13` self-test;
- Tier-1 rebuild/verify;
- factory byte-layout verifier;
- RPTUN layout, BLE GATT and PSRAM verifiers;
- final CP build-tree restoration and artifact equality checks.

The known SDK macro-redefinition diagnostics remained warnings; the build
exited zero. Official NuttX and apps had no tracked modifications afterward.

## Fresh-build host fixture

The following generated fixture is retained only under ignored build output at
`nuttx/bk7258-dual/n15-a-pair`. Its metadata
(`generation=15`, `version=n15-a-host`, `base_version=n15-m-board`,
`timestamp=0`) is a deterministic host-test fixture, not a production release
or board-write authorization.

| Artifact | Length | SHA-256 |
|---|---:|---|
| `pair-body.bin` | `1484416` (`0x16a680`) | `68789d089b0866151db47f8e261d64ede523b2d1f8106f68b31248aa1df89111` |
| `pair.rbl` | `2424832` (`0x250000`) | `78d3ea6524a3d3f2144769867ee79c3acce0b222898e8bffe5c8b35294fe9d98` |
| `s_app-candidate.bin` | `2576384` (`0x275000`) | `f08228ea768acdf1febe8f7fead380bd86cf339d59d514ca236c307796eaebc5` |
| `bk7258-ota-pair.json` | `3350` bytes | `6aa33b93181ebb5ff60fe8eede25fd9c3958ea44fdbdf23a146e7684f5cf2895` |

A second independent output directory built with identical inputs and explicit
metadata was byte-identical under `diff -rq`.

## Exit and next gate

N15-A is complete at the host-only gate. N15-B may implement a CP-owned
`s_app` staging wrapper with bounded erase/program/read-back, but it must
remain compile/runtime gated and must not write a board until separate owner
authorization. Boot remap, trial metadata, confirmation and rollback remain
N15-C/D work.
