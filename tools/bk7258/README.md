# BK7258 maintainer CLI

`bk7258.py` is the only tracked BK7258 maintainer entry.  It builds, signs,
verifies, packages and deploys artifacts; command implementations live in
`_lib/`.


## Responsibility and reproduction boundaries

The public CLI parses arguments, dispatches operations and prints results. The
existing modules own their domain rules; module count is not a cleanup target.

| Responsibility | Existing owner |
| --- | --- |
| Build/configuration, layout, SDK and toolchain | `build.py`, `layout.py`, `sdk.py`, `toolchain.py`; official build backend executes dependencies |
| Image encoding, `.bkpack` and verification | `image.py`, `package.py`, `trust.py` |
| Artifact identity/name, release-directory evidence, delivery and recovery policy | `product.py`; `release_product` receives an explicit verification callback, never CLI arguments or device control |
| Transport and console control | `deploy.py`, `deploy_usb.py`, `deploy_console.py`; not prerequisites for compilation |
| Static ownership checks | `layers.py`; kernel compatibility checks stay in the existing dedicated implementation |
| Voice provisioning/KWS and display asset preparation | `voice.py`, `voice_kws.py`, `display_assets.py`; product tools, not build policy |

`build.validate_provenance` is the shared public evidence validator. Consumers
do not call a private build helper or independently recalculate the schema.
This change moves the reviewed release rules; it does not claim every CLI
operation is now free of orchestration. Eager command registration/imports
remain unchanged; command-specific lazy loading is not a measured speedup or a
prerequisite for this iteration.

Team-authored firmware stays in the team repository. The local Dolphin sources
live in `app/dolphin`, while existing Shaniu services remain in `app/bk7258`.
IndexTTS/server/phone/training components do not acquire a NuttX link merely by
being part of the product; source ownership and build registration are separate.
No new Git repository or `ttsindex` manifest project is introduced here.
Existing `frameworks`/`external` links carry maintained build integration.
The historical framework/FFmpeg patch and generated-source replacement chain
is retired; do not recreate it to satisfy a stale instruction.
For the explicit development remote override and official delivery distinction,
see the root [README](../../README.md).

## Model development is optional for firmware builds

The current public model and metadata live in `app/bk7258/models`; Android's
builtin WKM lives in its own assets directory. Both are versioned in the team
repository. No separate training repository or NuttX linkfile is required.
Private training recordings/candidates are excluded from public publication.
Training inputs must have an authorized local dataset manifest and preserved
source/voice split; exporting a candidate is not board or human acceptance.

Reuse the maintained commands and inspect their current arguments:

```sh
tools/bk7258/bk7258.py voice kws audit --help
tools/bk7258/bk7258.py voice kws train --help
tools/bk7258/bk7258.py voice kws evaluate --help
```

The documented training runs used TensorFlow 2.15.1 in a separate environment.
Ordinary firmware builds use the existing model and do not require TensorFlow,
a training corpus, private voice-cloning weights or cloud credentials.
See `.agents/skills/edge-wakeword-training/` for the reusable workflow and the
contest report for builtin-versus-App-activated model identities.

## Source-layer gate

Every `bk7258.py build` runs the board/chip/app ownership gate before it
configures either role.  It can also be run directly:

```sh
python3 tools/bk7258/bk7258.py verify layers
```

The gate rejects raw Beken SDK headers, calls and types in `boards/bk7258` or
`app` (including `app/dolphin`); chip-to-board dependencies; physical pin/bus ownership in app;
CP-only Kconfig symbols nested in AP-only menus (and the reverse); and new
product GATT/UUID policy in the chip layer.  Product protocol belongs in app,
physical and calibration facts belong in board, and SDK/controller mechanics
belong in chip.

The current working-tree gate also scans CMake/Make and related scripts under
`boards/bk7258` and `app` for private SDK paths, libraries, symbols and linker
wrapping. It is a static lexical check, not a CMake interpreter or proof of ELF
resolution, runtime behavior or every Kconfig dependency. New app subdirectories
are enumerated; dynamic/generated dependencies still need targeted review.

`layer_exceptions.json` contains only hash-bound legacy product-protocol files.
Changing one invalidates the gate and requires a deliberate layer review; it
is not a wildcard allowlist.  `tests/host/bk7258/test_bk7258_layers.py` injects
each forbidden dependency and verifies that the gate fails closed.

## OTA deployment

`deploy` streams a signed CP/AP OTA package through the native USB CDC port,
then uses the CH340 CP console to reboot and confirm the accepted generation.
It is the host peer of the chip-level `BK7258_OTA_SOURCE_USB` source.

```sh
python3 tools/bk7258/bk7258.py deploy --inspect-only --package FILE \
  [--expected-board NAME] [--expected-version V] [--expected-counter N]

python3 tools/bk7258/bk7258.py deploy --package FILE \
  [--ota-port PORT] [--control-port PORT]
```

Use `--status-only` or `--reboot-only` with `--expected-version`,
`--expected-counter`, and a CH340 control port to check an accepted package.
`--control-port none` stages the pair without rebooting it.  Python 3 and
`pyserial` are required only when a serial port is opened.

The signed catalog may be scoped with `--expected-board`; product automation
must always supply its selected physical board.

### Historical Gateway release catalog (not used by current Shaniu)

After `release product` has produced one device-bound delivery ZIP, export the
metadata consumed by the authenticated Android console with:

```sh
python3 tools/bk7258/bk7258.py release gateway-catalog \
  --delivery /releases/aidk-v18.6.390+450.zip \
  --openssl /usr/bin/openssl \
  --output /private/shaniu/firmware-releases.json
```

Repeat `--delivery` for additional device releases. The command verifies each
complete delivery and its embedded OTA signatures in the same process, then
creates a new, fsynced mode-0600 `shaniu.firmware-release-registry/1` file. It
refuses deliveries without a verified signed OTA component, duplicate releases,
more than 32 entries, and an existing output path.

The registry contains only the accepted device ID, target/source versions,
board/layout identities, exact signed `catalog.json` SHA-256, and OTA package
size/SHA-256. It contains no package path, URL, firmware bytes, credentials, or
signing material. Supplying it to Gateway enables only release-list display;
the firmware update mutation remains disabled until the board confirmation and
progress protocol is implemented.

## Shaniu display assets

`package eye-pack` turns the reviewable logical-eye JSON into one deterministic,
bounded `.bkep` product asset.  `verify eye-pack` performs read-only structural,
CRC, and content-bound checks before that file is copied to the AIDK soldered
SD NAND.  It does not package the asset into CP/AP firmware or claim signed
publisher trust.

```sh
python3 tools/bk7258/bk7258.py package eye-pack \
  --source app/bk7258/assets/display/shaniu-default-v1.json \
  --output out/shaniu-display/shaniu-default-v1.bkep \
  --preview-dir out/shaniu-display/previews

python3 tools/bk7258/bk7258.py verify eye-pack \
  --package out/shaniu-display/shaniu-default-v1.bkep
```

The source, SD NAND layout contract, visual-state list, and binary format are
documented under `app/bk7258/assets/display/`.

## Shaniu owner console enrollment

`voice console-enrollment` only writes a private enrollment document for the
owner's phone. It does not contact a board or Gateway and never mutates an
existing Gateway registry. With `--access-output`, it also creates a matching
single-grant registry for a new one-device Gateway setup. It never creates the
independent device certificate binding. The bearer token is read only from an
existing regular POSIX mode-0600 file, never from command arguments or
environment. The command currently refuses Windows: it will remain unavailable
there until an audited owner/DACL private-file implementation exists.

```sh
python3 tools/bk7258/bk7258.py voice console-enrollment \
  --device-id aidk-1 --https-origin https://gateway.example:8443 \
  --spki-pin 'sha256/BASE64_SPKI_SHA256' \
  --token-file private/console-token --expires-at-ms 1893456000000 \
  --access-output private/aidk-1-console-access.json \
  --output private/aidk-1-console-enrollment.json
```

On POSIX, the output is an O_EXCL-created, flushed and fsynced mode-0600
`shaniu.console-enrollment/1` JSON document with the device ID, HTTPS origin,
one to eight canonical SPKI SHA-256 pins, expiry, and access token. Its exact
fields are `protocol`, `device_id`, `gateway_origin`, `certificate_pins`,
`access_token`, and `expires_at_ms`. Preserve it as private owner material;
CLI status output deliberately omits the token. Both outputs are new,
O_EXCL-created, fsynced mode-0600 files. If either target already exists, review it
instead of overwriting it.

Before using the pair, load the device's mTLS certificate binding into the
Gateway `shaniu.device-bindings/1` registry, then start the Gateway with that
registry and the generated `shaniu.console-access/1` file. The enrollment and
access files contain the same `device_id` and future `expires_at_ms`; the latter
stores only the SHA-256 digest of the former's token. For a multi-device or
rotated deployment, merge reviewed grants into an operator-owned registry
outside this command rather than asking it to overwrite live authorization.
