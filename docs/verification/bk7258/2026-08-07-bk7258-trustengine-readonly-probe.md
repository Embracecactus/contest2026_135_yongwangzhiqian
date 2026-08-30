# BK7258 TrustEngine read-only probe

Date: 2026-08-07

This is a reversible hardware probe made with J-Link SWD.  It performs only
`halt`, `mem32` reads, `regs`, `go`, and `exit`.  It does not write Flash,
OTP, EFUSE, TrustEngine control registers, or lifecycle state.

## Address evidence

The official v3.1.1.9 BK7258 register definitions identify the Dubhe/Shanhai
base as `0x4b110000` when the secure address offset is zero.  The board read
returned valid values at this base and at the documented top-status and OTP
manager offsets:

| Address | Meaning from the v3.1.1.9 headers | Read value |
| --- | --- | ---: |
| `0x4b110000` | `DBH_CLK_CTRL` | `0x0000003f` |
| `0x4b110004` | `DBH_RESET_CTRL` | `0x00000000` |
| `0x4b110100` | `DBH_VER` | `0x0000db31` |
| `0x4b110104` | `DBH_CFG1` | `0x92360000` |
| `0x4b110108` | `DBH_CFG2` | `0x00400080` |
| `0x4b11010c` | `DBH_CFG3` | `0x00000000` |
| `0x4b110400` | `OTP_SET` | `0x00000001` |
| `0x4b110410` | `OTP_UPDATE_STAT` | `0x00000004` |

The decoded fields are consistent with an instantiated OTP block: `CFG1`
has `OTP_EXIST=1`, `CFG2` reports a 128-bit secure word and a 64-bit
non-secure word, `OTP_SET.INIT_DONE=1`, and
`OTP_UPDATE_STAT.PUF_READY=1` according to the v3.1.1.9 Dubhe definitions.
`CLK_CTRL=0x3f` has the documented hash/SCA/ACA/OTP/TRNG/DMA clock bits set.

The same J-Link session read the OTP shadow window at
`0x4b111000` (`DBH_BASE_OTP_SPACE`).  The window was readable and contained
both zero and non-zero shadow words.  Secret-looking words are deliberately
not copied into this repository; this record only establishes address
accessibility and does not interpret or export device/root-key material.
`OTP_SET` had `DIRECT_RD=0`, so this observation was in the SDK-defined
shadow-read mode and was not a direct OTP-array read.

A second read-only session inspected only non-secret security fields:

| Address | SDK field | Read value |
| --- | --- | ---: |
| `0x4b111014` | Device ID shadow | `0x00000000` |
| `0x4b111028..0x4b111047` | Secure-Boot public-key hash shadow | all zero |
| `0x4b111048..0x4b111067` | Secure-debug public-key hash shadow | all zero |
| `0x4b111068` | Life-cycle (`DBH_OTP_LCS_OFFSET`) | `0x00000000` |
| `0x4b11107c` | Lock-control shadow | `0x00000000` |

Under the v3.1.1.9 enum, life-cycle value zero is `DBH_DEV_LCS_CM`; the
zero key hashes and zero lock-control shadow are consistent with an
unprovisioned, recoverable development part.  This is evidence about the
current sample only, not a guarantee about every BK7258 production lot.
The probe intentionally did not read the device-root-key shadow words and
did not change `OTP_SET.DIRECT_RD`.

## BL1 runtime probe

To distinguish SWD visibility from the BL1 execution context, the board
bootloader was rebuilt with the board-owned, default-off
`BL1_TRUSTENGINE_PROBE=1` flag.  The probe reads only the same documented
registers and prints them through the existing UART path; it never writes a
TrustEngine/OTP register.  The probe boot image SHA-256 was
`a96622d9beacb8adf87544b2d2904742ebf75e0675914d3551a0ab0c49ffeca3`.

COM7/BKFIL boot-only download followed by COM11 capture produced:

```text
u_bootloader enter
DUBHE VER 0x0000DB31
DUBHE CFG1 0x92360000
DUBHE CFG2 0x00400080
OTP SET 0x00000001
OTP STAT 0x00000004
OTP LCS 0x00000000
OTP LOCK 0x00000000
partition bl2 @ 0x024D0000
BL2RAM
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

The raw capture SHA-256 is
`c3a644464125de1de993395ae864c691c870dac44ad4279ff2e12c9f1ec5812e`.
This proves the addresses are readable in the actual BL1 reset path and that
the reads do not prevent the existing BL1→BL2→NSH chain from starting.

The board was then rebuilt without the probe (`BL1_TRUSTENGINE_PROBE=0`),
flashed with the valid raw-length candidate, and reset through the verified
COM7 RTS path.  The final capture reached `NuttShell (NSH)` with SHA-256
`77da42a1d0a59dbed0f43697095e78e343f83dc7db059c693bb136f0bf813f65`.

## Negative address probe

An earlier read of BK7271-style candidate addresses `0x08802740` and
`0x08806000` returned `Could not read memory` on the same BK7258 target.
This rejects those addresses as a BK7258 TrustEngine map candidate; it does
not by itself identify every BK7258 alias.

## What this proves and what it does not

This is the first direct BK7258 hardware evidence for the v3.1.1.9 Dubhe
register base, top status, OTP manager and shadow-window address relationship.
It supports using those addresses for a future board-owned *read-only*
inspection path.  It does **not** prove the BK7258 BootROM Manifest format,
the BootROM's key-selection policy, the TrustEngine signing ABI, or that
Secure Boot is enabled.  No OTP/EFUSE provisioning is performed in the
recoverable development path.
