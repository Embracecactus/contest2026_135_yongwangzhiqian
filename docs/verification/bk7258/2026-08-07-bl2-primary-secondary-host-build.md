# BK7258 primary/secondary BL2 fallback host build

Date: 2026-08-07

## Scope

This checkpoint covers the first implementation of the board-owned
primary/secondary BL2 fallback. It does not claim the unpublished BK7258
BootROM Manifest ABI and does not enable OTP/eFuse Secure Boot state.

## Build

```text
MCUBOOT_SIGNING_KEY=/tmp/bk7258-mcuboot-current-dev.pem
MCUBOOT_VERSION=18.1.1
BL1_MANIFEST_KEY=/tmp/bk7258-bl1-manifest-dev-key.pem
CP_CONFIG_NAME=cp_nsh_mcuboot
AP_CONFIG_NAME=ap_smp_mcuboot
BL2_LOGICAL_SIZE=0x3000
bash board/bk7258/scripts/build_dual_image.sh
```

The build completed with the official checksum-verified v3.1.1.9 SDK bundle,
the pinned NuttX MCUboot `imgtool.py`, and no SDK/NuttX source modification.

## Fixed addresses

| item | raw physical | logical XIP |
| --- | ---: | ---: |
| primary BL2 | `0x51d000` | `0x024d0000` |
| secondary BL2 | `0x53f000` | `0x024f0000` |
| secondary BL2 end | `0x561000` | `0x02510000` |
| secondary Manifest | boot tail | `0x0200fe00` |
| primary Manifest | boot tail | `0x0200ff00` |

Each BL2 envelope reserves 128 KiB logical / 136 KiB physical. The current
active image is 0x3000 logical bytes and is erase-sector padded in the package.

## Artifact hashes

```text
bl_crc.bin:              2c7665ecd46f3f39b1e6a59745ccf92787f4afade28da326f354be9000c1124b
bl1-manifest-primary:    1f0df1359df8818867b971f7c77197d4c06f545f27be39d375aff9211243bcd1
bl1-manifest-secondary:  88614890ce5809ecbea87a1bb75817260f85e086095aff682152cdbb768630fd
bl2_crc.bin:             f0fa26d271cc9ebb34ac202afffdde7c7b34dd272fba1498a7c2f5a595773a23
bl2_secondary_crc.bin:   f0fa26d271cc9ebb34ac202afffdde7c7b34dd272fba1498a7c2f5a595773a23
```

## Status

- Host compilation: PASS.
- Package manifest/factory-layout checks: PASS.
- Board normal-A boot: PASS; see
  [the normal-A board capture](2026-08-07-bl2-primary-secondary-board.md).
- Primary-corruption → secondary recovery: PASS; see
  [the fallback and restore capture](2026-08-07-bl2-primary-secondary-fallback-board.md).
