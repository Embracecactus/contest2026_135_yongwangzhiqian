# Windows / WSL2 BLE GATT Client

This optional companion is a bounded, native Windows C++/WinRT BLE Central and
GATT client. It has no GUI and is intended for reproducible embedded-device
tests from either Windows PowerShell or WSL2.

The default GAP workflow performs:

```text
targeted active scan
-> inspect the unpaired peer's Association Endpoint permissions
-> create a fresh device from the observed address, with AEP ID fallback
-> request a bounded connection and perform uncached service discovery
-> retry only an unreachable pre-ATT link with freshly closed/recreated objects
-> keep the proven GATT session only for the bounded transaction
-> one uncached characteristic discovery per selected service
-> filter GAP/N13 UUIDs locally and read Device Name
-> release the connection lease and close the Windows BLE objects
-> verify the same target advertises again
```

The client treats successful uncached service discovery—not an `Active`
`GattSession` flag—as proof of a usable RF/ATT link. Windows can otherwise
reuse a stale logical session without creating a new controller connection.
The default is three fresh attempts and is configurable with
`--connect-attempts`. Each failed `Unreachable` attempt closes its device,
waits for a new advertisement and a short scan-to-connect radio transition,
then alternates the freshly observed Association Endpoint ID used by
Microsoft's GATT-client example with a fresh address-resolved object. The
board-proven address form is attempted first; the AEP ID is the first fallback.

For each candidate the client sets `GattSession.MaintainConnection=true`, when
supported, as a bounded connection request. It does not trust the resulting
session status: only successful uncached service discovery proves the RF/ATT
link. The same session is then retained for the multi-operation N13 gate. An
RAII lease clears the flag and closes the session on every normal and
exceptional return path. Discovery uses one full uncached query instead of
repeated per-UUID connection transactions. After device-level access is
granted, the client also requests access on each selected GATT service before
uncached characteristic discovery, matching the Windows GATT sample's
unpaired-device workflow.

The client prints the matching Association Endpoint ID and access state for
diagnosis. Every retry creates a new device/session object; odd attempts use
the address and address type from the advertisement and even attempts use the
freshly observed ID. A timed-out discovery is not retried because Windows
does not currently expose cancellation for the underlying connection process;
stacking another request can leave more queued host work.

With `--n13`, the same bounded process additionally discovers the fixed N13
service and characteristics, reads and validates the 20-byte status frame,
uses write-with-response for an echo request, verifies the matching read and
notification, receives an exact CRC-checked notification burst, disables the
CCC, and proves the link remains usable without another notification. The
default burst is 100 frames and can be reduced with `--n13-burst-count` while
bringing up firmware.

Add `--n13-negative` to inject four isolated malformed control writes before
the valid echo: bad length, magic, version, and CRC. Every write must return a
GATT `ProtocolError`, and the normal echo that follows must still pass. The
result JSON records `negative_executed` and `negative_rejected`; correlate it
with the target's four exact bad-write counters for board-level evidence.

`--n13-cached-discovery` is an explicit, default-off recovery mode available
only together with `--n13-negative`. It first resolves exact cached service
instances by Bluetooth device ID and UUID; on Windows drivers that do not
publish unpaired GATT services as PnP instances, it may use the legacy
single-UUID cache projection. It never falls back to the device-wide async
enumeration that this mode is intended to avoid. The GAP Device Name and
initial N13 status are still read uncached, all malformed writes must receive
real ATT rejection responses, and the valid echo/read/notify gate still has to
pass before a JSON file is created. Depending on the WinRT projection, ATT
errors arrive either as `GattCommunicationStatus::ProtocolError` or the exact
Windows SDK HRESULT (`0x8065000d` for invalid length and `0x8065000e` for the
current NuttX semantic rejects); both are checked explicitly. JSON records
`"discovery_cache":"cached"`. This mode can close the already-frozen N13
negative-input gate, but it cannot be used as fresh service-discovery evidence
or for the formal 20-round lifecycle gate.

`--n13-targeted-discovery` is a separate uncached path. It queries exactly the
GAP and N13 service UUIDs with `GetGattServicesForUuidAsync` instead of asking
Windows to enumerate every service. It cannot be combined with cached mode and
still requires the normal uncached characteristic/read/data gates. On adapters
that support `MaintainConnection`, this mode first waits for a bounded
`GattSessionStatus::Active` transition before sending its first service query;
this prevents a `Closed` logical session from racing discovery. A complete
successful run is live discovery evidence; a Controller connection, session
timeout, or query timeout is not.

Every scan and WinRT operation has a caller-controlled deadline. The process
does not pair, modify the Windows pairing database, install a driver, or remain
resident after the bounded command exits.

## Requirements

- Windows 10/11 with a BLE adapter supporting the Central role
- Windows PowerShell 5.1 or PowerShell 7
- WSL2 Windows executable interop when launched from Linux
- Visual Studio Build Tools with C++ and a Windows SDK only when rebuilding

No NuGet package or network download is used by the build.

## Quick start from WSL2

```bash
./scripts/gatt_client_wsl.sh --probe --build

./scripts/gatt_client_wsl.sh \
  --address c8:47:8c:47:47:48 \
  --name BK7258-N13 \
  --expect-device-name 'BK7258 N13' \
  --n13 \
  --n13-negative \
  --result-file ./logs/n13-gatt.json

# Host-side recovery for N13 negative writes only:
./scripts/gatt_client_wsl.sh \
  --address c8:47:8c:47:47:48 \
  --name BK7258-N13 \
  --expect-device-name 'BK7258 N13' \
  --n13 --n13-negative --n13-cached-discovery \
  --result-file ./logs/n13-negative-cached.json

# Live uncached UUID-targeted discovery (also usable for the negative gate):
./scripts/gatt_client_wsl.sh \
  --address c8:47:8c:47:47:48 \
  --name BK7258-N13 \
  --expect-device-name 'BK7258 N13' \
  --n13 --n13-negative --n13-targeted-discovery \
  --result-file ./logs/n13-negative-targeted.json
```

Use `--scan-only` when only advertisement evidence is required. Use
`--no-rediscover` only when the caller explicitly does not need the
disconnect/re-advertising gate.

## Windows PowerShell

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\gatt_client.ps1 -Probe -Build
.\scripts\gatt_client.ps1 `
  -Address 'c8:47:8c:47:47:48' `
  -Name 'BK7258-N13' `
  -ExpectedDeviceName 'BK7258 N13' `
  -N13 `
  -N13Negative `
  -ResultFile '.\logs\n13-gatt.json'
```

## Evidence contract

Machine-readable console lines use the `BLEGATTC` prefix. A result JSON is
created only after every requested gate passes, and a stale result path is
removed before execution. Host-side scan/connection evidence must still be
correlated with target UART, lifecycle, and error counters before declaring a
firmware stage board-verified. This tool never launches BLEDebug or another
GUI application.

An uncached service index is required for normal board-level GATT discovery
proof. In the narrowly scoped cached negative mode, the cache only supplies
known handles; the uncached reads, ATT error responses, valid echo and target
bad-write counter deltas are the live evidence. UUID-targeted mode is uncached
and may provide discovery proof only when the entire requested gate succeeds.

If all fresh attempts return `Unreachable` while the target is still
discoverable, check the target HCI connection/ACL counters. Zero connection
and ACL deltas mean the request never reached the firmware; do not classify
that host-side stale session as a firmware failure. The retry path does not
pair the peer, restart the adapter, or change Windows device state.

## Exit codes

| Code | Meaning |
|---:|---|
| 0 | Requested probe completed successfully |
| 2 | Invalid arguments or host-side file error |
| 3 | No usable BLE adapter/Central role |
| 4 | Target advertisement not found before deadline |
| 5 | Target could not be resolved after scanning |
| 6 | Windows Runtime failure |
| 7 | GATT service/characteristic/read validation failed |
| 8 | A WinRT operation exceeded its deadline |

The source and scripts are covered by the parent toolkit's Apache-2.0 license.

## API references

- [Microsoft: Bluetooth LE advertisements](https://learn.microsoft.com/windows/apps/develop/devices-sensors/ble-beacon)
- [Microsoft: `BluetoothLEDevice.FromBluetoothAddressAsync`](https://learn.microsoft.com/uwp/api/windows.devices.bluetooth.bluetoothledevice.frombluetoothaddressasync)
- [Microsoft: GATT APIs](https://learn.microsoft.com/uwp/api/windows.devices.bluetooth.genericattributeprofile)
