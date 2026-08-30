# Verification: N15-C boot pair validation and gated remap

- Date and time zone: 2026-08-04, Asia/Shanghai
- Verifier: Codex
- Source state: branch `feat/bk7258-n15-ota`, local HEAD
  `a8900189c2e63a49a1f7dbf3334f5358db0d425f` plus uncommitted N15-A/B/C
  changes
- Environment: WSL2 Ubuntu; exact external Beken SDK source
  `/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9`; checksum-pinned
  repository SDK bundle `v3.1.1.9`
- Classification: **host/source/ELF-verified; no board write**

## Scope

N15-C adds the read-only boot half of the paired OTA design without changing
the deployed board state:

- a fixed 4 KiB append-only metadata sector with eight 512-byte records;
- complete primary A validation using actual CP/AP encoded lengths, erased
  slot padding, vectors, CP magic, CRC16 and whole-pair SHA-256;
- complete secondary B validation by reusing the N15-B descriptor/RBL core;
- fail-closed selection for erased, malformed, corrupt, stale or
  mixed-identity metadata;
- a clean-room raw-Flash reader and exact official one-offset remap register
  sequence in the team Tier-1 bootloader;
- four immutable compile/runtime selection/remap gates, all zero in the final
  ELF;
- deterministic host candidate and pending metadata generation, kept outside
  the factory image.

This phase deliberately does not erase/program Flash, consume `PENDING_B` as
permission to boot B, append `TRIAL_STARTED`, or remap the physical board.

## Frozen metadata and decision contract

Each 512-byte little-endian record contains magic/format/state, monotonic
sequence, pair generation and timestamp, the actual encoded primary CP/AP
lengths, candidate/base versions, the SHA-256 of the full padded primary
pair, the canonical 384-byte N15-B descriptor, and a record CRC32. The only
valid state path is:

`PENDING_B -> TRIAL_STARTED -> CONFIRMED_B | ROLLBACK_A`

All later records must retain the exact first-record identity and increment
sequence by one. In N15-C, `PENDING_B` is reported as a validated trial
candidate but remains mapped to A. `TRIAL_STARTED` is treated as a consumed
trial and falls back to A on the next reset. Only `CONFIRMED_B` can reach the
remap path, and final-ELF gates still prevent that path from executing.

## Commands and methods

Host/source fault matrix and exact official contract:

```bash
python3 board/bk7258/scripts/verify_bk7258_ota_boot.py \
  --self-test \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9 \
  --boot-elf board/bk7258/bootloader/bl.elf \
  --boot-bin board/bk7258/bootloader/bl.bin \
  --boot-crc board/bk7258/bootloader/bl_crc.bin --json
```

Team bootloader warning-clean build and ELF closure:

```bash
make -C board/bk7258/bootloader clean all verify
python3 board/bk7258/scripts/verify_bk7258_ota_boot.py \
  --elf-only \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9 \
  --boot-elf board/bk7258/bootloader/bl.elf \
  --boot-bin board/bk7258/bootloader/bl.bin \
  --boot-crc board/bk7258/bootloader/bl_crc.bin --json
```

Portable core static analysis:

```bash
cc -std=c11 -Wall -Wextra -Werror -fanalyzer \
  -Iboard/bk7258/bootloader -Iboard/bk7258/chip/cp \
  board/bk7258/chip/cp/bk7258_ota_staging_core.c \
  board/bk7258/bootloader/boot_ota_select_core.c \
  board/bk7258/scripts/host/bk7258_boot_ota_select_harness.c \
  -lssl -lcrypto -o /tmp/bk7258-n15c-analyzer
```

Exact full build with an explicitly requested host-only candidate:

```bash
BK7258_SDK_SOURCE=/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9 \
CP_CONFIG_NAME=cp_nsh_psram AP_CONFIG_NAME=ap_smp_psram \
N15_OTA_GENERATION=17 N15_OTA_VERSION=n15-c-host \
N15_OTA_BASE_VERSION=n15-b-host N15_OTA_TIMESTAMP=0 JOBS=8 \
board/bk7258/scripts/build_dual_image.sh
```

The wrapper was also invoked with only `N15_OTA_GENERATION=17`; it rejected
the partial identity with exit 2 before any build or artifact mutation.

## Results

### Portable model and source contracts

- 5 positive and 28 negative selector cases passed.
- Four freestanding SHA-256 vectors passed, including the million-`a` vector.
- The selector/staging host core compiled with `-Wall -Wextra -Werror` and
  GCC `-fanalyzer` without findings.
- The exact v3.1.1.9 Flash raw-read source, remap save/restore source,
  partition CSV, and official AB boot binary matched their pinned SHA-256
  values.
- The official enable function at `0x020022d8`, remap function at
  `0x020024f0`, and registers `0x44030058/5c/60/64` matched the pinned binary
  slices.

The fault matrix covers erased/pending/trial/confirmed/rollback states;
torn/dirty/gapped records; bad sequence, generation, transition, version,
length and descriptor addresses; candidate CRC/RBL/vector/read failures; and
primary CRC/padding/SHA/vector/length/read failures. Trusted metadata with an
invalid A pair is fatal; an invalid B candidate fails closed to verified A.

### Final Tier-1 ELF

| Artifact | Size | SHA-256 |
|---|---:|---|
| `bl.elf` | 159,600 | `469048c0d2a91a82a37e42fc73cbea3ae5c15bfdccfccce233b1195321e8975e` |
| `bl.bin` | 10,172 | `75efe45396232932289905f0d18192efad2010f171548a088c77715e2e0680ca` |
| `bl_crc.bin` | 69,632 (`0x11000`) | `73c81054f5563077cba93e9724b16c43bf1419ba15c205beb9da81308ae97b38` |

- Boot text is 10,170 bytes; `.data` is zero.
- The linker reserves exactly the bounded boot-only workspace
  `0x2800d000..0x28010000` (12 KiB) and requires normal `.bss` to be zero.
  The adapter clears the workspace before CP handoff.
- Required selector, N15-B validator, SHA-256, workspace and gate symbols are
  present; no undefined or forbidden Flash erase/program/runtime-enable
  symbols are present.
- The four final-ELF gate objects are read-only and each contains zero:
  selection compile/runtime and remap compile/runtime.

### Exact dual build and host candidate

The full `cp_nsh_psram + ap_smp_psram` build passed SDK bundle checksums,
N15-A 2/13, N15-B 2/21, N15-C 5/28, boot ELF, factory byte layout, RPTUN,
BLE GATT and PSRAM gates. Final ELFs:

| Artifact | Size | SHA-256 |
|---|---:|---|
| `nuttx-cp.elf` | 7,397,784 | `13c7be80ab16976c0c954eea90856d1d5d6e728b10e2db8b8ae9ede2c36b8945` |
| `nuttx-ap.elf` | 672,696 | `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc` |

The ignored host candidate is generation 17, version `n15-c-host`, base
`n15-b-host`, timestamp zero:

| Artifact | Size | SHA-256 |
|---|---:|---|
| `s_app-candidate.bin` | 2,576,384 (`0x275000`) | `43f4e16a3509868413de906e3b9548bc644dd981528608eae903dc152af8ed6d` |
| `bk7258-ota-metadata.bin` | 4,096 | `fc4009ff0d9c87fcd3a48697d99d8fcedefd80c3900151f1a64502b632e9a282` |

The metadata binds primary encoded lengths CP 725,356 and AP 184,518,
primary padded-pair SHA-256
`578f9afed60c44c78ecd5f319c99c23c7179508cf1900828c0316eb42e10c4ac`,
and the secondary candidate digest above. Its report records every
write/select/remap/trial/board gate as false.

The normal factory verifier still proves its embedded metadata sector is
all `0xff`; the generated pending metadata exists only below
`nuttx/bk7258-dual/n15-ota-host-candidate/` and is not part of a loader write
set.

## Failures and investigation

- The first strengthened layout run failed because two verifier string
  fragments did not match the linker's actual assertion wording. The
  contracts were corrected to the exact linker text and the full layout
  verifier then passed.
- No implementation, analyzer, build, packaging, or final-ELF failure
  remained.
- Existing warnings emitted from immutable SDK headers during NuttX wrapper
  builds remain non-fatal. Team Tier-1 sources compile separately with
  `-Werror`.

## Source-boundary audit

- Official NuttX and apps contain no new tracked changes after the build.
- Exact SDK bundle checksums passed for CP and AP.
- No SDK source or static library was modified.
- No Flash/board operation was requested or performed.

## Residual risks and next gate

- N15-C is not hardware verification. The deployed board still runs the
  previous A-only Tier-1 image and its B region is the non-selectable N15-M
  seed.
- N15-D must design and host-verify the append/read-back `TRIAL_STARTED`
  operation and the one-boot permission that follows it. Merely observing a
  pending record must never remap B.
- Physical candidate staging, metadata mutation, remap, confirmation,
  rollback, reset/corruption/power-loss injection and retained-service board
  regression still require separate gates and fresh owner authorization.
- CRC32/FNV/SHA-256 prove integrity only. Publisher authentication, key
  provisioning and anti-rollback remain unresolved security work.

## Evidence locations

- Final generated reports:
  `nuttx/bk7258-dual/bk7258-ota-boot.json` and
  `nuttx/bk7258-dual/n15-ota-host-candidate/` (ignored build artifacts).
- Source entry points:
  `board/bk7258/bootloader/boot_ota_select_core.c`,
  `boot_ota_select.c`, `boot_sha256.c`, and
  `board/bk7258/scripts/verify_bk7258_ota_boot.py`.
- Architecture decision:
  `memory/decisions/ADR-005-n15-boot-selector-metadata-v1.md`.
