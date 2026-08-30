# MCUboot build/download chain board verification

Date: 2026-08-07

## Scope

This run verifies the build path and the reversible BL1 -> BL2 -> MCUboot
handoff.  It uses only the repository's board code, the pinned NuttX MCUboot
`imgtool.py`, and the checksum-verified official SDK v3.1.1.9 bundle.  The
private signing keys stayed outside the repository in `/tmp`.  No OTP, EFUSE,
Secure-Boot lifecycle bit, or LittleFS byte was programmed.

## Build

The explicit profile was built with 32 host jobs:

```text
CP_CONFIG_NAME=cp_nsh_mcuboot
AP_CONFIG_NAME=ap_smp_mcuboot
MCUBOOT_VERSION=18.1.1
MCUBOOT_SECURITY_COUNTER=18
BL2_LOGICAL_SIZE=0x3000
```

The builder now fails closed unless the caller supplies external
`MCUBOOT_SIGNING_KEY` and `BL1_MANIFEST_KEY`.  It performs these steps in
order:

1. Build the board-owned SRAM BL2 and its 32+2 CRC image.
2. Generate the 256-byte candidate BL1 Manifest for the exact BL2 bytes.
3. Link and CRC-pack BL1 with Manifest enforcement enabled.
4. Build CP and AP from the normal configs.
5. Sign each raw CP/AP payload with the pinned NuttX MCUboot `imgtool.py`.
6. Apply the official v3.1.1.9 32-byte-data + 2-byte-CRC expansion and build
   the contiguous A/B package.

The output is `/home/lijian/project/open-vela/nuttx/bk7258-dual`.  Important
artifact sizes and SHA-256 values from this run are:

```text
bl_crc.bin         69632 bytes  954b87b3fe302b2e38b191385b4ce72aaa4505a3545d4b132ba1a1f13fb0edac
bl1-manifest.bin     256 bytes  (included in the BL1 tail)
bl2.bin              9452 bytes  (0x3000 logical copy window)
bl2_crc.bin         13056 bytes  5bf8d12cc2c0fc88e95ec58e87cdc62e41fb06086a0fada5d5ca7f673d4eaab2
app_crc_flash.bin  167936 bytes  74c3bdfc5799de2929eecdc7b0fe129b72849867866a3a1472d5cd59ea5aeaaf
app1_crc_flash.bin  65536 bytes  27e6821710898f64224ec05aa311c2204c41a354f12b0bd0318016d2b95481bb
```

The CP/AP logical signed images begin with MCUboot magic `0x96f3b83d`; the
builder no longer passes raw vectors directly to the MCUboot profile's flash
package.  The final host checks passed, including the factory-layout check.

## Board download

The WSL2 SOP detected the MCUboot profile and sent these bounded COM7 ranges
through BKFIL 2.1.11.15:

```text
bl_crc.bin         @ 0x000000, length 0x11000
bl2_crc.bin        @ 0x51d000, length 0x03300
app_crc_flash.bin  @ 0x011000, length 0x29000
app1_crc_flash.bin @ 0x165000, length 0x10000
```

The package verifier passed before the loader started.  BKFIL reported
`WriteFlash ->pass` for all four ranges and ended with
`Writing Flash OK` / `{All Finished Successfully}`.  The loader returned a
non-zero process status after its success banner; the SOP normalized that
known BKFIL behavior to success only because both explicit success markers
were present.

Capture directory:

```text
/tmp/bk7258-mcuboot-build-flash/20260807-171000/
```

The captured trace was:

```text
u_bootloader enter
partition bl2 @ 0x024D0000
bl2 ram @ 0x28020000
BL2RAM
B2INIT
B2GO
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

This proves that the newly generated BL1 Manifest was accepted, BL1 copied
the newly generated BL2, BL2 verified/started the newly signed CP/AP pair, and
the CP handoff reached NSH.  It does not prove that the candidate Manifest is
the undocumented BK7258 BootROM ABI; it is the reversible board-owned path
currently used while Beken's official BK7258 secure-boot adaptation remains
unavailable.

A second, non-writing COM7 RTS reset was run against the same package.  Its
COM11 capture was `/tmp/bk7258-mcuboot-build-rts/20260807-171201/serial.raw`
with SHA-256
`77da42a1d0a59dbed0f43697095e78e343f83dc7db059c693bb136f0bf813f65` and
reported `cold_path=yes`, `bl2_handoff=yes`, and `nsh=yes`.  The trace included
the cold clock marker followed by `B2INIT -> B2GOOK -> B2APOK -> B2HANDOFF`.

## Recovery boundaries

- The sparse download preserved LittleFS and the vendor-reserved ranges.
- `--boot-only` remains available when only BL1 needs recovery; it deliberately
  does not update BL2 or CP/AP.
- OTP/EFUSE writes and secure lifecycle transitions remain forbidden.
- The generated package is not a production key-management solution; the
  external development keys must be replaced by a reviewed release process
  before any product use.
