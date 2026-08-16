# BK7258 T5 development-root rotation and Flash readback

Date: 2026-08-16

## Result

**PACKAGE_PASS / FLASH_READBACK_PASS / SWD_RTT_BOOT_PASS.**
After explicit owner authorization, the COM3 T5-Board was backed up, the first
unobservable candidate was rolled back byte-for-byte, and a policy-consistent
SWD/RTT replacement was built with the same new development keys.  The
replacement package was written as one transaction, every written region was
read back byte-for-byte, and the complete BL2 hold/release, CP, AP, CPU2 and
NSH path was observed on hardware.

The candidate could not be accepted as booted.  It used UART1/COM4 at 460800
baud and disabled SWD even though the current board policy requires P0/P1
SWD/RTT.  Two reset observations with the UART1 route selected produced zero
bytes.  With the SWD route restored, attach failed at both 1 MHz and 100 kHz,
including a manual-reset attempt; this is consistent with the candidate's
explicit SWD-disable configuration.  These facts establish an observability
failure, not proof that the CPU failed to boot.

The exact six pre-write slices were therefore restored as one operation.  A
subsequent full 8-MiB read is byte-identical to both independent pre-write
reads.  A final non-halting SWD positive control then attached successfully,
identified the expected STAR r1p0 core, observed VTOR `0x28020000`, and found
the restored BL2 hold state.  That recovery established a known-good baseline
before the policy-consistent replacement was installed.

## Accepted SWD/RTT replacement

- `firmware.bkpack` SHA-256:
  `df0f46bdf1e18d0d8e7ccbb4ea3696ddd453cbba402e44c1bf7d6c35673711a2`.
- Layout: `bk7258-v3119-ab-124ebfab37ca1fcd`, SHA-256
  `124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a`.
- CP/AP version: `18.6.81+0`; protected security counter:
  `0x12060051` on both images.
- Materialized debug contract: SWD enabled on P0/P1, target CP, BL2 boot hold
  enabled, RTT0 console and RTT1 syslog enabled.
- The three software roots remained unchanged from the first rotation:
  X/Y `0c0281291904cddd236205bc3999385757ec8e29273399300f2a3bb6641dcb37`,
  SEC1 `11cf0d61c6317616e5546f23901d0e3e8344484a334575c1acc65ee559e2ce9c`,
  and MCUboot SPKI
  `32427c8a9b1cb1be80c41b5b3e62b639d55e956f7ecdfe47621449fb9fb8455e`.

BKFIL connected on COM3, switched to 460800 baud, and reported six erase
passes, six write passes, `Enprotect pass`, `Writing Flash OK`, and
`{All Finished Successfully}`.  A single post-write readback covered every
written range; all six files were byte-identical to the package members:

| Region | Physical range | Readback SHA-256 |
|---|---:|---|
| BL1 | `[0x000000,0x011000)` | `97986dfe0b39efed8ee776990e3dc3b1fd7dd4ad9d1304f61b2b017184ce5ce2` |
| CP A | `[0x011000,0x032000)` | `da2485471c2d793b4cf08830feedc770590698a0e30c15cdb96b25f3c286cfce` |
| AP A | `[0x165000,0x175000)` | `d6ef70ae18877204f46cb349ec09c38f9a1caacabcec517e4a706006544d264c` |
| CP/AP B pair | `[0x286000,0x4fb000)` | `73c907338fd50d953d68e20a9a87576e5fd3597bf1ed06b26b4ee2504b5cbaa3` |
| BL2 primary | `[0x51d000,0x521000)` | `8587beb1f036af7721648a49e186c415981601c8da5e595576706f0f7a1bfc9e` |
| BL2 secondary | `[0x53f000,0x543000)` | `8587beb1f036af7721648a49e186c415981601c8da5e595576706f0f7a1bfc9e` |

Hardware acceptance then passed:

- J-Link identified STAR r1p0 (`CPUID=0x631f1320`) and read the three expected
  roots from the installed BL1/BL2 image.
- BL2 stopped at `VTOR=0x28020000` with the release word clear.  Writing the
  documented RAM release token launched the selected pair.
- CP switched to its runtime RAM vector table (`VTOR=0x28010800`), published a
  valid SWD trace (`SWDT`, version 2), and RTT0 produced
  `NuttShell (NSH)` followed by `nsh>`.
- AP boot state was `READY`, generation 1, error 0.  Its heartbeat advanced
  from `0x0fa9` to `0x0fbb` over two seconds.
- CPU2 state was `SECONDARY_READY`, generation 1, error 0, matching the
  bootstrap-only product contract rather than claiming scheduler-online SMP.
- AIRCR retained `PRIGROUP=0`; live system/external priority bytes were all
  aligned to the verified three-bit `0x20` step (including PendSV `0xe0`).

The board is left running this accepted SWD/RTT package.  No OTP/eFuse,
LittleFS, calibration or device-identity region was written.

## Recovery baseline

- BKFIL `2.1.11.15` read the complete physical Flash twice from COM3 at
  460800 baud.
- Both reads were exactly 8,388,608 bytes, byte-identical, and had SHA-256
  `f3009741f940f137ffd97fcadf11eb40d41c675f919a3f6216de4956f86a6b25`.
- A full pre-write image and exact rollback slices for every subsequently
  written range remain local and are intentionally not tracked because the
  image includes device-specific configuration and calibration data.
- The newly generated private keys were copied outside the repository to a
  user-private directory with directory mode `0700` and file mode `0600`.
  No private key bytes or key paths are stored in this record, package, log,
  or Git tree.

## Earlier rejected candidate contract

The candidate `firmware.bkpack` has SHA-256
`b5d429e91c51ca4e5e5ca9487267c2a83db85cf17ac159391f62adb503fc77ff`,
layout `bk7258-v3119-ab-124ebfab37ca1fcd`, and layout SHA-256
`124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a`.
Its CP/AP pair is MCUboot `18.6.81+0` with protected security counter
`0x12060051` on both members.

Host verification established:

- both embedded BL1 Manifest signatures validate and bind the primary and
  secondary BL2 copies at XIP `0x024d0000` and `0x024f0000`;
- both BL2 CRC streams decode to the authorized BL2 bytes;
- CP and AP both pass MCUboot `imgtool verify`, have equal version and security
  counter, and use MCUboot SPKI identity
  `32427c8a9b1cb1be80c41b5b3e62b639d55e956f7ecdfe47621449fb9fb8455e`;
- the secondary CP/AP pair is signed by the same new root and was included in
  the maintenance write, so rollback is not left on the prior software root.

The new BL1 software-root identities are X/Y
`0c0281291904cddd236205bc3999385757ec8e29273399300f2a3bb6641dcb37`
and SEC1
`11cf0d61c6317616e5546f23901d0e3e8344484a334575c1acc65ee559e2ce9c`.
The ordinary target preflight intentionally rejects this old-root to new-root
transition, so the operation was performed as an explicitly authorized
maintenance rotation rather than reported as a normal compatible update.

## Earlier candidate write and readback

BKFIL connected to COM3 as BK7236, switched to 460800 baud, unprotected the
Flash, and erased/wrote these six 4-KiB-aligned ranges:

| Region | Physical range | Length | Written SHA-256 |
|---|---:|---:|---|
| BL1 | `[0x000000,0x011000)` | `0x11000` | `9091cf42dbc4a33e5ef0faccd2543b353ef078427e7d52efb41e2fe700441eb6` |
| CP A | `[0x011000,0x033000)` | `0x22000` | `d89a7d1934aaea43265634f43129bb0dec1a7153ca88046a557c8b8d3b253cd3` |
| AP A | `[0x165000,0x175000)` | `0x10000` | `969128152fef56807f786d6f596531e26701924bd9ce1703278e55cb46421c4e` |
| CP/AP B pair | `[0x286000,0x4fb000)` | `0x275000` | `3fa330c0cc7dc3ed2f6000b467202cd1ff86c72745ca2d980b10668d9b377ce7` |
| BL2 primary | `[0x51d000,0x521000)` | `0x4000` | `b246b9d0c67a6517b537da5ffadb38f0bc8f7a90e6954f5a6dfe54fdb5834707` |
| BL2 secondary | `[0x53f000,0x543000)` | `0x4000` | `b246b9d0c67a6517b537da5ffadb38f0bc8f7a90e6954f5a6dfe54fdb5834707` |

The loader reported six `EraseFlash ->pass`, six `WriteFlash ->pass`,
`Enprotect pass`, `Writing Flash OK`, and
`{All Finished Successfully}`.  It then read the full 8 MiB at 460800 baud;
the post-write image SHA-256 is
`6c21d5d6e623aa712a5a6026b1dc76a3705216db503111ea8c14a7962d55ba10`.
Each written slice exactly matches the candidate.  The five complementary
gaps, including `[0x543000,0x800000)`, exactly match the pre-write image, so
`usr_config`, LittleFS, easyflash, RF/network calibration, and the remainder
of the device tail were preserved.

## Earlier rollback evidence

- The loader reported six erase passes, six write passes, Flash protection
  restored, `Writing Flash OK`, and `{All Finished Successfully}` for the
  rollback transaction.
- The post-rollback read is 8,388,608 bytes with SHA-256
  `f3009741f940f137ffd97fcadf11eb40d41c675f919a3f6216de4956f86a6b25`,
  exactly equal to both pre-write images (`cmp` success against each).
- The restored image provides the SWD positive control that the rejected
  candidate lacked.  BKFIL labels the compatible downloader family as
  `BK7236`; the board target remains BK7258/T5, and J-Link identified its STAR
  r1p0 processor core.

The subsequent accepted replacement above resolved the policy/materialization
mismatch without another root rotation.

## Security boundary

The validated chain is a development software trust chain: BL1-embedded
signed Manifests authorize BL2, and BL2 MCUboot authorizes CP/AP.  `bl_crc.bin`
itself has the Beken 32+2 CRC envelope but is not authenticated by a proven
OTP/BootROM production root.  The package container is not authenticated and
the host-reference secureboot report is not an active hardware policy.  No
OTP/eFuse or lifecycle state was read or written in this operation.
