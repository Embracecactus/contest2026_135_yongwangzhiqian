# AIDK AI Toy product release and hardware operation

The repository has one public BK7258 workflow entry:
`tools/bk7258/bk7258.py`.  AIDK does not maintain a second build, signing,
packaging, key-broker, or deployment script.  This document records only the
physical operations that cannot be generalized across boards.

## Hardware paths

| Connector | Wiring | Purpose |
|---|---|---|
| CH340 Type-C | CH340E to UART0 TX/RX | BK Loader recovery and CP console |
| Native Type-C | BK7258 USB0 DP/DM | Signed CP/AP OTA transport |

CH340 RTS/CTS are not connected to CEN.  When BK Loader prints `Getting Bus`,
press and release K1 once.  An isolated relay, PhotoMOS, or open-drain fixture
across K1 may automate that physical action; do not drive CEN from
RS-232-level control signals.

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

## Signed full recovery

Private keys are operator inputs to the common CLI.  Generate a fresh,
independent BL1 and MCUboot P-256 generation for every new wired full download.
Keep private files outside Git in a vault/HSM or a mode-0600 temporary
directory, and destroy plaintext copies after package verification and hardware
acceptance.  Retain the installed MCUboot signer only through the approved
secret manager when future apps-only OTA is required.

```sh
tools/bk7258/bk7258.py build \
  --board aidk_ai_toy --boot mcuboot --clean --jobs 12 \
  --bl1-public-key "$BL1_PUBLIC" \
  --mcuboot-public-key "$MCUBOOT_PUBLIC" \
  --openssl /usr/bin/openssl --rollback-floor 1

tools/bk7258/bk7258.py release full \
  --build-manifest "$BUILD_MANIFEST" \
  --bl1-key "$BL1_PRIVATE" --mcuboot-key "$MCUBOOT_PRIVATE" \
  --version 1.0.0+1 \
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
CH340 rate is 460800 baud without fast-link.  Treat `GetBus`, erase, or write
failure text as failure even if the process exit code is ambiguous, and require
an explicit terminal success marker before reboot acceptance.

## Apps-only OTA

Build OTA with the public root already installed on the source devices and sign
it with that root's protected MCUboot private key.  Use a strictly increasing
generation and keep BL1/BL2/layout unchanged:

```sh
tools/bk7258/bk7258.py build \
  --board aidk_ai_toy --boot mcuboot --clean --jobs 12 \
  --bl1-public-key "$INSTALLED_BL1_PUBLIC" \
  --mcuboot-public-key "$INSTALLED_MCUBOOT_PUBLIC" \
  --openssl /usr/bin/openssl --rollback-floor 1

tools/bk7258/bk7258.py release ota \
  --build-manifest "$BUILD_MANIFEST" \
  --mcuboot-key "$INSTALLED_MCUBOOT_PRIVATE" \
  --version 1.0.1+2 --openssl /usr/bin/openssl \
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
