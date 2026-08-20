# BK7258 single-CLI real-build checkpoint

Date: 2026-08-20

Scope: host source refactor, official SDK rebuild, OpenVela CP/AP build and
unsigned package creation. No private key, Flash write or hardware mutation.

## Source authority

- Team manifest project: `vendor/beken/bk_avdk_smp`
- Revision: `cb080de1655d579c7593ecf504c440997c4c137b`
- Branch: `openvela/v3.1.1.9`
- Checkout status: clean
- Local object verification: `missing_objects=0`; `git fsck --full` passed

## SDK rebuild

All profiles were built through the official `make bk7258_cp|bk7258_ap`
targets in temporary clean checkouts, then verified by deterministic tree hash:

| Profile | Files | Tree SHA256 |
|---|---:|---|
| cp | 384 | `a7294935c10872b7f8da883443c3e024c82269676130ee0a4e73269b6c1433f3` |
| ap | 692 | `12206ecee041f16969fe62461e21aef1c6f25ad0c14e968342ffca7470de747d` |
| ap-sdio4 | 692 | `ed3e6c3b49add274d48b53c4127c2fa942538ba1bfc7a8b79d51a7cbff649438` |

The exported closure comes from the official `app.elf` link command. Profile
comments, rather than Python/Make/CMake, declare NuttX-owned runtime omissions.
UART was rebuilt with `CONFIG_BK_PRINTF_DISABLE`; official CRC comparison
against four byte vectors passed.

## Firmware build

Command contract:

```text
bk7258.py build \
  --cp-config board/bk7258/configs/t5ai_core_cp_base \
  --ap-config board/bk7258/configs/t5ai_core_ap_base \
  --partition board/bk7258/partitions/bk7258/auto_partitions.csv \
  --jobs 8 --clean
```

Result: PASS through OpenVela's official CMake build entry.

| Artifact | Bytes | SHA256 |
|---|---:|---|
| CP `nuttx.bin` | 168096 | `7cbce610469df83436e54c3b99bebd8395e356dfed1dbde4cf2b1806ba8c0159` |
| AP `nuttx.bin` | 95216 | `aa463cee81475028dc8207d2e0d8bc5899776cf0be4d458ee386c601e4cc1d5a` |

Selected layout: `bk7258-d4d6db74c7a826b7`.

## Package

- Image verification: PASS, four writes plus one explicit erase range.
- Unsigned `.bkpack` create: PASS.
- Independent package verification: PASS.
- Package SHA256:
  `090382e68f29eee04c60034681ba1ba793d163d0562bdd086a53356366dd3718`.

The package is explicitly `unsigned`; it is not a signed-delivery or hardware
acceptance result.

## Open gates

- Signed profile and BL1/BL2 public anchors
- Explicit matching BL1 and MCUboot private-key paths
- Signed package verification
- Recoverable hardware validation
- Final repository residual/reference and memory checks
