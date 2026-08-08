# Beken MCUboot reference flow for the BL1/BL2 reverse path

Date: 2026-08-07

## Evidence boundary

This record uses the read-only BK7236 secureboot implementation and its
official v2.0.1 documentation under `/home/lijian/project/TuyaOpen/bk_idk` as
a same-Armino-security-architecture reference. Beken's support clarification
is that the architecture is shared with BK7258, while BK7236 is single-core.
That makes the documented roles useful, but does not prove a BK7258 BootROM
format, address, register, or dual-core image mapping. No file from that
checkout is copied into the project. The only runtime SDK allowed for this
project remains Beken v3.1.1.9.

## What the official BK7236 documentation actually freezes

The local copy of `docs/bk7236/zh_CN/security/bk_security_boot.rst` gives a
vendor-described two-stage security flow:

```text
immutable BL1 / BootROM
        |
        |  verify manifest, BL2 hash and version floor
        v
BL2 / MCUboot
        |
        |  verify signed application images and security counter
        v
TFM-S + CPU0 application
```

The BL1 description is specific about responsibility, not about a BK7258
binary layout:

1. BL1 reads a manifest from a fixed Flash location. The manifest carries the
   signature algorithm, image-hash algorithm, image version and public key.
2. BL1 compares the public-key hash with the OTP BL1 root-key hash, verifies
   the manifest signature (excluding the signature field), hashes BL2 with
   SHA-256 and compares the image version with the OTP floor.
3. Only after these checks does BL1 transfer control to BL2.

The documented secure partition minimum is `bl1_control` 12 KiB,
`manifest` 4 KiB and `bl2` 128 KiB. The BK7236 document also states that its
BL2 is MCUboot 1.9.0 and describes a CRC/AES/padding step before the signed
image is assembled. These values and the single-core `TFM-S`/`CPU0 APP`
image roles are **BK7236 reference facts**, not BK7258 measurements.

This explains two current boundaries. First, the earlier 128 KiB experiment
supports the BL2 capacity/copy question; it does not enlarge the public
BK7258 BL1 boot slot. Second, the project's 256-byte development Manifest is
a recoverable board-owned authorization layer, not a reconstruction of the
vendor's undocumented manifest encoding or OTP-backed root of trust.

## Confirmed reference sequence

The reference `components/tfm/tfm/bl2/ext/mcuboot/bl2_main.c` performs the
following sequence:

```text
BL1/ROM handoff
      |
      v
bk_efuse_init / platform early init
      |
      +-- optional secure-download recovery path
      +-- watchdog and clock setup
      +-- MCUboot memory allocator setup
      +-- partition_init()
      +-- flash_map_init()
      +-- platform/OTP/security-counter initialization
      |
      v
boot_go(&rsp)
      |
      v
do_boot(&rsp)
      +-- RAM-load: vector from ih_load_addr + ih_hdr_size
      +-- direct-XIP: map physical CRC offset to virtual code offset
      +-- set VTOR/MSP and branch to reset vector
```

The important boundary is that `boot_go()` owns image validation and slot
selection. The platform `do_boot()` code only converts the selected response
into the final vector address and performs the handoff.

## CRC/XIP implication

The reference direct-XIP path uses:

```c
FLASH_PHY2VIRTUAL_CODE_START(
    primary_physical_offset + ih_hdr_size / 32 * 34)
```

That expression exists because the raw downloader stream contains 32 data
bytes followed by 2 CRC bytes. The current board implementation exposes the
decoded logical XIP view through `flash_area_read()`, so its final jump uses
the logical `ih_hdr_size` directly. These are equivalent views only if the
flash controller has already removed the CRC bytes; the two forms must not be
mixed.

## Beken imgtool delta

The read-only Beken tool adds these extensions to the standard MCUboot
`imgtool` command:

- `--action_type hash|sign|sign_from_sig`;
- `--signature`;
- `--pubkeyfile`;
- `--hash_outfile`;
- Beken-specific signature/hash JSON output.

The standard NuttX tool already provides the core options used by the board
path, including EC256 signing, version/security counter, `--boot-record`,
`--public-key-format`, `--max-align`, `--pad-header`, and `--pad`. Therefore
the board must keep NuttX as the signer and only add a board-owned adapter if
one of the Beken JSON/external-signature workflows becomes necessary.

The board packer now exposes those options directly. The v3.1.1.9-compatible
parameter set is:

```text
--header-size 0x1000 --align 1 --max-align 8
--public-key-format full --boot-record SPE --pad
```

This reproduces the standard options visible in v3.1.1.9 `bl2_sign.py` while
continuing to execute the pinned NuttX tool. It does not claim to reproduce
Beken's missing `action_type`/JSON extension or the final secureboot package.

## Consequences for the current implementation

Already aligned:

- BL2 calls the pinned NuttX `boot_go()`;
- the board flash backend presents decoded logical XIP bytes;
- CP/AP are mapped as two MCUboot images with paired vector checks;
- the final branch is outside the mutable remap window and sets VTOR/MSP.

The BK7236 documentation also confirms that these are the right *roles* to
separate: BL1 authorizes BL2, while MCUboot authorizes later images. For this
dual-core board, treating CP and AP as a coupled two-image MCUboot result is a
board adaptation; the BK7236 single-core `TFM-S`/`CPU0 APP` pair cannot be
copied as an official BK7258 mapping.

Still to prove for a complete reverse:

1. The board-owned BL1/BL2 gates now establish MSPLIM/VTOR, clear reset
   interrupt residue, invalidate I-cache, and take over the watchdog before
   entering MCUboot. Their equivalence to every required Beken platform hook
   still needs source/binary evidence.
2. The BL2 vector now uses the 8-byte-aligned top of its dedicated SRAM window
   (`0x28030000`), matching the official Cortex-M entry contract; BL1 rejects
   an unaligned BL2 MSP. BL1 also checks all 64 vector entries for Thumb
   addresses inside the BL2 SRAM window before copying control there.
3. BL1 must have an explicit, evidence-backed BL2 acceptance contract. The
   BK7236 documentation confirms the expected semantic checks (root-key
   binding, manifest signature, BL2 SHA-256 and version floor), but a vector
   check alone is not the same as the official Beken manifest/BL1
   authorization path.
4. The exact BK7258 v3.1.1.9 secure BL2 signing artifact is still missing; the
   BK7236 reference cannot be promoted to BK7258 fact without a binary or
   vendor confirmation.

The BL2 handoff now calls the board-owned `boot_prepare_app_handoff()` after
the final CP/AP remap decision. This reuses the recovered official sequence
for D-cache maintenance, MPU clearing and I-cache invalidation immediately
before VTOR/MSP/Reset branching.

No hardware verification or new test harness is introduced by this record.

The complete page-by-page text and image audit is recorded in
[`2026-08-07-bk7236-security-docs-audit.md`](2026-08-07-bk7236-security-docs-audit.md).
