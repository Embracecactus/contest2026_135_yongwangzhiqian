# AIDK AI Toy product release and hardware operation

The repository has one public BK7258 workflow entry:
`tools/bk7258/bk7258.py`.  AIDK does not maintain a second build, signing,
packaging, key-broker, or deployment script.  This document records only the
physical operations that cannot be generalized across boards.

## Validation and signing scope

Use three scopes. An ordinary increment reuses the current build tree, runs
only the affected role and its relevant regression, and stops when the stated
short-loop observation resolves its one hypothesis. An impacted regression
adds only the board/profile or shared-chip tests selected by the changed owner;
record why each test applies. A boot, trust, layout, migration, or recovery
change uses the dedicated clean/package/readback/boot workflow below.

`--clean`, key generation, key destruction, and full product packaging are not
ordinary increment steps. The maintained BL1/MCUboot signer is referenced by
its approved secure local path, secret-manager, or HSM reference and public fingerprint. A missing
private signer blocks release signing only; it does not block build, regression
or an explicitly labelled unsigned diagnostic artifact. Never put a private
key value or path in this file, a command log, or the repository.
The current CLI accepts explicitly supplied key-file paths; it does not resolve
secret-manager or HSM URIs. Choosing such a service and connecting its signing
capability is a separately authorized deployment, not an automatic fallback.

For a short hardware loop, use mechanical tools for build/config/artifact
facts and give an agent only the current hypothesis, compact evidence, affected
owner, stop condition, and relevant tests. Stop after the observation confirms
or rejects that hypothesis; do not add unrelated probes or broad validation.

## Build handoff and immutable releases

New builds emit `bk7258.build-manifest/3`; historical `/2` handoffs remain
readable. `--product` is explicit metadata, not a board-derived product guess.
The manifest records the selected CP/AP profiles, project commit and bounded
source digest, actual NuttX/apps Git baseline and changed-source digests, and
existing SDK, toolchain, layout and public trust identities. For an isolated
NuttX copy, Git compares the actual copy against the canonical baseline without
refreshing the canonical index. No private diff or key material is archived.

The ordinary generated configuration defaults to
`CONFIG_LIBC_UNAME_DISABLE_TIMESTAMP=y`, avoiding NuttX's always-touch/relink
path. An explicit profile override is respected. Source provenance changes
only the manifest, never a public header or the role cache identity.

New `/3` releases require an explicit `--artifact-id` at creation. For example:
`shaniu-bk7258-aidk_ai_toy-app__openvela_ap-v18.6.354+419-bdev02-ota.bkpack`.
The same stem with `-full.bkpack` and `-full.bin` identifies a full release.
Keep SDK internal names unchanged; a release destination cannot be overwritten.
Use the exact sealed bytes for subsequent query, verification and delivery;
repackaging or re-signing is a new artifact, not a rename of an accepted one.

`version`, `artifact_id`, and `security_counter` have separate roles. This batch
retains the existing compatibility mapping: `+GENERATION` supplies the counter,
full release generation equals the compiled floor, and OTA must meet existing
floor and running-device checks. Changing only an artifact ID does not permit
an upgrade. The variables below are approved inputs, not instructions to reset
any counter or to migrate the installed trust domain.

Inspect data impact with the existing query, without building or signing:

```sh
tools/bk7258/bk7258.py package flash-contract \
  --package "$PACKAGE" --transport full-bin
```

Use `--transport ota` for an OTA package. Full-bin reports the complete layout's
erase/write range separately from package overlays. OTA does not require a
full-device base; its device-managed erase details and startup migration remain
unknown in this static report. This report does not authorize a device write or
waive HIL's target, exact-file and scope checks. Complete product acceptance and
recovery exercises are separate from a normal build/package iteration.

## Hardware paths

| Connector | Wiring | Purpose |
|---|---|---|
| CH340 Type-C | CH340E to UART0 TX/RX | BK Loader recovery and CP console |
| Native Type-C | BK7258 USB0 DP/DM | Signed CP/AP OTA transport |

CH340 RTS/CTS are not connected to CEN.  Never use COM8 RTS/DTR as reset.  If
the running firmware provides the `reset reboot` command, first eject native
USB MSC (or switch it back to CDC), then use BK Loader's atomic software-reset
handoff:

```text
bk_loader.exe download -p 8 -b 460800 -s 0 -i FULL_FLASH.bin \
  --swrst "reset reboot" --hard-reset 0 --reboot 1 --uart-type CH340 \
  --fast-link 1
```

This makes BK Loader wait for and consume the boot-ROM window created by the
firmware reboot; it does not toggle modem-control pins.  The AIDK CH340 path
requires `--fast-link 1` for this handoff; the same command without it timed
out before erase/write.  If software reboot is unavailable, press and release
K1 once when BK Loader prints `Getting Bus`.
An isolated relay, PhotoMOS, or open-drain fixture across K1 may automate that
physical fallback; do not drive CEN from RS-232-level control signals.

## Accept one device readback

Recovery is not based on a generic factory image.  Read the complete 8-MiB
Flash from the exact unit and assign the unit a stable asset/serial identifier.
Then create canonical acceptance evidence:

```sh
tools/bk7258/bk7258.py package accept-base \
  --board aidk_ai_toy \
  --base /secure/device-01/readback-8m.bin \
  --device-id AIDK-DEVICE-01 \
  --capture-method bk-loader-readback \
  --output /secure/device-01/accepted-base.json
```

The command binds the base hash and size to the physical board, selected
partition layout, device ID, and capture method.  The JSON is operator
acceptance evidence, not hardware attestation; the operator/fixture remains
responsible for proving that the readback came from the named unit.

If the board CSV moves protected data, pass the snapshot's actual old CSV as
`--source-partition` and a new private BIN path as `--relocated-base` to the same
`package accept-base` command. It copies same-name protected partitions without
changing their size, permissions, policy or bytes; immutable partitions cannot
move. Retain the printed source/target hashes and per-partition mapping with the
private release evidence. The resulting evidence is explicitly marked as a
same-device partition relocation, not a new hardware readback. Use that new BIN
and its accepted-base evidence for `release full`. Reusing a historical base
does not preserve later configuration or memory; a user-directed full write
without a fresh snapshot must state that limitation. Never publish these bases.

## Signed full recovery

Private keys are operator inputs to the common CLI through the maintained,
explicit signer reference. Use an approved secure local path, secret manager,
or HSM and record only the signer reference/public fingerprint in release evidence. Rotate or
replace an identity only under explicit owner authorization.

This AIDK path emits and writes a dense 8-MiB image. It is whole-device
recovery, not a partial update: before any erase/write/migration, locate and
verify trusted same-device base material. A historical accepted full readback
may be reused when its device identity and hash match; the full base file
required by the CLI does not mean a new capture is required for every write.
A configuration rollback requires explicit authorization. If non-reconstructible
data has no trustworthy same-device material, refuse the operation.
Device-unique state remains target-bound even when the requested feature changes
only CP or AP.

```sh
tools/bk7258/bk7258.py build \
  --board aidk_ai_toy --boot mcuboot --product shaniu --jobs 12 \
  --bl1-public-key "$BL1_PUBLIC" \
  --mcuboot-public-key "$MCUBOOT_PUBLIC" \
  --openssl /usr/bin/openssl --rollback-floor "$ROLLBACK_FLOOR"

tools/bk7258/bk7258.py release full \
  --build-manifest "$BUILD_MANIFEST" \
  --bl1-key "$BL1_PRIVATE" --mcuboot-key "$MCUBOOT_PRIVATE" \
  --version "$RELEASE_VERSION" --artifact-id "$ARTIFACT_ID" \
  --base /secure/device-01/readback-8m.bin \
  --base-evidence /secure/device-01/accepted-base.json \
  --openssl /usr/bin/openssl --output-dir /secure/device-01/release-1

tools/bk7258/bk7258.py release product \
  --full-release /secure/device-01/release-1 \
  --base /secure/device-01/readback-8m.bin \
  --openssl /usr/bin/openssl \
  --output /secure/device-01/aidk-device-01-v1.0.0-1.zip

tools/bk7258/bk7258.py verify delivery \
  --delivery /secure/device-01/aidk-device-01-v1.0.0-1.zip \
  --openssl /usr/bin/openssl
```

The product ZIP contains one dense operator BIN covering offsets
`0x000000..0x800000`, the signed recovery package, canonical base evidence,
build/release-policy evidence, checksums, and flashing instructions.  Use the
BIN only on the device ID named in `release.json`; do not chip-erase and do not
copy it to another unit.  Until manufacturing provisioning assigns unique
MAC/RF/Bluetooth/calibration state, the ZIP reports
`factory=requires-provisioning`.

Use BK Loader at offset zero and length `0x800000`.  The validated conservative
CH340 software-reset handoff uses 460800 baud with `--fast-link 1`, as above.
Treat `GetBus`, erase, or write
failure text as failure even if the process exit code is ambiguous, and require
an explicit terminal success marker before reboot acceptance.

## Apps-only OTA

Build OTA with the public root already installed on the source devices and sign
it with that root's protected MCUboot private key.  Use a strictly increasing
generation and keep BL1/BL2/layout unchanged:

```sh
tools/bk7258/bk7258.py build \
  --board aidk_ai_toy --boot mcuboot --product shaniu --jobs 12 \
  --bl1-public-key "$INSTALLED_BL1_PUBLIC" \
  --mcuboot-public-key "$INSTALLED_MCUBOOT_PUBLIC" \
  --openssl /usr/bin/openssl --rollback-floor "$ROLLBACK_FLOOR"

tools/bk7258/bk7258.py release ota \
  --build-manifest "$BUILD_MANIFEST" \
  --mcuboot-key "$INSTALLED_MCUBOOT_PRIVATE" \
  --version "$OTA_VERSION" --artifact-id "$OTA_ARTIFACT_ID" --openssl /usr/bin/openssl \
  --output-dir /secure/device-01/ota-2
```

`tools/bk7258/bk7258.py deploy` performs the common verified USB OTA transport.
The CH340 console remains the control/reboot path.  The product ZIP may combine
the new wired recovery and an OTA package, but it records two independent trust
facts: the root installed by wired recovery and the root required on source
devices for OTA.  They may differ during an intentional full-download key
rotation.

## Acceptance

A release is accepted only after:

1. the target-bound build manifest verifies;
2. signed package trust and the outer product ZIP verify with OpenSSL;
3. the operator BIN is exactly 8 MiB and names the correct device/base evidence;
4. BK Loader reports a complete write and the rebooted CP reports the requested
   confirmed version/counter with AP, CPU2 and RPTUN healthy; and
5. plaintext private keys are absent from the repository and operator logs.

If native USB does not enumerate, confirm the cable is on USB0 rather than the
CH340 connector.  The expected AP log is
`AIDK USB OTA: ready ep=02/82 protocol=1 max-payload=128`.  Do not bypass a
failed board acceptance with raw Flash writes or chip/SDK/NuttX changes.
