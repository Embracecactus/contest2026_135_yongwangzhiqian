# BK7258 MCUboot BL2 CP/AP A/B proof

Date: 2026-08-07

## Scope

This is a recoverable development proof for the board-owned BL2 path. It uses
the pinned MCUboot bootutil sources and the v3.1.1.9 CP/AP build outputs. No
SDK, NuttX or MCUboot source was modified, and no OTP/eFuse value was written.

## Implemented boundary

- BL2 is linked as a bare-metal board image and copied by BL1 from the
  dedicated `bl2` partition into `0x28020000`.
- `MCUBOOT_IMAGE_NUMBER=2` maps image 0 to CP A/B and image 1 to AP A/B.
- The board-owned packer uses `imgtool.py` to add 0x200-byte MCUboot headers
  and EC256 signatures, then performs Beken's 32-byte-data + 2-byte-CRC
  expansion. The B pair is written at raw `0x286000`.
- BL2 checks the selected AP vector before handing control to the selected CP.
- The BL2 flash-read wrapper feeds a 60-second watchdog period while image
  validation runs. BL1 retains its 8-second recovery period.

## Hardware evidence

1. Built CP with `cp_nsh_mcuboot` and AP with `ap_smp_mcuboot` using `-j32`.
2. Generated a development EC256-signed CP/AP pair (version 17.0.0,
   security counter 17) and independently verified the CP image with imgtool.
3. Downloaded BL1, BL2, CP A, AP A and the complete B pair through COM7.
4. Pulsed COM7 RTS and captured COM11 at 460800 baud.
5. Final capture: `/tmp/bk7258-mcuboot-real-final2/20260807-015747`.
   Result: `PASS_NSH`; the board reached the normal NSH path.

## Root cause found during bring-up

The initial `B2BAD` was a watchdog timeout during CRC-decoded image hashing
and ECC verification, not a B-slot address error. Feeding only on flash reads
was insufficient for the ECC phase; a longer BL2-only watchdog period made the
same signed A/B pair pass. Diagnostic logs and the temporary development key
were removed/restored after the test.

## Remaining gate

MCUboot confirmation must be bridged to the existing N15 format-2 journal (and
later the accepted N17 format-3/generation policy) before any OTA lifecycle
claim. The board is not N17-armed and no metadata/policy write was performed.
