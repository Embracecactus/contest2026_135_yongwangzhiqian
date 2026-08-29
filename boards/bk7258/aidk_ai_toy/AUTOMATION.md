# AIDK AI Toy build, full provisioning, and OTA automation

## What is automated

There are two physical USB paths on this board:

| Port | Wiring | Role |
|---|---|---|
| CH340 Type-C | CH340E to UART0 TX/RX | First BK Loader download, CP console, software reboot, acceptance |
| Native Type-C | DP/DM directly to BK7258 USB0 | CDC-ACM signed OTA object transport |

The first full download cannot be made completely automatic by software on the
unmodified PCB: CH340 RTS/CTS are NC and no USB signal controls CEN.  Start the
single full download and press K1 RESET once when BK Loader prints `Getting
Bus`.  A relay/PhotoMOS or open-drain transistor across K1 may supply the
optional reset hook; never drive CEN directly from RS-232-style control levels.

After that first provisioning, no button is needed.  The native USB transport
streams the signed catalog and CP/AP images into the existing OTA Manager.  The
host then sends `bkota reboot` through CH340, BL2 boots the pending pair, and the
Supervisor confirms it after the configured stable-health interval.  A failed
trial resets and reverts to the previously confirmed pair.

USB is deliberately CDC rather than raw DFU or MSC.  Raw DFU would bypass the
paired signature/counter/commit policy.  Exporting fixed NAND as MSC while AP
also mounts it would introduce two filesystem owners.  The CDC request/response
source needs no NAND cache and never exposes on-chip Flash.

## One-time host setup

Required tools:

- the repository-pinned BK7258 toolchain and SDK profiles;
- OpenSSL at `/usr/bin/openssl` (override with `AIDK_OPENSSL`);
- BEKEN BKFIL `bk_loader.exe` for initial provisioning;
- Python 3 plus `pyserial` in the environment that can open Windows COM ports.

Create an operator-owned password file outside the repository:

```sh
install -m 600 /dev/null /secure/aidk-broker-password
# Fill it through the operator's secret manager; do not paste it into a log.
export AIDK_KEY_BROKER_PASSWORD_FILE=/secure/aidk-broker-password
```

The default broker encrypts the installed MCUboot signing key as PKCS#8 AES-256
outside Git.  Plaintext copies exist only in `/tmp/aidk-trust.*` while a command
is running.  For a vault/HSM, set `AIDK_KEY_BROKER` to an executable implementing:

```text
broker seal   STATE_DIR ROOT_GENERATION PLAINTEXT_INPUT
broker unseal STATE_DIR ROOT_GENERATION PLAINTEXT_OUTPUT
broker discard STATE_DIR ROOT_GENERATION
```

When WSL cannot open Windows COM ports, run the transport with a Windows Python
that has `pyserial`:

```sh
export AIDK_WINDOWS_PYTHON='/mnt/c/Path/To/python.exe'
```

## First full provisioning

Use one accepted factory snapshot truncated to `0x7fa000`; the release tool
validates its digest and materializes exactly one operator image.  Every
invocation creates fresh, independent BL1 and MCUboot roots.

```sh
boards/bk7258/aidk_ai_toy/scripts/aidk_pipeline.sh provision \
  --state-dir /secure/aidk-device-01 \
  --base /secure/aidk-device-01/accepted-base.bin \
  --version 1.0.0+1 \
  --output /tmp/aidk-release-1 \
  --loader '/mnt/c/Tools/BKFIL/bk_loader.exe' \
  --port COM9
```

This is one transaction: fresh keys, clean CP/AP/BL2/BL1 build, signed full
release, one `0x0-0x7fa000` loader download, reboot, and `bkota status`
acceptance for the exact version/counter.  It neither chip-erases nor writes
the immutable tail.  Only after runtime acceptance are public roots and device
state promoted; the BL1 private key is destroyed and the MCUboot key remains
only in the configured encrypted broker.

The default BK Loader transfer rate is 460800 baud and fast-link is omitted.
That exact CH340/COM9 path completed two independent full 8 MiB reads at
460800 without fast-link, while 1, 2, and 6 Mbps fast-link downloads showed
intermittent `SetBR7231N` relink failures during erase or write.  Override only
for a validated fixture with `AIDK_DOWNLOAD_BAUD`; a faster handshake is not
an acceptance criterion.
BKFIL 2.1.11.15 may return process status 1 even after printing its terminal
success banner, so the pipeline requires the explicit success marker and
rejects every known GetBus/erase/write failure marker before runtime acceptance.

For an automatic hardware reset, pass an executable with `--reset-hook`.  The
pipeline starts it immediately before BK Loader; the hook owns its bounded
delay and one reset operation.  This AIToy has no J-Link attached.  A
relay/PhotoMOS or open-drain transistor across K1/CEN may provide a future
fixture hook; the pipeline waits for that hook and rejects the transaction if
either the hook or the full loader operation fails.

Without a reset hook, provisioning must run in an interactive terminal.  It
builds and seals the release first, then pauses before opening BK Loader.  Press
Enter when the operator is ready at the board.  After BK Loader prints
`Getting Bus`, press and release K1 RESET once; do not hold it before opening
the port.  BK Loader keeps the bounded ROM handshake window open while that
single reset enters the serial downloader.

## Normal modify, build, and OTA

After making board-only changes, use a strictly higher generation:

```sh
boards/bk7258/aidk_ai_toy/scripts/aidk_pipeline.sh ota \
  --state-dir /secure/aidk-device-01 \
  --version 1.0.1+2 \
  --output /tmp/aidk-release-2
```

That command performs a clean build with the accepted public roots, temporarily
unseals the matching MCUboot signer, creates one signed OTA `.bkpack`, streams
it over the native USB port (VID:PID `1209:0001`), reboots through the CH340
port (VID:PID `1a86:7523`), and waits for the exact confirmed version/counter.
The accepted state advances only after that final confirmation.

Use explicit ports if automatic discovery is ambiguous:

```sh
... aidk_pipeline.sh ota ... --ota-port COM10 --control-port COM9
```

To build/sign without touching hardware, add `--no-deploy`.  To deploy an
already produced package, the pipeline first runs the single complete public
trust verification and then starts the transport:

```sh
boards/bk7258/aidk_ai_toy/scripts/aidk_pipeline.sh deploy-ota \
  --package /tmp/aidk-release-2/package/firmware-aidk_ai_toy-v1.0.1+2-ota.bkpack \
  --state-dir /secure/aidk-device-01 --version 1.0.1+2
```

The state-aware form also checks that the package's embedded version/counter
match the arguments.  Before opening native USB it uses the CH340 console to
software-reboot and strictly confirm the currently accepted generation; this
clears a stale failed OTA worker without K1.  It promotes accepted state only
after the staged pair reboots and passes the same strict confirmation.

## HTTPS alternative

The profile also enables verified HTTPS Range OTA.  Associate Wi-Fi through the
product's credential policy, provision the server CA on an AP-visible path,
then use:

```text
bkota apply-http https://server/path/catalog.json /path/to/server-ca.pem
bkota reboot
```

Plaintext HTTP remains disabled.  HTTPS and USB are only source transports;
both converge on the same signed catalog, exact image hashes, inactive-slot
geometry, AP-first/CP-sector-zero-last commit, trial boot, and auto-confirm
policy.

## Acceptance gates

A handoff is complete only when all applicable gates pass:

1. `bk7258.py build --board aidk_ai_toy --boot mcuboot --clean` publishes one
   target-bound manifest.
2. `release full|ota` completes its package structure and public trust checks.
3. Full provisioning writes one operator image and reports no GetBus/write
   failure; OTA reaches `READY_TO_REBOOT` through the USB source.
4. The rebooted CP console reports the requested
   `pair=confirmed version=... counter=...`, AP state 2/error 0, CPU2 online,
   RPTUN state 4/error 0, OTA manager state 0, and Supervisor state 2.
5. Private plaintext key files no longer exist, and no tracked file outside
   `boards/bk7258/aidk_ai_toy/` was changed by this workflow.

If native USB does not enumerate, first confirm the direct Type-C cable is on
USB0 rather than the CH340 connector.  The expected AP log is `AIDK USB OTA:
ready ep=02/82 protocol=1 max-payload=128`.  Board initialization deliberately
holds the device controller disconnected for 250 ms before enumeration, then
owns the OTA bulk endpoint packet pump.  Its configuration callback first arms
Bulk OUT, each completion copies that MUSB FIFO packet into the bounded SPSC
ring and rearms the next packet; `/dev/ttyGS0` is not the OTA data path.
The Windows host pads every outbound protocol frame to the 64-byte Bulk packet
boundary.  Padding is outside the declared frame payload and the target's
magic resynchronization discards it; the host deliberately avoids unbounded
CDC `SetCommState`/`PurgeComm`/`flush` calls.  On Windows it opens an overlapped
COM handle with bounded read/write timeouts because USB bulk OTA needs no UART
DCB, baud change or modem-control operation; open retries remain inside the
configured deadline and tolerate the short AP re-enumeration disconnect.
The host permits 120 seconds of silence between protocol frames by default
(`--frame-timeout` overrides it), so a bounded inactive-slot erase does not
abort an otherwise live whole-package transaction.
Do not fall back to raw Flash writes or change chip/SDK/NuttX to bypass a
failed board acceptance.
