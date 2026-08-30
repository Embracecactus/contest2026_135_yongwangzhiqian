# BK7258 build, package and hardware evidence SOP

Last reviewed: 2026-08-30

## One host entry

```bash
cd <openvela-workspace>/contest2026_135_yongwangzhiqian
tools/bk7258/bk7258.py --help
```

- `build`: official OpenVela CP/AP plus project BL1/BL2 and a verified handoff manifest;
- `sdk`: manifest-selected SDK bundle lifecycle;
- `release`: the only signed full/OTA publication and product-ZIP path;
- `package`: package inspection plus unsigned direct-boot diagnostics;
- `verify`: read-only layout, image, package and trust verification.

There is no parallel product framework, build plan, executor, board postbuild
or old command alias.

## Inputs

The team manifest pins both the SDK and OpenVela ARM prebuilt. Synchronize the
declared projects before building; the command does not fall back to
`/usr/bin` or a developer PATH compiler.

Normal development names the physical board and boot mode. The board-owned
`openvela.conf` supplies its maintained CP/AP pair, partition CSV and release-
policy CSV; the generic tool has no board-name table.  The partition file owns
geometry/build writes while the policy file maps partition names to product
update semantics without repeating offsets or capacity.  The following
example is the unsigned bring-up/diagnostic chain, not a product release:

```bash
tools/bk7258/bk7258.py build \
  --board t5ai_core --boot direct
```

Adding a physical board adds its directory, role configs and declaration; it
does not modify the build/package Python. Special xTS, performance and
drivercheck runs use the explicit `--cp-config`, `--ap-config` and
`--partition` form.

Repository inputs are resolved from `bk7258.py`, independent of the caller's
working directory. Official NuttX artifacts keep their basenames under
`out/bk7258/<board>/<cp>__<ap>/<layout-id>/roles/<boot>/<role>/<build-id>/cmake`;
project BL1/BL2 and final images use the pair/layout-identity output directory.
The build ID covers the role seed, profile, pair path, layout, accepted SDK,
locked toolchain and public signing source, so changing any opaque input cannot
reuse a stale CMake cache. `--clean` deletes only that generated CMake tree.
Every successful build prints one canonical
`out/bk7258/.../releases/<boot>/build-manifest.json`; it binds the exact raw
artifacts, ELFs, role configs, SDK profiles, locked toolchain, layout and trust
inputs. It also binds the physical-board owner and verifies that owner against
the output path. Failed builds leave no publishable manifest.

`direct` is the pre-existing `BootROM -> BL1 -> CP` diagnostic chain. It has no
BL2, signing or OTA trust and cannot be passed to `release`. The sole signed
release chain is `mcuboot`: `BootROM -> BL1 -> BL2 -> signed CP/AP`.

This software trust chain is not the TEE/HUK security architecture described by
official openvela document 1594. BK7258 does not currently claim OP-TEE,
hardware unique-key provisioning or hardware-immutable Secure Boot; those need
a separately authorized hardware security design and provisioning review.

## MCUboot build and signed package

`--boot mcuboot` additionally requires explicit BL1 and MCUboot public PEMs,
OpenSSL and a rollback floor. It generates private defconfig overlays and
public-only C sources under `out/`; no tracked key or boot-mode config exists.
The build command prints the only manifest accepted by signed release.

Signed creation never accepts hand-entered artifact, ELF, member-name, SDK or
counter lists. Use that manifest and one version whose `+GENERATION` equals the
compiled rollback floor:

```bash
tools/bk7258/bk7258.py package accept-base \
  --board "$BOARD" --base "$ACCEPTED_BASE" \
  --device-id "$DEVICE_ID" --capture-method fixture-readback \
  --output "$ACCEPTED_BASE_EVIDENCE"

tools/bk7258/bk7258.py release full \
  --build-manifest "$MANIFEST" \
  --bl1-key "$BL1_PRIVATE" --mcuboot-key "$MCUBOOT_PRIVATE" \
  --version "$VERSION" \
  --base "$ACCEPTED_BASE" --base-evidence "$ACCEPTED_BASE_EVIDENCE" \
  --openssl "$OPENSSL" --output-dir "$RELEASE_DIR"
```

`$ACCEPTED_BASE` must be one exact complete-Flash readback from the device that
will receive this recovery.  Its canonical evidence binds the hash/size to the
board, layout, stable device ID and capture method; a raw caller-provided hash
is not sufficient.  This is operator acceptance evidence rather than hardware
attestation, so the operator/fixture owns proof that the named unit produced
the readback.  The command reloads and re-hashes the build handoff, matches both private roots
to the public roots compiled into BL1/BL2, signs CP/AP with the pinned official
imgtool component, creates Manifest A/B, verifies the complete public trust
chain before publication, and atomically emits:

- `package/firmware-<BOARD>-v<VERSION>-full.bkpack`;
- `flash/operator-<BOARD>-v<VERSION>.bin`;
- `evidence/accepted-base.json`;
- `evidence/build-manifest.json`;
- `release.json` with the exact hashes, layout and write boundary.

`release ota` uses the same MCUboot build manifest and matching MCUboot key,
emits only the signed CP/AP candidate package, and rejects a generation below
the compiled floor. Official imgtool remains a single-image signing component;
the project release command owns the BK7258 multi-image/layout transaction.
After the full and optional OTA directories independently pass verification,
assemble the operator-facing artifact with:

```bash
tools/bk7258/bk7258.py release product \
  --full-release "$FULL_RELEASE_DIR" --base "$ACCEPTED_BASE" \
  --ota-release "$OTA_RELEASE_DIR" \
  --ota-required-source-version "$SOURCE_VERSION" \
  --openssl "$OPENSSL" --output "$BOARD-$VERSION.zip"
```

Omit both OTA arguments when no compatible OTA is available.  The product
command accepts different full/OTA build manifests so a wired full release may
rotate its fresh root while the OTA remains signed by the root installed on
the source devices.  `release.json` records these separately as the recovery's
`installed_root` and the OTA's `required_source_root`; they need not be equal.
Board, layout and target version must still match.

NuttX produces a board flash binary and OpenVela leaves the final multi-image
delivery format to the SoC/product; neither project defines a universal BK7258
firmware ZIP.  This repository therefore owns one versioned product contract.
`package create --unsigned` remains the low-level, sparse verification
container for one direct build and is not an operator handoff.  A complete
direct diagnostic handoff uses `package delivery`:

```bash
tools/bk7258/bk7258.py package accept-base \
  --board "$BOARD" --base "$DEVICE_BASE" \
  --device-id "$DEVICE_ID" --capture-method fixture-readback \
  --output "$DEVICE_BASE_EVIDENCE"

tools/bk7258/bk7258.py package delivery \
  --build-manifest "$DIRECT_MANIFEST" --unsigned \
  --version "$VERSION" \
  --base "$DEVICE_BASE" --base-evidence "$DEVICE_BASE_EVIDENCE" \
  --output "$DIAGNOSTIC_DELIVERY.zip"

tools/bk7258/bk7258.py verify delivery \
  --delivery "$DIAGNOSTIC_DELIVERY.zip"
```

The deterministic ZIP contains one dense `recovery/*-full-flash.bin` for BKFIL
at offset zero, the verified `.bkpack`, `release.json`, build/release-policy
and accepted-base evidence, `SHA256SUMS` and `FLASHING.md`.  Its BIN size equals
`FLASH_CAPACITY` from the selected CSV (8 MiB for the current three boards).
Firmware partitions are cleanly replaced, `reset_marker` is reset, and
configuration, persistent, device-unique and unmapped bytes come from the
exact device base.  It remains explicitly unsigned and diagnostic-only, and
must not be copied to another unit.  Never hand off the sparse container,
`pair.bin` or loose segments as the sole whole-device download.

Both unsigned diagnostics and signed releases obtain the physical board only
from their verified build manifest. The physical target is present in the
package manifest and is covered by the signed OTA/full-update catalog. The AP
accepts only `bk7258.ota/2` and compares that signed target with NuttX's
compiled physical-board identity, so boards that share a Flash layout are
still different update targets.

Public verification is read-only:

```bash
tools/bk7258/bk7258.py verify package --package firmware.bkpack
tools/bk7258/bk7258.py verify trust \
  --package firmware.bkpack --openssl /path/to/openssl
tools/bk7258/bk7258.py verify delivery \
  --delivery product.zip --openssl /path/to/openssl
```

The trust and signed-delivery commands verify both BL1 Manifests, packaged
BL1/BL2 roots and CP/AP MCUboot signatures without private keys.

## Fresh trust generation for every full download

Every owner-authorized full BK Loader download is a new trust generation,
including a switch between diagnostic and performance profiles:

1. Create two new, independent P-256 keypairs in a new mode-0700 temporary
   directory: one for BL1 and one for MCUboot.  Never reuse either private key
   from a previous full download and never use one key for both trust layers.
2. Keep private PEM files mode 0600.  Do not print them, copy them into tracked
   files or ordinary logs, or record their temporary paths.  Retain only public
   fingerprints and signed artifacts.
3. Embed the new public keys in a `--boot mcuboot --clean` build and release
   with the matching private keys. The version's `+GENERATION` is the single
   BL1/MCUboot security-counter source and must be strictly higher than the
   last accepted target.
4. Before writing, independently pass package structure, public trust chain,
   selected-layout flash contract and materialization verification.  The new
   public key is not expected to match the old target before it is installed.
5. After package, download and board acceptance, delete that generation's
   temporary private-key directory.  A later full download starts again at
   step 1, even when the firmware content is otherwise unchanged.

The OTA path is different: it remains bound to the public trust root
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

`release full` validates the complete accepted-base evidence and digest, then materializes one
complete-Flash operator image in the same atomic publication.  The live board
release policy resets only transactional state and proves all preserve,
factory-init and device-unique ranges remain byte-identical to that base.
`package materialize` remains only for read-only verification/materialization
of an already signed compatible package; it is not a signed release creation
path.  A universal factory image is not inferred from this device-bound base:
until a reviewed production provisioner assigns per-unit MAC/RF/Bluetooth and
calibration state, the product ZIP records `requires-provisioning`.

## Hardware boundary

UART, J-Link and Flash transport remain in:

- [Windows/WSL2 hardware debug](../../../../tools/windows-hardware-debug/README.md)
- [Chinese SOP](../../../../tools/windows-hardware-debug/SOP.zh-CN.md)
- [Agent safety rules](../../../../tools/windows-hardware-debug/AI_AGENT_SOP.md)

For current full-image acceptance, use COM3 and pass only the single verified
operator image to BK Loader/bk_loader at address zero.  Do not chip erase.
`usr_config` and Agent persistent data are already materialized into that one
image; do not add parallel sparse inputs.  The complete-Flash image carries
the same device's immutable/calibration tail byte-for-byte and is therefore
valid only for that unit.  Never substitute another board's tail or modify
OTP/eFuse, lifecycle or debug-lock state.

Record the input count, start/end address, image SHA-256 and loader success
texts.  `WriteFlash ->pass`, `Writing Flash OK` and
`All Finished Successfully` are transport evidence, not boot or application
acceptance. Before writing, match the package/release physical target to the
connected board. A valid run also records the new generation, public
fingerprints, signed boot chain, CP/AP state and feature-specific board gates.

Compile success is not runtime acceptance, package verification is not target
trust, and Flash success is not application acceptance. Retain exact artifact
hashes plus UART/J-Link evidence for any hardware claim.
