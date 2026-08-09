# BK7258 BL1 -> BL2 -> MCUboot ABI freeze

Date: 2026-08-07

## Scope

This record freezes the board-owned address contract used by the current
recoverable MCUboot proof.  The source of the partition numbers is the
generated v3.1.1.9 `bk7258_partition_layout.h`; the runtime behavior was
checked by the BL2 build and the signed CP/AP handoff capture recorded in
`2026-08-07-mcuboot-bl2-pair.md`.

No official SDK, NuttX or upstream MCUboot file is modified.  No OTP/eFuse
operation is part of this ABI.

## Two address views

The Beken flash stream stores 32 logical bytes followed by 2 CRC bytes.  The
raw downloader uses the physical stream, while BL1, BL2 and the application
execute through the decoded logical XIP view.

```text
raw flash:  [32 data bytes][2 CRC bytes] [32 data bytes][2 CRC bytes] ...
                | 34 physical bytes -> 32 logical XIP bytes |

BL1: raw partition -> decoded logical bytes -> BL2 SRAM at 0x28020000
BL2: MCUboot reads decoded XIP -> validates CP/AP pair -> optional remap
CPU0: selected CP vector -> selected CP image (with paired AP present)
```

The board ABI therefore uses `raw / 34 * 32` for a physical-to-XIP offset and
`logical / 32 * 34` for a logical-to-raw span.  The compile-time checks and
these conversions live in `bootloader/bl2/include/bk7258_bl2_abi.h`.

## Frozen regions

| Region | Raw flash | Raw span | Logical/XIP view | Logical span |
|---|---:|---:|---:|---:|
| BL1 bootloader | `0x000000` | `0x11000` | `0x02000000` | `0x10000` |
| CP A | `0x011000` | `0x154000` | `0x02010000` | `0x140000` |
| AP A | `0x165000` | `0x121000` | `0x02150000` | `0x110000` |
| CP B / AP B pair | `0x286000` / `0x3da000` | `0x154000` / `0x121000` | `0x02260000` / `0x023a0000` | `0x140000` / `0x110000` |
| BL2 package | `0x51d000` | partition capacity `0x22000` | `0x024d0000` | `0x20000` capacity |

The current BL2 binary is smaller than its reserved capacity: the build emits
8,620 raw bytes, pads the logical image to `0x3000` for the BL1 copy, and
produces 13,056 CRC-expanded bytes.  The reserved `0x20000` logical capacity
remains a build-time upper bound; it is not a claim that the current package
occupies the whole partition.

## Image and handoff contract

1. The board packer adds the 0x200-byte MCUboot image header and EC256
   signature before applying Beken's 32+2 expansion.
2. `MCUBOOT_IMAGE_NUMBER=2` maps image 0 to CP A/B and image 1 to AP A/B.
3. Every flash-area size is the decoded logical size.  `flash_area_read()`
   reads the direct-XIP view and never exposes CRC bytes to MCUboot.
4. `boot_go()` must validate both images.  Before the CP branch, BL2 checks
   that the selected AP vector has an aligned MSP in the AP SRAM window and a
   Thumb reset address.
5. If MCUboot selects B, BL2 remaps the CP A XIP window through the B pair:

   ```text
   remap_begin  = 0x02010000
   remap_end    = 0x02260000
   remap_offset = CP_B_XIP - CP_A_logical_offset = 0x02250000
   ```

   The remap is enabled only after validation and instruction-cache barriers;
   BL2 itself executes from SRAM, so the code that enables remap is not fetched
   from the window being changed.
6. BL1 keeps its approximately 8-second watchdog recovery window.  The
   board-owned BL2 flash-read path feeds a 60-second period during hashing and
   ECC validation, which is required by the measured development-board path.
7. If MCUboot selects the B pair but the paired B AP vector is malformed,
   BL2 rejects the pair and enters its fail-closed path.  The A attempt was
   already performed with both secondary areas hidden, so BL2 never silently
   mixes CP-B with AP-A or retries an A pair that was already rejected.

## What this does not freeze yet

The latest source also adds the board-owned BL2 platform gate (VTOR,
pending-interrupt cleanup, I-cache invalidation and watchdog takeover) before
`boot_go()`, read-back validation for the remap registers, and distinct
fail-closed AP-vector diagnostics.  The current-source B-slot/remap proof is
recorded in `2026-08-07-mcuboot-b-slot-remap-current-board.md`; the normal-A
restore is recorded in the same record.

- The current source now has independent board captures for B-slot
  selection/remap, a signed-but-invalid B AP-vector rejection, and the
  normal-A restore.  The negative record is
  `2026-08-07-mcuboot-b-ap-vector-reject-board.md`.
- It does not add N15/N17 journal writes, anti-rollback counters, or a new OTA
  downloader.
- The source key is the checked-in development public key.  The previous
  hardware proof used a temporary development key and was restored afterward;
  its private key was not stored in the repository.

## Pinned MCUboot source behavior used by BL2

The board wrapper is built against the repository's pinned `apps/boot/mcuboot`
source; this section records the relevant upstream roles without changing that
source:

- `boot_go()` clears the loader state and enters the direct-XIP
  `context_boot_go()` path.
- With `MCUBOOT_IMAGE_NUMBER=2`, the loader opens CP image 0 and AP image 1
  through `flash_area_id_from_multi_image_slot()` and validates each image.
- Direct-XIP selection chooses the highest valid version in each image's two
  slots.  The board wrapper consequently treats the CP result as the handoff
  authority and performs an explicit AP vector check for the same selected
  pair before branching.
- `fill_rsp()` returns the selected image-0 header and logical XIP offset.  It
  does not program Flash in this configuration because no swap strategy is
  enabled and the board `flash_area_write/erase` operations are intentionally
  immutable failures.

This is why the current proof is a direct-XIP boot proof, not an OTA/trailer
implementation.  Slot confirmation, writable swap state and rollback policy
remain separate work and are not silently inferred from these loader calls.

## Verification for this freeze

- `make -C board/bk7258/bootloader/bl2 -j32` passes; the current BL2 raw
  binary is 9,840 bytes and the CRC-expanded output is 13,056 bytes for the
  `0x3000` logical copy.  The package pads the BL2 CRC segment to 16 KiB for
  the BKFIL write range.
- `make -C board/bk7258/bootloader -j32` passes with the unchanged BL1
  8-second watchdog boundary.
- The pinned `imgtool.py verify` accepts both CP and AP images in a newly
  generated version `18.0.0` development pair.  The private test key stayed in
  `/tmp` and was not copied into the repository.
- The current source's header-bounds, reset-range and B-to-A remap checks were
  exercised on the board with the temporary development signing key kept only
  in `/tmp`; the final normal-A restore also passed.  This remains a
  recoverable software-rooted proof, not official BK7258 Secure Boot.

## Tool provenance decision

The official v3.1.1.9 tree was audited before changing the board packer:

- `tools/env_tools/bk_py_libs/bk_crc/bk_crc16.py` is present and implements
  the official 32-byte data plus 2-byte big-endian CRC packet format.
- `bk_packager_linear_crc` and `bk_ota_pack.py` are present and are the
  official partition/package pipeline for the normal SDK OTA image.
- The secureboot helper `beken_utils/scripts/bl2_sign.py` is present, but its
  referenced `tools/mcuboot_tools/imgtool.py` is absent from the v3.1.1.9 tree.
  Therefore the local SDK cannot currently reproduce its secure BL2 signing
  command without an additional official artifact.
- The board script uses the pinned NuttX MCUboot `imgtool.py` only for the
  standard MCUboot proof.  Its small board-owned role is to compose the CP/AP
  pair and invoke CRC expansion; it must not be described as the official
  Beken secureboot packer.

Decision: reuse the official v3.1.1.9 CRC/partition tools whenever the exact
tool exists, keep board code as a thin adapter, and do not copy or infer the
missing secureboot `imgtool.py` from v4.0.1, BK7259, or another SDK.  The exact
official secure BL2 package remains an evidence request to Beken.
