# Generic IPSS Manifest shape and BK7258 board-owned raw-length test

Date: 2026-08-07

## Provenance boundary

The v3.1.1.9 BK7258 SDK contains `genbl1.py`/`bl1_sign.py`, but does not ship
the referenced `secure_boot_tool`.  A locally available generic IPSS tool was
run only as a forensic reference; it is not added to this repository and is
not a runtime dependency.  Its SHA-256 is:

```text
f93f2c7f83090c65da5d3e498e3a14152c0201b956e7334e75d270723e5cc119
```

This establishes the generic Armino/IPSS record shape, not the unpublished
BK7258 BootROM ABI.  The project still builds runtime artifacts only from
the pinned official v3.1.1.9 SDK plus repository-owned code.

## Record recovered from the generic tool

The tool emits 213 bytes (`0xd5`) for one unencrypted ECDSA-256/SHA-256
image.  The stable field map observed in multiple existing samples is:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| `0x00` | 4 | magic `0xa1bc2fd8` (little endian bytes `d8 2f bc a1`) |
| `0x04` | 4 | layout version `0x00010001` |
| `0x08` | 4 | Manifest version/security counter (`5` in the samples) |
| `0x0c` | 4 | encoded record size `0xd5` |
| `0x10` | 4 | algorithm flags `0x00030619` |
| `0x14` | 4 | image count (`1`) |
| `0x18` | 4 | image flags (`0`) |
| `0x1c` | 4 | image version/reserved (`0`) |
| `0x20` | 4 | image static/XIP address |
| `0x24` | 4 | image load address |
| `0x28` | 4 | raw image byte length |
| `0x2c` | 4 | image entry address |
| `0x30` | 32 | SHA-256 of the raw image bytes |
| `0x50` | 4 | reserved zero |
| `0x54` | 65 | SEC1 uncompressed public key (`0x04 || X || Y`) |
| `0x95` | 64 | raw ECDSA signature (`r || s`) over bytes `0x00..0x94` |

The tool's output is exactly 213 bytes; the board packer places it in a
256-byte erased tail so CRC expansion and the existing boot partition remain
unchanged.

For the current board-owned BL2, a generic-tool-equivalent record has:

```text
static/XIP = 0x024d0000
load       = 0x28020000
entry      = 0x28020000
raw size   = 0x24ec (9452 bytes)
raw SHA256 = 4289886989ebcbe52e98d5812657f981925bea949c5b4e2903de5633c03d92f1
```

## Implementation change

`make_bl1_manifest.py --format beken-candidate-v1` now records and hashes the
raw BL2 length, while the BL1 copy window remains the separately supplied
`BL2_LOGICAL_SIZE` (`0x3000` in this test).  The verifier accepts a descriptor
length no larger than that window and requires the remaining XIP bytes to be
`0xff`.  The previous padded-length record remains accepted for recoverable
development images when its digest covers the full copy window.

This is still a board-owned compatibility parser.  It does not read OTP
roots, enable Secure Boot, or claim BootROM acceptance.

## Board results

The new raw-length candidate was packed with the existing CRC32+2 boot image
packer and flashed only to the boot partition at physical `0x0`:

```text
Manifest SHA256: 365cc0fc32a368c3c826788beb352f3cb42d3d964b557705aa3cdc192dc839f6
BL1 CRC image:  28155e5f29dad7acba75340e8a480689d15fa0194d198e6a1842e485b66c7be5
BL2 logical:    4289886989ebcbe52e98d5812657f981925bea949c5b4e2903de5633c03d92f1
```

Positive COM7/BKFIL + COM11 capture (`serial.raw` SHA-256
`f4f6dd3fe6882d55eea2da82a5914e46801fe04ce167e3efb7d74d43e589589f`):

```text
u_bootloader enter
partition bl2 @ 0x024D0000
bl2 ram @ 0x28020000
BL2RAM
B2INIT
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

The negative test flipped one signature bit at offset `0x95`, repacked only
the boot partition, and produced no `BL2RAM`; the capture SHA-256 is
`e609715df9542cdce4fbd07ab8290e3e37b90eee57da964c05765c775e4a2d76`:

```text
u_bootloader enter
partition bl2 @ 0x024D0000
bl1 manifest rc 0x00000003
BAD
bl2 manifest
```

The valid raw-length boot was then flashed again.  The restoration capture
has the same positive trace and SHA-256 as the first positive capture.  The
board is therefore left on the valid candidate and no OTP/EFUSE state was
changed.

## Limit

The record shape is now independently reproduced from the generic IPSS
tool and consumed by our board-owned BL1, but the exact BK7258 BootROM
consumer, OTP key-selection behavior, encryption fields and lifecycle
counter semantics remain unproven.  Those require a BK7258 Secure Boot
binary/sample or a reversible vendor confirmation; the current board is in
life-cycle `CM` with zero Secure-Boot key-hash/lock shadows.

## 2026-08-07 deep reverse checkpoint

The reference tool was imported into Ghidra with its DWARF information.  It
is the unmodified `release/v2.0.1` BK7236 `bk_idk` artifact, not a BK7258
runtime input:

```text
path:   /home/lijian/project/armino/bk_idk/tools/env_tools/beken_utils/tools/sh_sec_tools/secure_boot_tool
sha256: f93f2c7f83090c65da5d3e498e3a14152c0201b956e7334e75d270723e5cc119
git:    bk_idk release/v2.0.1 @ 650e754e
```

The DWARF/source names and decompiled writers provide stronger evidence than
the JSON scripts alone.  The binary contains `parse_mnft_cfg.c` and
`gen_mnft_bin.c` routines named `get_basic_info`, `get_img_info`,
`calculate_manifest_size`, `write_manifest_file`, `write_cipher_info`,
`write_extended_program`, and `do_manifest_sign`.

The writer is byte-stream based and has no hidden per-field padding:

```text
manifest_basic_t (24 bytes)
  image_desc_t[image_count] (24 bytes each, digest pointer is not serialized)
  image_digest[ digest_size ] for each image
  optional extended-program bytes (raw file contents)
  cipher info: blob_size (4), optional IV (16), optional key blob
  public key bytes
  signature bytes
```

For one unencrypted EC256/SHA256 image, the reference tool calculates:

```text
24 + 24 + 32 + 4 + 65 + 64 = 213 (0xd5) bytes
```

The generated temporary sample was independently checked against the source
layout:

```text
header magic       d8 2f bc a1       (0xa1bc2fd8, little endian)
layout/content     01 00 01 00 / 05 00 00 00
encoded size       d5 00 00 00
flags              19 06 03 00       (0x00030619)
image count        01 00 00 00
descriptor         version=0, static=0x02024000, load=0x28040000,
                   size=64, entry=0x28040000
image digest       SHA256(bytes 0..63)
cipher info        00 00 00 00
public key         0x04 || X || Y (65 bytes)
signature          raw r || s (64 bytes)
```

The signed region is exactly `manifest[0:0x95]`; the reference tool's
`bl1_manifest_digest.txt` equals `SHA256(manifest[0:0x95])`.  The board
packer produces the same first `0xd5` bytes (the ECDSA signature is expected
to differ because signing is randomized) and then fills the containing
256-byte flash record with `0xff`.

The recovered enum values also explain `0x00030619`: SHA-256 is hash enum 3,
ECDSA-256/SHA-256 is manifest signature enum 6, and the public-key hash is
SHA-256 enum 3; the remaining low flag bits indicate secure boot enabled and
no extension/cipher-key blob for this sample.  RSA/EC521 signature lengths
are selected by the same writer (`0x80`, `0x100`, `0x40`, `0x84`), while
encrypted-image fields add an IV and/or key blob according to the cipher
scheme.

This is a recovered **generic BK7236/IPSS record**, not a claim about the
BK7258 BootROM consumer.  The later
[`2026-08-08` BootROM reverse](2026-08-08-bk7236-bootrom-deep-reverse.md)
independently confirms this record from the BK7236 consumer side and closes
its lifecycle and encrypted-image ordering.  Neither result proves BK7258
BootROM acceptance, authorizes writing OTP/eFuse, or enables a security
lifecycle state.

### Signature/public-key encoding recovered from the helpers

The remaining key helpers were also decompiled from the same DWARF-bearing
tool:

| Signature selector | Public-key bytes in the record | Signature bytes in the record |
|---:|---:|---:|
| 1/2 (RSA-1024) | 128-byte RSA modulus | 128 bytes |
| 3/4 (RSA-2048) | 256-byte RSA modulus | 256 bytes |
| 5/6 (ECDSA-P256) | 65-byte SEC1 point (`0x04 || X || Y`) | 64 bytes (`r || s`) |
| 7/8 (ECDSA-P521) | 133-byte SEC1 point (`0x04 || X || Y`) | 132 bytes (`r || s`) |

The RSA helper serializes the modulus in big-endian `BN_bn2bin` form.  The
ECDSA helper serializes the OpenSSL point using the key's point-conversion
form (the generated P-256 sample is the uncompressed form) and pads each
signature integer to the curve width.  RSA PKCS v1.5 versus PSS is carried by
the separate four-bit `rsa_pkcs_version` field; it does not alter the public
key/signature byte widths above.

An encrypted-image host-only sample additionally confirmed the writer's
order: after each image digest it emits a four-byte key-blob length, then the
optional IV for CBC/CTR schemes, then the key blob, then the public key and
signature.  For AES-ECB-256 the flags became `0x0303061d` and a 32-byte key
blob followed the length.  The project does not enable this path on the board:
there is no BK7258 TrustEngine/BootROM consumer proof, and the current
board-owned verifier intentionally accepts only the unencrypted EC256 case.
