# BL1/BL2 128 KiB CRC-boundary verification

Date: 2026-08-07 (Asia/Shanghai)

## Scope

Determine whether the earlier reset at the `0x28030000` destination boundary
was caused by a bootloader SRAM/MPU limitation or by reading an incomplete
CRC-expanded BL2 XIP image. No SDK/NuttX source, OTP, or eFuse was changed.

## Evidence

- The BL2 logical partition is 128 KiB (`0x024d0000..0x024f0000`) and the
  physical 34/32-encoded span is 136 KiB (`0x22000`). This is the reserved
  **capacity/execution window**, not the length that every boot must copy.
- The earlier test wrote only the 8 KiB development BL2 package and asked BL1
  to read/copy 128 KiB. Its erased tail did not contain valid 32-byte + CRC16
  packets.
- A board-owned BL1 variant was built with `BL2_COPY_SIZE=0x20000` and flashed
  together with a complete 128 KiB logical BL2 image expanded to 0x22000
  physical bytes. COM11 log:
  `logs/bl1-valid-128k-crc-20260807-010521/capture.log`.
- The complete-image run crossed `0x28030000`, printed `bl2 ram @
  0x28020000`, `BL2RAM`, and reached NuttShell. This proves the Secure
  `0x28020000` alias can accept the full 128 KiB copy in this boot context.
- The board was restored with the normal development BL2 package. The current
  default contract is `BL2_LOGICAL_SIZE=0x3000` (12 KiB logical, `0x3300`
  physical), while the old capture below used `BL2_COPY_SIZE=0x2000` (8 KiB).
  COM11 again reached NuttShell in
  `logs/bl1-final-8k-20260807-010615/capture.log`.

## Conclusion

The apparent 64 KiB SRAM-bank limit was a false diagnosis. The reset was
caused by BL1 reading beyond the valid CRC-expanded XIP payload, not by the
linker script, MPU cleanup timing, or a permanently inaccessible SRAM bank.
BL2 packaging and copy length must be matched: either copy only the valid
logical payload or provide a complete CRC-expanded span before copying the
whole 128 KiB role. The shared `boot_bl2_contract.h`, BL1 Makefile, BL2
Makefile, and Manifest generator now enforce the distinction:

```text
reserved capacity: 0x20000 logical / 0x22000 physical
current active image: 0x3000 logical / 0x3300 physical
BL1 copy length = Manifest BL2 length = BL2 CRC padding length
```
