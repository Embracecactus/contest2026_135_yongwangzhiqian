# ADR-015: MCUboot packer provenance for the BL1/BL2 reverse path

- Status: Accepted for the current reverse-engineering phase
- Date: 2026-08-07

## Decision

Use official v3.1.1.9 tools whenever the required operation exists:

- `bk_crc16.py` for Beken's 32-byte plus 2-byte CRC stream;
- `bk_packager_*` and the SDK partition generator for normal partition
  placement;
- the pinned NuttX MCUboot `imgtool.py` for the current standard-MCUboot
  direct-XIP proof.

The board-owned script may remain only as a thin CP/AP composition and
validation adapter.  It must not be presented as the official Beken secureboot
packer.

The CRC helper is vendored, without source edits, at
`tools/vendor/bk7258-sdk-v3.1.1.9/bk_crc16.py`. This removes the build-time
dependency on a separately installed SDK while retaining exact v3.1.1.9
provenance in `ORIGIN.md`. The board CRC wrapper calls that snapshot; it does
not maintain a second CRC implementation.

## Evidence and limitation

The v3.1.1.9 tree contains `bl2_sign.py`, but the referenced
`beken_utils/tools/mcuboot_tools/imgtool.py` is absent.  Its exact secure BL2
format therefore cannot be reproduced from this local SDK alone.  The missing
tool or an official output package must be obtained from Beken before claiming
an exact secureboot BL2 reproduction.

Do not copy the tool from v4.0.1, BK7259, or another SDK.  No SDK or NuttX
source is modified by this decision.

## Consequence

The current 0x200-byte-header EC256 pair is a standard MCUboot behavioral
proof, not yet the official Beken secureboot BL2 image format.  The distinction
must remain explicit in code, verification records, and future hardware
reports.

`imgtool.py` remains sourced from the pinned NuttX tree. The vendored SDK
snapshot supplies only the BK7258-specific 32+2 CRC operation.

The board packer exposes the standard NuttX equivalents of the v3.1.1.9
`bl2_sign.py` options (`header-size=0x1000`, `align=1`, `max-align=8`, full
public key, `SPE` boot record and trailer padding). Beken's private
`action_type`/JSON extension is intentionally not copied.

## Official support boundary

Beken technical support confirmed that BK7258 hardware supports Secure Boot,
but the current SDK does not contain a BK7258 Secure Boot adaptation. This
confirms a hardware capability, not the availability of a reproducible
v3.1.1.9 BL1/BL2/manifest/signing package. OTP/eFuse provisioning therefore
remains explicitly outside the current reversible development path.
