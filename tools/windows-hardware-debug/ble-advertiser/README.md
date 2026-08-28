# Windows / WSL2 BLE Test Advertiser

This optional companion to the Windows hardware-debug toolkit publishes a
bounded Bluetooth Low Energy manufacturer-data beacon through the **Windows**
Bluetooth stack. It is intended to provide a known, real RF source for embedded
BLE scan tests.

WSL2 normally cannot use the Windows host Bluetooth radio as a Linux HCI device.
The WSL wrapper therefore launches the Windows-native publisher while keeping
the build and test workflow in the Linux terminal.

## Safety and scope

- The tool transmits a public, non-connectable test advertisement for the
  requested duration. Do not put credentials, device secrets, or personal data
  in the payload.
- The default company ID is `0xFFFE`, the unassigned test value used by
  Microsoft's BLE advertisement example. It is not a company identity claim.
- Windows owns advertisement flags and local-name fields. The tool deliberately
  publishes manufacturer data only and uses legacy advertising for broad
  controller compatibility.
- A ready file is cleared at invocation and recreated only after the Windows
  publisher reaches `Started`; callers must also require process exit code 0.
  Neither item alone is RF evidence—the receiving device must still report the
  address, RSSI, and payload.

## Requirements

- Windows 10 version 1703 or later, or Windows 11
- A Bluetooth adapter whose Windows driver supports the BLE peripheral role
- Windows PowerShell 5.1 or PowerShell 7
- To build from source: Visual Studio Build Tools with the C++ workload and a
  Windows 10/11 SDK
- WSL2 Windows executable interop for the Linux wrapper

No NuGet package or network download is used by the build.

## Windows quick start

From PowerShell in this directory:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
./scripts/advertise.ps1 -Probe
./scripts/advertise.ps1 `
  -Payload 'BK7258-N12V' -DurationSec 30 `
  -ReadyFile ./out/n12v.ready.json
```

Force a fresh build with `-Build`. Supply raw bytes with `-PayloadHex`, for
example `-PayloadHex 4e31325601020304`.

## WSL2 quick start

```bash
./scripts/advertise_wsl.sh --probe --build

./scripts/advertise_wsl.sh \
  --payload BK7258-N12V \
  --duration 30 \
  --ready-file /tmp/ble-advertiser.ready.json
```

Expected start evidence:

```text
BLEADV ADAPTER ... low_energy=1 peripheral=1
BLEADV STATUS Started
BLEADV READY status=Started
```

The process remains in the foreground for the bounded duration and calls
`Stop()` before returning. Duration `0` means run until Ctrl+C.

## BK7258 N12-V example

Keep the advertiser running while sending the board command from a second WSL
terminal or serial tool:

```text
bkbttest n12v 10000 15000
```

N12-V passes only when the BK7258 output includes a real advertisement address,
RSSI, and payload matching the test manufacturer data. Seeing `BLEADV READY`
proves that Windows accepted the publication request; it does not replace the
target-side evidence. The dedicated `n12v` command searches all returned
reports for the exact company ID and payload, because Windows can interleave its
own system manufacturer-data advertisement on the same adapter.

Reference hardware validation on 2026-08-02 used payload
`4e31325601020304`. The Windows publisher reached `Started`, and BK7258 reported
two packets; it selected index 1 with `n12v_match=1`, RSSI `-49`, and AD bytes
`0b ff fe ff 4e 31 32 56 01 02 03 04`. The complete N12 evidence is
recorded in the [BK7258 N12 worklog](../../../docs/platforms/bk7258/nuttx-port/n12-beken-bt-ipc-wrapper.md).

## Exit codes

| Code | Meaning |
|---:|---|
| 0 | Probe or bounded advertisement completed successfully |
| 2 | Invalid command-line input |
| 3 | No usable BLE adapter or no peripheral-role support |
| 4 | Publisher failed, aborted, or did not reach `Started` in time |
| 6 | Windows Runtime failure |

The source and scripts are covered by the parent toolkit's Apache-2.0 license.

## API references

- [Microsoft: Bluetooth LE advertisements](https://learn.microsoft.com/windows/apps/develop/devices-sensors/ble-beacon)
- [Microsoft: `BluetoothLEAdvertisementPublisher`](https://learn.microsoft.com/uwp/api/windows.devices.bluetooth.advertisement.bluetoothleadvertisementpublisher)
- [Microsoft: `BluetoothAdapter.IsPeripheralRoleSupported`](https://learn.microsoft.com/uwp/api/windows.devices.bluetooth.bluetoothadapter.isperipheralrolesupported)
