# Shaniu Android companion A1

This directory contains the native Kotlin companion app for Shaniu. The
normal entry point configures the board's direct cloud connection through
authenticated BLE provisioning. It does not reconnect to a stored Gateway or
ask for console credentials. A durable local receipt is shown as a saved
claim, never as proof that the board is online. Unavailable device controls
are labelled explicitly. MiMo is the initial editable service preset; no
device identity, API key, access token or certificate pin is baked into the APK.

The previous HTTPS/WSS `console-v1` client is accessible only through the
debug build's explicit historical-service entry. A release build ignores the
`legacy_console` intent extra. Deterministic fake services remain test fixtures.

The provisioning foundation under `provision/ProvisionTls.kt` creates a TLS 1.2
client engine pinned to the exact device certificate SHA-256 supplied by the
owner's bootstrap. It checks validity dates and signing/server-auth usage and
does not alter the Gateway trust policy. Host tests generate a temporary P-256
identity and exercise real TLS acceptance/rejection plus invalid certificate
dates. The provisioning activity connects this foundation to GATT, activation import,
possession proof and local confirmation. Their physical board interoperability
still requires acceptance; host TLS tests alone do not prove it.
`ProvisionBootstrap` now strictly parses bounded owner-supplied QR data and
provides redacted object/error strings plus explicit clearing of owned decoded
secret buffers. It neither scans a QR code nor authenticates or claims a device
by parsing it. Scanner/JVM text copies are outside its clearing guarantee.

`ProvisionTlsChannel` drives the client SSLEngine over a bounded byte stream.
Its serialized owner must provide GATT queues, generation checks, deadlines and
disconnect handling. Host engine tests cover 20-byte fragmentation, bidirectional
data, ciphertext corruption, early writes and queue rejection. These tests do
not establish Android Bluetooth or board mbedTLS interoperability.

`ProvisionGattSession` adds worker-owned bounded RX/TX queues, negotiated ATT
fragment sizing, one acknowledged write at a time, generation/token filtering,
and handshake/write/session deadlines. Host tests bridge two real TLS engines
through these queues at MTU 23 and 185. `AndroidProvisionGatt` now bridges real
platform service discovery, MTU, CCC subscription, write acknowledgements and
notifications through a bounded worker queue, polls deadlines and closes the
platform connection on failure. It requires a caller-selected BluetoothDevice
and granted CONNECT permission, supplied by `ProvisionActivity`. The board
service and phone/platform callback acceptance remain separate hardware gates.

Before APPLY, `ProvisionBindingStore` durably records the public device and
transaction locator. It can also save an AES-GCM encrypted control key, bound
to that device and transaction by authenticated data. Android's non-exportable
Keystore key protects it at rest; pending ciphertext is promoted in the same
atomic commit as the binding, and missing/corrupt ciphertext fails closed.
Plaintext borrowed for a control request is wiped when its callback returns.
The normal flow now creates an independent random SCB3 owner key and saves the
exact device certificate pin with its authenticated binding. Daily BLE controls
use this identity for STATUS, CANCEL, VOLUME, PERSONA, CLEAR_HISTORY,
MEMORY_SET and MEMORY_DELETE; no Gateway is required. Memory controls are
asynchronous: the response accepts the operation; subsequent status must leave
pending, report no failure and confirm the intended enabled state before the
App displays success. A disconnect loses completion attribution, not the board
job; reconnection only reports current state. No mutation is auto-replayed.
Persistence is off by default, stores at most the latest three turns encrypted
on SD, and uses a private owner-bound policy/key. Disable retains the ciphertext;
delete rotates the key, disables persistence and clears RAM after durable policy
publication. Uncertain private-policy publication stays blocked until restart.
These paths have host/target build evidence, not physical-board acceptance.
The privacy page clears device RAM conversation context only after confirmation;
recording/cloud/playback activity rejects that request. It does not delete provider
records or change credentials/persona, and old firmware may reject the new command.
The UI uses confirmed responses, closes the phone connection on backgrounding,
and does not replay a timed-out mutation. Physical acceptance remains pending.

## Direct local OTA source protocol and acceptance boundary

An authenticated SDC1 session advertises `INFO=9` through STATUS capability bit
4096 and the OTA command family through bit 8192. OTA commands are
`OTA_BEGIN=10`, `OTA_APPEND=11`, `OTA_START=12`, `OTA_STATUS=13` and
`OTA_CANCEL=14`. They retain the normal 16-byte request header and 40-byte
reply. `BEGIN` carries a four-byte record length (44..3371); each `APPEND` is
1..32 bytes; `START`, `STATUS` and `CANCEL` are empty. The App advances an
append only after its matching reply. A `START` acknowledgement means that the
device accepted the source request, not that firmware was installed.

The record is `SOU1`: 4-byte magic, big-endian URL and PEM lengths, 4-byte
phone IPv4, raw 32-byte catalog SHA-256, then URL and PEM bytes. URL is at
most 255 bytes, PEM is at most 3072 bytes, and the entire record is at most
3371 bytes. The selected `.bkpack` is locally integrity-checked, then a
short-lived phone-local HTTPS source exposes only the five verified ZIP
members. Its ephemeral certificate is sent over the already authenticated BLE
control channel; no Gateway, firmware upload over BLE, private-key export or
accept-all TLS path is used. The device remains authoritative for certificate,
catalog signature, board and security-counter checks.

`OTA_STATUS` reports state 0 idle, 1 queued, 2 active or 3 terminal; phases
are downloading(1), verifying(2), staged(3), rebooting(4), trial(5),
confirmed(6), rolled back(7), failed(8). The App calls success only after a
terminal `confirmed` report with matching persisted device ID, version and
counter. A cancel reply of `-EALREADY` is not displayed as cancellation: the
source upload ends and the App continues status/version reconciliation. Moving
the App to background closes the temporary source and reports interrupted
transfer; reconnecting may only verify device-reported state.

Host tests exercise parsing and control sequencing. They do not prove Android
Bluetooth callbacks, AndroidKeyStore/mbedTLS interoperability, Wi-Fi reachability,
download, flash, reboot, rollback or confirmation on a board. Required device
acceptance is: authenticate over BLE; select a signed compatible package; start
from active Wi-Fi; verify all source ranges are fetched; observe each phase;
test cancel before and after `START`; force a trial failure/rollback; and prove
only phase 6 plus the actual post-reboot version/counter is reported complete.

A board COMMITTED response becomes an App COMMITTED result
only when one atomic preference commit publishes the bound device and consumes
that matching receipt. Disk failure remains UNCONFIRMED. Because Android updates its memory cache
before confirming disk persistence, all adapters sharing that preferences object
stop reads/writes after a failed commit. A new process reloads durable state for
reconciliation; reopening an Activity alone does not establish persistence. Legacy non-atomic device mirrors never become authoritative
bindings. The settings page can delete this handset's locator and encrypted
control data; it does not erase the board's network configuration.

## Direct-control cross-language verification

From the repository root, compile the production C session implementation with
its synthetic AP-state host peer:

```sh
cc -std=c11 -Wall -Wextra -Werror -I app/bk7258 \
  tests/host/bk7258/test_control_session.c \
  app/bk7258/bk7258_control_session.c -o /tmp/shaniu-control-peer
/tmp/shaniu-control-peer
cd android/shaniu-companion
SHANIU_CONTROL_PEER=/tmp/shaniu-control-peer ./gradlew :app:testDebugUnitTest \
  --tests '*DeviceControlInteropTest' --offline
```

The Kotlin production protocol sends AUTH, STATUS, CANCEL, VOLUME and PERSONA
to the C production parser through process pipes and consumes fragmented real
responses. It also checks busy-setting rejection and subsequent confirmed state.
The peer's key and AP state are synthetic: this does not prove TLS, BLE, actual
flash writes or physical audio behavior. Without `SHANIU_CONTROL_PEER` the
optional test is reported skipped; the executable is a Gradle test input.

The complementary board-side encrypted test runs from the repository root:

```sh
python3 tests/host/bk7258/test_provision_tls.py
```

It builds the workspace mbedTLS and production control pair/session, then uses
20-byte fake GATT queues. Daily-control coverage includes AUTH split across
single-byte TLS records, congestion without repeated mutation, replay rejection,
wrong owner keys and authentication timeout cleanup. Its TLS client is mbedTLS,
and does not use the physical Android Bluetooth stack.

To run the complete Android host suite against both native peers while the
runner keeps temporary mbedTLS binaries alive:

```sh
SHANIU_ANDROID_INTEROP=1 python3 tests/host/bk7258/test_provision_tls.py
```

This also tests production `ProvisionTls`, `ProvisionGattSession` and
`DeviceControlProtocol` with the C control pair through 20-byte ciphertext
pipes. It verifies exact certificate pin acceptance/rejection, owner proof and
confirmed volume. Temporary test certificates and private keys are deleted on
exit. The TLS provider here is the host JVM; Android's device provider, ATT
callbacks, radios and physical AP actions remain separate acceptance gates.

## Android runtime acceptance

Build `:app:assembleDebugAndroidTest`, install the debug application and test
APK, then run on the selected emulator/device:

```sh
adb -s <serial> shell am instrument -w \
  com.shaniu.companion.test/com.shaniu.companion.provision.ControlKeyInstrumentation
```

Optional `-e cloud_probe 1` additionally calls the production CloudEndpoint
preflight against the public MiMo Token Plan endpoint using Android system trust
and TLS 1.2, matching the board transport. It sends no API credential or model
request. This is opt-in network acceptance, not part of the offline test suite.
A passing emulator run does not prove physical-board network or playback behavior.

Optional `-e ui_probe 1` renders the real MainActivity with synthetic in-memory
snapshots. It checks memory-operation gates, unknown policy state, offline text,
and absence of a conversation cancel button during a memory job. It neither
persists a binding nor opens BLE. The test finishes the Activity and writes a
synthetic screenshot to app cache `device-ui-acceptance.png`; retrieve/remove it
with `adb exec-out run-as com.shaniu.companion ...`. This is UI acceptance only.

The runner uses uniquely named disposable preferences and Keystore entries.
It checks encrypted pending recovery, authenticated certificate pins, borrowed
key wiping, and native Android TLS 1.2 engine exchanges with 20-byte ciphertext
fragments and wrong-pin rejection. Its temporary EC signing key permits raw
prehashed ECDSA (`DIGEST_NONE`) for Conscrypt callbacks and SHA-256 for the test
certificate signature. This is test-only; production key permissions are not
changed. All test entries are deleted afterwards. Transport is an in-memory
queue, so this test does not certify BluetoothGatt or physical board behavior.

## Historical service console (debug entry only)

In the historical service console, after board provisioning commits, the overview page asks the owner to import a
bounded `shaniu.console-enrollment/1` JSON document. An unbound phone can also
choose **连接已有设备** on the overview or settings page to import owner-issued
credentials for an already provisioned device, without repeating Bluetooth
provisioning. This connects an existing authorized device; it does not perform
physical claiming or change the board's network. When a local binding or
provisioning result exists, the document must name that same device.
The document carries an explicit HTTPS Gateway root, one to eight canonical
`sha256/...` SPKI pins, a future expiry and the access token. The app validates
the document and authenticates a device snapshot before committing the local
binding. Raw endpoint and token entry remains a debug-only developer panel.
Non-secret connection metadata is stored in private app preferences. The token
is stored separately with AES-GCM under a non-exportable Android Keystore key
and is never rendered or logged. This is an at-rest guarantee only and does not
claim StrongBox or hardware-backed key storage.

After an authenticated snapshot succeeds, the app opens a cursor-bound WSS
event stream. Sequence gaps fail closed and trigger a full snapshot refresh.
All network and Keystore work runs off the UI thread. Disconnect/reconnect uses
a connection generation so late callbacks cannot update the new session. While
the Activity is foreground, retryable transport failures use bounded exponential
backoff (1, 2, 4, 8, 16, then 30 seconds) and re-authenticate through a fresh
snapshot before reopening WSS. Going to the background or explicitly disconnecting
cancels pending recovery. Mutations are never retried automatically.

The active UI supports:

- reported device, turn, emotion, battery, firmware and update state;
- reported board-owned turn state and explicit cancellation without capturing
  phone audio;
- volume and persona mutations;
- long-term-memory revoke and delete controls, with no remote enable path;
- an immutable-manifest firmware release list and a locally confirmed install
  request. The button is enabled only for an idle device whose reported source
  version matches the verified release.

The UI never applies a mutation optimistically. An accepted receipt means only
that Gateway admitted the request; the device must report the resulting state.
For OTA, only a later `CONFIRMED` report is completion.

The client freezes these relative endpoints:

- `GET /console/v1/devices/{device_id}/snapshot`;
- `GET /console/v1/devices/{device_id}/firmware/releases?generation={generation}`;
- `POST /console/v1/devices/{device_id}/mutations`;
- `WSS /console/v1/devices/{device_id}/events?generation={generation}&after_sequence={sequence}`.

`OkHttpConsoleGatewaySession` injects the token only inside the authenticated
transport, rejects cross-origin requests, disables redirects and transparent
retries, bounds decoded bodies to 64 KiB, and installs no logging interceptor.
The manifest permits Internet access, forbids cleartext traffic and trusts only
system CAs. Clearing local binding data removes both ordinary configuration and
the encrypted credential.

Use an Android emulator for routine UI, navigation and client error-flow work.
Select its explicit ADB serial when installing or driving the app; do not depend
on a connected physical phone. BLE/NFC and board audio acceptance still require
the actual devices. The app uses `minSdk 29` and compiles/targets SDK 35:

```bash
./gradlew :app:testDebugUnitTest :app:assembleDebug
```

The debug APK is emitted at
`app/build/outputs/apk/debug/app-debug.apk`. Host unit tests cover the strict
wire contract, state reducer, mutation policy, secure transport construction,
failure mapping and the isolated fake lifecycle. A live emulator test with a
disposable local CA covers TLS/WSS interruption, generation-changing recovery,
explicit disconnect and foreground/background cleanup. Production CA deployment,
Xiaomi 10 behavior and physical Gateway reachability remain device tests; a host
build alone is not evidence for those gates.

The development `console-v1` snapshot permits explicit JSON `null` for
`volume_percent` and `charging` when the device hasn't reported them. Both keys
remain required, numeric values remain bounded to 0–100, and charging accepts
only a boolean or null. The UI displays unknown instead of inventing 50% or
not-charging; the volume slider is disabled until a real value arrives.
Known-value settings/battery change events retain their existing strict types.
Gateway and app must use this updated development contract together; older
strict decoders cannot consume null snapshots.

The current Gateway console endpoint reports explicit `unknown` for emotion
and for OTA before an authoritative board report exists. Its optional
metadata-only release registry can populate the verified release list only when
the board reports both the exact required source version and the matching
MCUboot public-root SHA-256. The install mutation sends only the selected
manifest SHA-256 and requires a local confirmation; it never sends a URL,
filesystem path, CA or firmware bytes. The Gateway waits for a matching board
OTA report before returning an accepted receipt, and the App still treats only
reported `CONFIRMED` as success.
Shared protocol
vectors under `gateway/shaniu/tests/fixtures/console-v1` are test resources for
both implementations. Connect to the separately configured console HTTPS port,
using an operator-issued console enrollment file, never the MiMo API key. Its
token, device ID and expiry must match an active `shaniu.console-access/1`
Gateway grant. Current backend
controls support persona changes within the active MiMo connection, cancellation
with a separate board stop ACK, and runtime volume with a matching board policy
report. These paths have local integration coverage; physical acceptance remains
pending. Board provisioning and App control enrollment are wired in source and
covered independently on the host/emulator. The OTA control protocol and App
entry have host coverage; the device HTTP source, reboot reconciliation and
physical rollback flow remain outside that evidence.

BLE provisioning is under development in this module; phone media upload and
physical device-bound OTA execution are still pending. The production Gateway runs separately.
