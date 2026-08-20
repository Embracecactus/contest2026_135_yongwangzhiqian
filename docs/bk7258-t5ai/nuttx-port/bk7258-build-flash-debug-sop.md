# BK7258 build, package and hardware evidence SOP

Last reviewed: 2026-08-20

## One host entry

```bash
cd <openvela-workspace>/contest2026_135_yongwangzhiqian
tools/bk7258/bk7258.py --help
```

- `build`: official OpenVela CP/AP plus project BL1/BL2;
- `sdk`: manifest-selected SDK bundle lifecycle;
- `package`: deterministic release container;
- `verify`: read-only layout, image, package and trust verification.

There is no product, framework, build plan, executor, board postbuild or old
command alias.

## Inputs

The team manifest pins both the SDK and OpenVela ARM prebuilt. Synchronize the
declared projects before building; the command does not fall back to
`/usr/bin` or a developer PATH compiler.

Every build requires CP config, AP config, boot mode, partition CSV and jobs:

```bash
tools/bk7258/bk7258.py build \
  --cp-config board/bk7258/configs/t5ai_core_cp_base \
  --ap-config board/bk7258/configs/t5ai_core_ap_base \
  --boot direct \
  --partition board/bk7258/partitions/bk7258/bk7258_ab_onchip_persistent.csv \
  --jobs 8
```

Repository inputs are resolved from `bk7258.py`, independent of the caller's
working directory. Official NuttX artifacts keep their basenames under
`out/bk7258/<config>-<boot>/cmake`; project BL1/BL2 and final images use the
pair/layout-identity output directory.

## MCUboot build and signed package

`--boot mcuboot` additionally requires explicit BL1 and MCUboot public PEMs,
OpenSSL and a rollback floor. It generates private defconfig overlays and
public-only C sources under `out/`; no tracked key or boot-mode config exists.

Signed package creation requires:

- raw project BL1, CP, AP and project BL2 artifacts from that build;
- matching BL1 and MCUboot private PEMs;
- matching BL1/BL2 ELFs;
- explicit MCUboot version, MCUboot security counter and BL1 counter;
- explicit member basenames for all final artifacts.

Use `bk7258.py package create --help` for the complete argument surface. The
release stage signs CP/AP independently with pinned imgtool, creates Manifest
A/B, verifies private keys against compiled roots, finalizes CRC/pair bytes,
then passes those immutable bytes to `package.py`.

Public verification is read-only:

```bash
tools/bk7258/bk7258.py verify package --package firmware.bkpack
tools/bk7258/bk7258.py verify trust \
  --package firmware.bkpack --openssl /path/to/openssl
```

The second command verifies both BL1 Manifests, packaged BL1/BL2 roots and CP/AP
MCUboot signatures without private keys.

## Persistence

The selected CSV declares one storage topology: on-chip persistent, removable
block or fixed block. Ordinary build, package, boot and update preserve data;
none auto-format a medium. Provisioning/formatting is a separately named and
authorized action.

## Hardware boundary

UART, J-Link and Flash transport remain in:

- [Windows/WSL2 hardware debug](../../../tools/windows-hardware-debug/README.md)
- [Chinese SOP](../../../tools/windows-hardware-debug/SOP.zh-CN.md)
- [Agent safety rules](../../../tools/windows-hardware-debug/AI_AGENT_SOP.md)

Only package-declared sparse ranges may be considered for an authorized
download. Chip erase, persistent/calibration destruction, OTP/eFuse,
lifecycle changes and debug locking always require separate irreversible-action
authority.

Compile success is not runtime acceptance, package verification is not target
trust, and Flash success is not application acceptance. Retain exact artifact
hashes plus UART/J-Link evidence for any hardware claim.
