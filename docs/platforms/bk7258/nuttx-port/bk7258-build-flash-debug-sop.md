# BK7258 build, package and hardware evidence SOP

Last reviewed: 2026-09-13 (instruction scope and workflow routing only)

## One host entry

```bash
cd <contest-repository>
tools/bk7258/bk7258.py --help
```

- `build`: official OpenVela CP/AP plus project BL1/BL2 and a verified handoff manifest;
- `toolchain`: install or verify the manifest-locked Arm GNU toolchain;
- `sdk`: manifest-selected SDK bundle lifecycle;
- `release`: the only signed full/OTA publication and product-ZIP path;
- `package`: package inspection plus unsigned direct-boot diagnostics;
- `deploy`: inspect or deliver one verified signed OTA package to a target;
- `verify`: read-only layout, image, package and trust verification.

There is no parallel product framework, build plan, executor, board postbuild
or old command alias.

## Inputs

The team manifest pins both the SDK and OpenVela ARM prebuilt. Synchronize any
missing projects required by the selected build; the command does not fall back to
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

`--boot mcuboot` additionally requires the approved BL1 and MCUboot public
PEMs, OpenSSL and a rollback floor. A8 uses a sustained trusted signing
identity: its public fingerprints and approved signer reference are release
evidence, while private signing material remains at an approved secure local
PEM path. The current release interface accepts private PEM paths; a secret
manager or HSM is only a possible authorized storage arrangement, not a
verified signing-backend adapter in this workflow. The build consumes only
public keys, generates
private defconfig overlays and public-only C sources under `out/`, and never
records a private-key path or value. The build command prints the only manifest
accepted by signed release.

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
install a separately authorized replacement root while the OTA remains signed by the root installed on
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
and accepted-base evidence, `SHA256SUMS` and `FLASHING.md`.  Its BIN size equals `FLASH_CAPACITY` from the selected CSV.
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

## A8 sustained trust identity and signing boundary

An owner-authorized full BK Loader download does not create a new trust
generation by itself. A8 maintains two independent trust layers, BL1 and
MCUboot, under sustained identities identified by their public fingerprints and
approved signer references. A normal release reuses their public keys and the
approved private PEM signing input when boot components, configuration, toolchain,
protected metadata and dependencies remain compatible.

The build/sign boundary is deliberate:

1. Build with the approved public PEMs only. The build manifest records public
   trust inputs and is the sole input accepted by release creation.
2. The current release CLI signs from approved private PEM paths. A secret
   manager or HSM may be an authorized storage arrangement before that input is
   supplied, but no adapter integration is claimed here. Private key values and
   paths must not be placed in the repository, `out/`, ordinary logs, package
   evidence or operator handoff.
3. Record the signer reference and public fingerprints in release evidence;
   public verification validates the resulting chain without private keys.
4. Creation, import, rotation, revocation, migration or destruction of either
   identity is a separately owner-authorized identity operation. Neither a
   full download, diagnostic/performance switch, `--clean`, nor a source rebuild
   grants that authorization.

An authorized identity transition embeds the replacement public key in the
affected boot chain and follows the package, rollback, selected-layout and
materialization checks before writing. The OTA path remains bound to the public
root installed on its source target. Its eligibility, and the recovery method
for an identity transition, are separate decisions pending their own validation.

## Persistence

The selected CSV declares one storage topology: on-chip persistent, removable
block or fixed block. Ordinary build, package, boot and update preserve data;
none auto-format a medium. Provisioning/formatting is a separately named and
authorized action.

The selected board's `openvela.conf`, partition CSV and release-policy CSV own
the exact storage ranges and semantics. Before materialization, read and
validate one coherent accepted base; derive every preserve, factory-init,
device-unique and transactional range from those selected inputs. Do not copy
the historical 1-MiB window or any address from an older layout into the
operator procedure.

`release full` validates the complete accepted-base evidence and digest, then materializes one
complete-Flash operator image in the same atomic publication.  The live board
release policy resets only transactional state and proves all preserve,
factory-init and device-unique ranges remain byte-identical to that base.
`package materialize` remains only for read-only verification/materialization
of an already signed compatible package; it is not a signed release creation
path.  A universal factory image is not inferred from this device-bound base:
until a reviewed production provisioner assigns per-unit MAC/RF/Bluetooth and
calibration state, the product ZIP records `requires-provisioning`. RF data
origin, station ownership, Beken calibration/test tool roles and the production
acceptance gates are defined by the maintained
[RF calibration and factory-provisioning contract](../rf-calibration-and-factory-provisioning.md).

## Hardware boundary

UART, J-Link and Flash transport remain in:

- [Windows/WSL2 hardware debug](../../../../tools/windows-hardware-debug/README.md)
- [Chinese SOP](../../../../tools/windows-hardware-debug/SOP.zh-CN.md)
- [Agent safety rules](../../../../tools/windows-hardware-debug/AI_AGENT_SOP.md)

For full-image acceptance, use the fixture-selected loader/control port and pass only
the single verified operator image to BK Loader/bk_loader at address zero.  Do not chip erase.
`usr_config` and Agent persistent data are already materialized into that one
image; do not add parallel sparse inputs.  The complete-Flash image carries
the same device's immutable/calibration tail byte-for-byte and is therefore
valid only for that unit.  Never substitute another board's tail or modify
OTP/eFuse, lifecycle or debug-lock state.

Record the selected port, input count, start/end address, image SHA-256 and loader
success texts. The host COM number is fixture state, not a board identity.  `WriteFlash ->pass`, `Writing Flash OK` and
`All Finished Successfully` are transport evidence, not boot or application
acceptance. Before writing, match the package/release physical target to the
connected board. A valid run also records the new generation, public
fingerprints, signed boot chain, CP/AP state and feature-specific board gates.

Compile success is not runtime acceptance, package verification is not target
trust, and Flash success is not application acceptance. Retain exact artifact
hashes plus UART/J-Link evidence for any hardware claim.

## Handoff gates by stage

Read only the stage relevant to the requested delivery. Passing a hardware-fast
observation completes that iteration and does not itself request final source
acceptance, a multi-board campaign, or a product release. Continue remaining
work already covered by the task; apply final gates only when that delivery is
in scope.

- **Hardware-fast debug iteration:** follow the repository's hardware-fast
  loop, build only the affected target and hand off the exact debug artifact
  with minimum integrity checks. Defer broad regression and release assembly.
  A whole-device BIN still requires accepted-base, approved signing identity,
  signature, rollback and exact-Flash-size checks; prefer the permitted installed
  apps-only contract where applicable.
- **Final source acceptance:** run `git diff --check`, the host BK7258
  regression and header audit, validate manifest and local documentation
  links, confirm official checkouts have no team-owned tracked edits, and
  clean-build every affected board/profile. Shared chip/common/test/build
  changes require all supported boards; board-only wiring changes require
  that board plus shared host regression.
- **Downloadable artifact:** verify its build manifest, required ELF symbols
  and package through the sole BK7258 CLI. Report the exact package/artifact
  path, board/profile/boot and manifest identities, and actual verification
  results. Label direct or unsigned diagnostics explicitly; a loose binary
  pair is not a verified full package.
- **Whole-device recovery:** supply one dense BIN for offset zero whose size
  equals the selected partition CSV's full Flash capacity. Preserve immutable,
  factory-init, device-unique, preserve and unmapped bytes from the exact
  accepted same-unit readback; reset only transactional state. Record
  accepted-base hash/size, layout, stable device identity and capture method,
  and state that the image cannot be copied to another unit. A raw hash alone
  does not prove which device supplied the base. Do not assemble release bytes
  outside the maintained CLI.
- **Product release:** direct diagnostics use `package delivery` with the
  complete base, accepted-base evidence and explicit version; signed full/OTA
  products use `release product`. The verified ZIP includes `release.json`,
  `SHA256SUMS`, `FLASHING.md`, accepted-base/build/release-policy evidence,
  the complete device-bound recovery BIN and any compatible OTA package.
  OTA updates CP/AP and declares the accepted source version and installed root.
  The eligibility and recovery method for a root, BL2 or layout transition are
  separate decisions pending their own validation; this SOP does not prescribe
  wired full recovery. A universal factory image requires the reviewed
  per-device provisioner; otherwise declare
  `requires-provisioning`.
- **Atomic publication:** publish release directories and ZIPs with no-replace
  semantics; even an existing empty destination is an error. Packaged
  `evidence/build-manifest.json` must validate against packaged target,
  layout and security facts without the original `out/` path. Only initial
  release construction may consume the path-bound build handoff.

Host/Flash completion does not establish boot, function or physical acceptance.
Keep the separate target-side evidence described under Hardware boundary.

## Repository synchronization and checkout hygiene

Read this section when synchronization or checkout-conflict handling is needed.

- Treat the manifest as the repository inventory. Before syncing, inspect the
  relevant materialized projects, remotes and pinned revisions. Do not
  re-download an already complete project merely because it also exists upstream.
- Synchronize missing task projects and their required dependencies with normal,
  non-forced `repo sync`. Synchronize the complete manifest for full workspace
  initialization or an explicit full-sync request; one missing project does not
  require a full sync. Do not use `--force-sync`. A China-hosted mirror may be
  used when the original host is slow only when it serves the same repository
  and object identity; verify the pinned commit after syncing.
- A sync conflict may expose a team adaptation outside its owner. Inspect the
  change first; move verified team adaptations into this repository through the
  supported manifest/build extension, then clean only that verified accidental
  checkout change and retry. Never discard unrelated user work, explicitly
  excluded OpenAMP experiments or unknown untracked files as bulk cleanup.
- The manifest-pinned Beken SDK identity is authoritative and may point to the
  owner's fork. Do not silently replace that fork with vendor upstream or rebuild
  an installed bundle from an arbitrary local SDK tree. Verify remote, commit,
  profile and provenance first; synchronize or rebuild only when the pinned
  project or required object is actually absent or invalid.

## Workspace integration

Read this section when changing manifest mappings or application registration.
Paths below are relative to the contest repository or named official workspace.

- A `linkfile` source is relative to the contest project root and its destination
  is the official workspace discovery location. Link one coherent directory,
  not individual implementation files, and verify both the manifest source and
  the materialized destination symlink after changing it.
- Preserve `app/hello_app` and `quickapp/hello_quickapp` as generated template
  examples. Product commands and features belong in a separately named directory
  such as `app/bk7258` with its own manifest mapping; never turn the hello example
  into a product-app compatibility container.
- Map a NuttX built-in product command below an official application discovery
  boundary that recurses through CMake, Kconfig and Make, such as
  `apps/system/bk7258`. Do not place it below `packages/demos` merely because the
  generated hello template uses that location. After changing a mapping or
  symbol, verify the resolved `.config` and ELF/builtin table; a symbol present
  only in the seed defconfig is not integrated.

## Test integration

Read only the matching path when adding or changing test integration. These
are ownership rules, not a requirement to run every test path for each change.

- Linux-native mock and sanitizer regression stays under `tests/host/` in the
  contest checkout. Never map its host Makefile into the official OpenVela
  `tests/` application tree.
- Target CMocka applications stay under `app/testing/`, mirroring their official
  `apps/testing/` destination and using official Kconfig, Make.defs and
  Application.mk contracts. Expose the whole application through one manifest
  linkfile below `apps/testing/`; do not route it through `external/` when the
  standard location already supplies Kconfig, Make and CMake child discovery.
  Keep it ready to move upstream without a product-only build wrapper.
- Add board serial automation as a linked child below official pytest
  `tests/scripts/script/`. Reuse parent fixtures and the UART0 control channel;
  do not fork `conftest.py`, `utils/common.py` or `pytest.ini`.
