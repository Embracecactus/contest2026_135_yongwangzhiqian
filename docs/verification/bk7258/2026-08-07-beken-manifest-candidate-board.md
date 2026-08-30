# BK7258 Beken/Armino Manifest candidate board verification

Date: 2026-08-07

## Scope

This is a reversible, board-owned compatibility experiment.  The candidate
record follows the one-image field conventions recovered from the public
BK7236/Armino security scaffolding and is **not** claimed to be the official
BK7258 BootROM Manifest ABI.  It uses the existing 256-byte BL1 tail and does
not modify the SDK, NuttX, OTP, EFUSE, or Secure Boot lifecycle state.

## Implementation

- `boot_bl1_manifest.c` dispatches on `0xa1bc2fd8` and verifies the candidate
  header, one unencrypted EC256/SHA-256 image descriptor, BL2 address/size,
  SHA-256, SEC1 public key anchor, and raw `r||s` signature.
- The established `BKBL1M2` parser remains available for existing development
  images.
- `make_bl1_manifest.py --format beken-candidate-v1` emits a 256-byte record:
  0xd5 bytes of encoded content followed by `0xff` tail padding.
- The candidate root is the existing recoverable software development key;
  no private key is stored in the repository.

## Host evidence

The candidate was generated from the rebuilt BL2 with:

```text
BL2 logical size: 0x3000
BL2 XIP:         0x024d0000
BL2 load/entry:  0x28020000
Manifest size:   256 bytes
Manifest SHA256: 2e08d1bad9f42debe1083b621a7100f423a8067d0cb900995f78213a3f704bd6
BL2 SHA256:      4289886989ebcbe52e98d5812657f981925bea949c5b4e2903de5633c03d92f1
BL2 CRC SHA256:  5bf8d12cc2c0fc88e95ec58e87cdc62e41fb06086a0fada5d5ca7f673d4eaab2
BL1 CRC SHA256:  1fef4c389397f7b527345a111d8b81525a215d7258c2e7e4b28563d77d60cccd
```

The field assertions and an independent OpenSSL verification of the raw
signature both passed.

## Board evidence

Using BKFIL 2.1.11.15, only boot `[0x0, 0x11000)` and BL2
`[0x51d000, 0x3300)` were written.  COM7 RTS reset and COM11 at 460800 baud
produced:

```text
u_bootloader enter
partition bl2 @ 0x024D0000
BL2RAM
B2INIT
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

Capture: `/tmp/bk7258-beken-candidate-rts/20260807-160159/serial.raw`

A one-bit mutation at the candidate signature start was then flashed as a
boot-only image.  RTS reset produced the fail-closed result:

```text
u_bootloader enter
partition bl2 @ 0x024D0000
bl1 manifest rc 0x00000003
BAD
bl2 manifest
```

Capture: `/tmp/bk7258-beken-invalid-rts-retry2/20260807-160358/serial.raw`

The valid candidate boot image was restored and a final RTS reset again
reached `NuttShell (NSH)`; capture:
`/tmp/bk7258-beken-valid-rts-final/20260807-160445/serial.raw`.

## Read-only OTP root policy

The candidate verifier now reads only the v3.1.1.9 Dubhe OTP shadow fields
already confirmed by SWD:

```text
secure-boot public-key hash: 0x4b111028..0x4b111047 (32 bytes)
LCS:                         0x4b111068 (CM = 0)
```

Policy is fail-closed for provisioned devices: a non-zero OTP hash must match
the candidate Manifest key hash, and the compiled development key is not a
fallback.  The observed board is still `CM` with an all-zero hash, so it uses
the recoverable software root.  The implementation never reads the device
root key, writes OTP/EFUSE, or changes `OTP_SET`/`DIRECT_RD`.

The policy build was compiled with `BL1_OTP_ROOT_POLICY=1`, then only the boot
range `[0x000000, 0x011000)` was downloaded.  COM7 RTS reset and COM11 capture
passed:

```text
u_bootloader enter
partition bl2 @ 0x024D0000
BL2RAM
B2INIT
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

Capture: `/tmp/bk7258-otp-policy-rts/20260807-164105/serial.raw`

```text
serial.raw SHA256: 77da42a1d0a59dbed0f43697095e78e343f83dc7db059c693bb136f0bf813f65
bl_crc.bin SHA256: 4771193242ea1cc7816c28d92a0035f40ed3f9f2f659f72850b3ccaca59fcb9e
```

This proves the CM/empty-hash branch on the target.  The non-zero OTP branch
is source-reviewed but intentionally not provisioned or exercised because
OTP/EFUSE programming is irreversible.

## Full BL1 -> BL2 -> MCUboot chain regression

The current valid boot image was then exercised against the already flashed
v18.1.1 EC256-signed CP/AP pair.  A valid reset again produced
`B2INIT -> B2GOOK -> B2APOK -> B2HANDOFF -> NuttShell`.

Next, one bit in the candidate Manifest signature was flipped in the physical
32+2 image (with that packet's CRC recomputed).  BL1 stopped before copying or
entering BL2:

```text
u_bootloader enter
partition bl2 @ 0x024D0000
bl1 manifest rc 0x00000003
BAD
bl2 manifest
```

Capture: `/tmp/bk7258-fullchain-bl1-negative/20260807-164607/serial.raw`

Finally, one byte in the v18.1.1 signed CP-A payload was flipped and its
32+2 CRC packet recomputed.  The CP-A segment was downloaded at
`0x011000..0x029000`; BL2 still reached NSH, and the remap registers proved it
selected the valid B pair:

```text
0x44030058 = 0x02010000 0x02260000 0x02250000 0x00000001
```

Capture: `/tmp/bk7258-fullchain-cp-signed-negative/20260807-165206/serial.raw`

The valid v18.1.1 CP-A segment was restored.  The final capture again reached
NSH and remap returned to the A state:

```text
0x44030058 = 0x00010000 0x00100000 0x00000000 0x00000000
```

Capture: `/tmp/bk7258-fullchain-cp-restore/20260807-165307/serial.raw`

## Conclusion

The candidate parser is now proven on BK7258 as a board-owned BL1 format with
positive and negative behavior, and its read-only OTP-root policy passes the
current CM development state.  The complete current chain also proves BL1
Manifest rejection, BL2 MCUboot CP-A signature rejection, B-pair remap, and
recovery to A.  This does not prove that BK7258 BootROM or an official Beken
BL1 uses these bytes.  The non-zero OTP branch remains unprovisioned by
policy; irreversible provisioning remains forbidden.
