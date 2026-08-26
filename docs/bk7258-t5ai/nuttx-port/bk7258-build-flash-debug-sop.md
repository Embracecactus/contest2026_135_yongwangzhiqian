# BK7258 build, package and hardware evidence SOP

Last reviewed: 2026-08-27

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
  --cp-config boards/bk7258/t5ai_core/configs/t5ai_core_cp_base \
  --ap-config boards/bk7258/t5ai_core/configs/t5ai_core_ap_base \
  --boot direct \
  --partition boards/bk7258/common/partitions/bk7258/bk7258_ab_onchip_persistent.csv \
  --jobs 8
```

Repository inputs are resolved from `bk7258.py`, independent of the caller's
working directory. Official NuttX artifacts keep their basenames under
`out/bk7258/<cp>__<ap>/<layout-id>/roles/<boot>/<role>/<build-id>/cmake`;
project BL1/BL2 and final images use the pair/layout-identity output directory.
The build ID covers the role seed, profile, pair path, layout, accepted SDK,
locked toolchain and public signing source, so changing any opaque input cannot
reuse a stale CMake cache. `--clean` deletes only that generated CMake tree.

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

## Fresh trust generation for every full download

Every owner-authorized full BK Loader download is a new trust generation,
including a switch between diagnostic and performance profiles:

1. Create two new, independent P-256 keypairs in a new mode-0700 temporary
   directory: one for BL1 and one for MCUboot.  Never reuse either private key
   from a previous full download and never use one key for both trust layers.
2. Keep private PEM files mode 0600.  Do not print them, copy them into tracked
   files or ordinary logs, or record their temporary paths.  Retain only public
   fingerprints and signed artifacts.
3. Embed the new public keys in a `--boot mcuboot --clean` build and sign with
   the matching private keys.  Version, MCUboot security counter and BL1
   counter must be explicit and strictly higher than the last accepted target.
4. Before writing, independently pass package structure, public trust chain,
   selected-layout flash contract and materialization verification.  The new
   public key is not expected to match the old target before it is installed.
5. After package, download and board acceptance, delete that generation's
   temporary private-key directory.  A later full download starts again at
   step 1, even when the firmware content is otherwise unchanged.

The apps-only path is different: it remains bound to the public trust root
already installed on the target and must not be used as a shortcut around the
fresh-generation full-download rule.

## Persistence

The selected CSV declares one storage topology: on-chip persistent, removable
block or fixed block. Ordinary build, package, boot and update preserve data;
none auto-format a medium. Provisioning/formatting is a separately named and
authorized action.

For the current T5-Board Agent layout, the authoritative CSV is
`boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv`.
Before materialization, read and validate one coherent accepted base.  Preserve
all of `usr_config [0x4fc000,0x50a000)` and Agent persistent data
`[0x561000,0x7fa000)`; preserving only the historical 1-MiB persistent window
is invalid for this layout.

Use `bk7258.py package materialize --help` for the public materialization
surface.  Its current Agent operator image is exactly one `0x7fa000`-byte file
for address `0x000000`; the write ends before `[0x7fa000,0x800000)`.

## Hardware boundary

UART, J-Link and Flash transport remain in:

- [Windows/WSL2 hardware debug](../../../tools/windows-hardware-debug/README.md)
- [Chinese SOP](../../../tools/windows-hardware-debug/SOP.zh-CN.md)
- [Agent safety rules](../../../tools/windows-hardware-debug/AI_AGENT_SOP.md)

For current full-image acceptance, use COM3 and pass only the single verified
operator image to BK Loader/bk_loader at address zero.  Do not chip erase.
`usr_config` and Agent persistent data are already materialized into that one
image; do not add parallel sparse inputs.  Never write the immutable tail,
factory calibration, OTP/eFuse, lifecycle or debug-lock state.

Record the input count, start/end address, image SHA-256 and loader success
texts.  `WriteFlash ->pass`, `Writing Flash OK` and
`All Finished Successfully` are transport evidence, not boot or application
acceptance.  A valid run also records the new generation, public fingerprints,
signed boot chain, CP/AP state and feature-specific board gates.

Compile success is not runtime acceptance, package verification is not target
trust, and Flash success is not application acceptance. Retain exact artifact
hashes plus UART/J-Link evidence for any hardware claim.
