# ADR-017: BK7258 official Secure Boot source crosswalk

Status: accepted reference boundary  
Date: 2026-08-07

## Decision

Treat the official `bk_idk` `release/v2.0.1` checkout as two different
evidence classes:

1. Its `docs/bk7258/**` pages and generic Beken security tools are accepted
   as official BK7258 architecture and packaging references.
2. Its buildable `projects/security/**` secureboot examples are BK7236-only
   and remain read-only single-core examples. They are not BK7258 runtime
   source, address maps, or CP/AP implementations.

The project continues to use only the official BK7258 SDK v3.1.1.9 and the
pinned NuttX MCUboot source for runtime/build input. No official source is
modified. OTP/eFuse provisioning, Secure Boot enablement, AES key writes,
lifecycle changes and debug locks remain outside the reversible development
scope.

## Evidence

- `docs/bk7258/en/developer-guide/security/bk_security_boot.rst` documents
  immutable BL1, primary/secondary BL2 Manifest selection, BK7258 BL2 as
  MCUboot 1.9.0, and the AES/CRC/padding/merge/sign order.
- `docs/bk7258/en/developer-guide/config_tools/bk_config_partitions.rst`
  documents 4 KiB Manifest partitions and 128 KiB logical BL2 partitions.
- `tools/env_tools/beken_utils/scripts/genbl1.py` plus the temporary
  `secure_boot_tool` run produce the `0xa1bc2fd8`, `0x10001`, `0xd5` generic
  Manifest record described in the linked verification record.
- `projects/security/*/config/bk7236` is the only concrete secureboot
  project family in this checkout; no BK7258 secureboot project configuration
  exists.
- The v3.1.1.9 BK7258 board input still carries `secureboot_en=FALSE` and
  `security_boot_ena=0`; its read-only `bl1_sign.py`/`bl2_sign.py`/
  `partition.py` call chain nevertheless establishes the source packaging
  order as logical merge -> MCUboot sign/pad -> optional AES -> 32+2 CRC ->
  physical tail/status placement. This source fact is separate from
  BootROM acceptance.

## Consequences

- The current board-owned `beken-candidate-v1` parser/packer is a verified
  generic-format compatibility experiment, not an official BootROM ABI claim.
- Full reverse completion requires implementing and testing the missing
  primary/secondary BL2 + Manifest fallback behavior in the board-owned
  development chain before any hardware-rooted claim can be made.
- The official 1.9.0 imgtool is a reference only; the project does not mix it
  into the v3.1.1.9/NuttX runtime chain.
- The BK7236 source confirms the two-stage responsibilities and the
  fail-closed/fallback order, but its BL1_2 encrypted image structure is not
  the BK7258 Manifest ABI. If CP and AP remain separate MCUboot images, the
  board layer must enforce same-slot pairing: upstream `boot_go()` validates
  image IDs independently and its response identifies only the first enabled
  image. The official composite `primary_all` approach is the alternative,
  not an implicit property of `MCUBOOT_IMAGE_NUMBER=2`.
