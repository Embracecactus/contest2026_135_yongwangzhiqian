# N15-B CP-only staging host verification

- Status: host/source/ELF-verified; board-write gate closed
- Verified: 2026-08-04T00:51:40+08:00
- Branch: `feat/bk7258-n15-ota`
- Source HEAD before this uncommitted work:
  `a8900189c2e63a49a1f7dbf3334f5358db0d425f`
- SDK: official Beken v3.1.1.9 only
- Hardware writes: none

## Scope and result

N15-B adds a repository-owned CP adapter and portable staging core for the
exact N15-A `bk7258-cp-ap-pair-v1` candidate. The implementation does not
change official NuttX, apps, SDK source, or SDK archives.

Before any mutation, the target validates the complete `0x275000` physical
candidate and a deterministic 384-byte little-endian descriptor:

- descriptor magic/format/size/CRC32, exact schema and accepted layout ID;
- caller-pinned non-zero generation, timestamp, version, and base version;
- exact physical `0x286000..0x4fb000` and logical CP/AP/RBL geometry;
- all 32-byte + CRC16 packets and the exact 96-byte algorithm-0 RBL header;
- CP/AP vector ranges, Thumb reset entries, and CP `BK7236` magic;
- physical/logical/CP/AP SHA-256 and RBL body CRC32/FNV-1a;
- erased bytes in every gap and tail outside CP, AP, and RBL payloads.

The mutation path is CP-only. A shared Flash guard serializes staging with
the existing MTD/LittleFS owner, while read-only MTD transactions retain no
temporary SDK write permission. One staging invocation owns the full
operation; each 4 KiB sector is then an exclusive source-read, erase,
erased-readback, 256-byte program/readback transaction. A final full-slot
SHA-256 must match the descriptor before success is returned.

Failure never changes trial metadata or remap state, so B remains
unselectable. The staging compile gate is off, the runtime gate initializes
false, and the final CP ELF contains no runtime write-enable setter or NSH
command.

## Portable fault-injection matrix

The host harness compiles the same `bk7258_ota_staging_core.c` with
`-Wall -Wextra -Werror` and OpenSSL SHA-256. It passed two positive and 21
negative cases.

Positive cases:

1. full validation with zero Flash access;
2. modeled successful staging of all 629 sectors / 2,576,384 bytes, with the
   observed access range exactly `0x286000..0x4fb000` and sentinels before and
   after the slot unchanged.

Negative cases:

1. compile write gate off;
2. runtime write gate off;
3. operation deadline expiry before mutation;
4. Flash-owner lock timeout;
5. sector erase failure;
6. erased-readback mismatch;
7. program failure;
8. immediate readback mismatch;
9. final full-slot digest mismatch;
10. short/ambiguous source read;
11. source mutation after successful preflight;
12. descriptor CRC corruption;
13. physical start one sector low;
14. physical start one sector high;
15. physical size one sector short;
16. physical size one sector long;
17. descriptor generation drift;
18. descriptor version drift;
19. caller-pinned generation mismatch;
20. physical packet corruption;
21. RBL-header corruption with packet CRC recomputed.

All cases kept accesses within the exact B range and balanced every acquired
Flash guard.

## Target and full-build closure

The exact v3.1.1.9 `cp_nsh_psram + ap_smp_psram` build passed:

- checksum-pinned CP/AP SDK bundle checks and exact SDK source checks;
- accepted A/B layout, N15-A 2/13, and N15-B 2/21 host gates;
- Tier-1 rebuild/verify and final CP build-tree restoration;
- N15-B final-ELF closure, with validator, staging core, Flash guard, SDK
  permission wrapper, and stock NuttX SHA-256 symbols retained;
- factory byte layout, RPTUN, BLE GATT, and retained PSRAM gates.

The first integrated run correctly exposed an outdated retained-N14 profile
check that allowed only PSRAM lines. The verifier was narrowed to additionally
allow exactly `CONFIG_BK7258_OTA_STAGING=y`; direct ELF verification and a
second complete build then passed. No other profile drift was accepted.

Final ELF sizes from the successful build:

| ELF | File size | text | data | bss | SHA-256 |
|---|---:|---:|---:|---:|---|
| CP | 7,397,784 | 647,956 | 34,724 | 99,848 | `a0c49e8373b8ad3f339fbf3126fe6be043c27ebb1bc8cd25cbe927bbfea768dc` |
| AP | 672,696 | 170,276 | 3,180 | 30,580 | `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc` |

Official NuttX and apps had zero tracked changes after the build. The exact
SDK source worktree also had zero tracked changes. Known SDK header macro
redefinition diagnostics remained warnings.

## Fresh-build host fixture

The ignored fixture at `nuttx/bk7258-dual/n15-b-pair` uses
`generation=16`, `version=n15-b-host`, `base_version=n15-a-host`, and
`timestamp=0`. It is evidence only and carries no board-write authority.

| Artifact | Length | SHA-256 |
|---|---:|---|
| CP `app.bin` | 682,680 | `441482bf621610e973a8392289768777d3b2c44931db411ad24354fb2fa367b2` |
| AP `app1.bin` | 173,648 | `da5558c6c61f089678bf745c24c474236f154eeeb56f8db9faaef3bb5abf6af5` |
| `pair-body.bin` | 1,484,416 | `ff68860f10d231773c887487faeab0099a571d5f149da951571a5d5d6b2dc12f` |
| `pair.rbl` | 2,424,832 | `a491646c68d34c9db928e09c24f340d045c4520e5c8f1fbcbd5de10200775fed` |
| `s_app-candidate.bin` | 2,576,384 | `1a4a3a88f61f2f4dd5329792222199dc743af86999a66ce87c0ba7465ae2d112` |
| `bk7258-ota-pair.json` | 3,347 | `8c074bd69885c03eaa8284b0429f2feb5fac785492af3b388b8fd01a5841eafb` |
| `bk7258-ota-stage.bin` | 384 | `ef257fab8876269794ae60273f0205f7411fe9bc017e0e1a4a5358a26279699c` |
| `bk7258-ota-stage.json` | 2,623 | `cf105305b161a3a92c16525fe10aaea1650347d1613582d38856d9aa51df7d43` |

Two independently generated descriptors and reports were byte-identical.
The canonical descriptor verifier also passed while checking the final CP
ELF in the same invocation.

## Limits and next gate

- The supplied timeout covers Flash ownership, mutation, and final readback;
  full preflight runs first under the source callback's bounded-read contract.
- An individual synchronous official SDK Flash call cannot be aborted in its
  middle. Deadline checks occur before and between operations; this is not a
  production latency or power-loss-safety claim.
- CRC/FNV/SHA provide integrity only. Publisher authentication and
  anti-rollback remain out of scope for this first N15 implementation.
- No physical B write has been authorized or performed. B on the board is
  still the non-selectable N15-M seed.

N15-B is closed at the host/source/ELF gate. N15-C may now add team-owned
boot-time A/B pair validation and fail-closed remap selection. Metadata
mutation, one-trial confirmation/revert, and hardware fault injection remain
N15-D/E work.
