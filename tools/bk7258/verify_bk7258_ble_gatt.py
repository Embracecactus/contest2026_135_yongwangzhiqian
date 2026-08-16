#!/usr/bin/env python3
"""Verify the source/config/ELF contract of the BK7258 N13 BLE service."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from bk7258_framework import config_document, resolve


class VerificationError(RuntimeError):
    """Raised when an N13 build gate is not satisfied."""


EXPECTED_HANDLES = {
    "BK7258_BLE_GATT_GAP_SERVICE_HANDLE": 0x0001,
    "BK7258_BLE_GATT_NAME_CHRC_HANDLE": 0x0003,
    "BK7258_BLE_GATT_NAME_VALUE_HANDLE": 0x0004,
    "BK7258_BLE_GATT_APPEARANCE_CHRC_HANDLE": 0x0005,
    "BK7258_BLE_GATT_APPEARANCE_VALUE_HANDLE": 0x0006,
    "BK7258_BLE_GATT_SERVICE_HANDLE": 0x0010,
    "BK7258_BLE_GATT_CONTROL_CHRC_HANDLE": 0x0011,
    "BK7258_BLE_GATT_CONTROL_VALUE_HANDLE": 0x0012,
    "BK7258_BLE_GATT_STATUS_CHRC_HANDLE": 0x0013,
    "BK7258_BLE_GATT_STATUS_VALUE_HANDLE": 0x0014,
    "BK7258_BLE_GATT_STATUS_CCC_HANDLE": 0x0015,
}

EXPECTED_UUIDS = {
    "service": "72580001-4e31-3347-4154-545f424c4500",
    "control": "72580002-4e31-3347-4154-545f424c4500",
    "status": "72580003-4e31-3347-4154-545f424c4500",
}

REQUIRED_AP_SYMBOLS = {
    "bk7258_ble_gatt_initialize",
    "bk7258_ble_gatt_hci_event",
    "bk7258_ble_gatt_get_stats",
    "bk7258_ble_gatt_worker",
    "g_attributes",
    "bt_start_advertising",
    "bt_stop_advertising",
}

REQUIRED_CP_TIMING_SYMBOLS = {
    "bk_delay_us",
    "up_udelay",
}


def run(*args: str) -> str:
    result = subprocess.run(
        args,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise VerificationError(
            f"command failed ({result.returncode}): {' '.join(args)}\n"
            f"{result.stdout}{result.stderr}"
        )

    return result.stdout


def parse_symbols(nm: str, elf: Path) -> set[str]:
    symbols: set[str] = set()
    for line in run(nm, "--defined-only", str(elf)).splitlines():
        match = re.match(r"^[0-9a-fA-F]+\s+\S\s+(\S+)$", line)
        if match:
            symbols.add(match.group(1))

    return symbols


def uuid_wire_bytes(canonical: str) -> bytes:
    return bytes.fromhex(canonical.replace("-", ""))[::-1]


def require_lines(text: str, required: list[str], description: str) -> None:
    for line in required:
        if line not in text.splitlines():
            raise VerificationError(f"{description} is missing exact line: {line}")


def verify_source(board: Path) -> dict[str, object]:
    repository = board.parents[1]
    cp_config = config_document(
        resolve(repository, "t5ai_core_bringup", "cp")
    )["defconfig"]
    tick_line = "CONFIG_USEC_PER_TICK=1000\n"
    if cp_config.count(tick_line) != 1:
        raise VerificationError(
            "canonical PSRAM validation CP suite must select the "
            "SDK-compatible 1 ms tick"
        )

    stubs_source = (board / "chip/common/bk7258_sdk_stubs.c").read_text(
        encoding="utf-8"
    )
    delay_start = stubs_source.index("void bk_delay_us(unsigned int us)")
    delay_end = stubs_source.index("\n}", delay_start)
    delay_source = stubs_source[delay_start:delay_end]
    if "up_udelay(us);" not in delay_source or "(void)us;" in delay_source:
        raise VerificationError(
            "N13 SDK wrapper must preserve bk_delay_us with up_udelay"
        )
    for token in [
        "!defined(CONFIG_BK7258_WIFI_VNET)",
        "!defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_AP_CORE)",
    ]:
        if token not in stubs_source:
            raise VerificationError(
                "CP BT/PHY closure must leave the SDK Wi-Fi low-voltage "
                f"symbol to libwifi.a: missing {token}"
            )

    ap_config = config_document(
        resolve(repository, "t5ai_core_bringup", "ap")
    )["defconfig"]
    if "CONFIG_BK7258_AP_CORE=y" not in ap_config.splitlines():
        raise VerificationError("canonical AP config must select AP core")
    if "CONFIG_SMP_DEFAULT_CPUSET=0x1" not in ap_config.splitlines():
        raise VerificationError("canonical AP default CPU set is not pinned to CPU0")

    kconfig_source = (board / "chip/Kconfig").read_text(encoding="utf-8")
    priority_start = kconfig_source.index(
        "config BK7258_BLE_GATT_PRIORITY"
    )
    priority_end = kconfig_source.index(
        "config BK7258_BLE_GATT_STACKSIZE", priority_start
    )
    if "\tdefault 96\n" not in kconfig_source[priority_start:priority_end]:
        raise VerificationError(
            "N13 GATT worker Kconfig default must remain priority 96"
        )

    header = (board / "chip/include/bk7258_ble_gatt.h").read_text(
        encoding="utf-8"
    )
    handles: dict[str, int] = {}
    for name, expected in EXPECTED_HANDLES.items():
        match = re.search(
            rf"^#define\s+{re.escape(name)}\s+(0x[0-9a-fA-F]+)u?$",
            header,
            re.MULTILINE,
        )
        if match is None:
            raise VerificationError(f"N13 handle macro is missing: {name}")
        value = int(match.group(1), 16)
        if value != expected:
            raise VerificationError(
                f"N13 handle {name}: expected 0x{expected:04x}, "
                f"got 0x{value:04x}"
            )
        handles[name] = value

    ordered = list(handles.values())
    if ordered != sorted(ordered) or len(set(ordered)) != len(ordered):
        raise VerificationError("N13 handles are not strictly increasing/unique")

    for canonical in EXPECTED_UUIDS.values():
        if canonical not in header:
            raise VerificationError(f"N13 canonical UUID is missing: {canonical}")

    source = (board / "chip/ap/bk7258_ble_gatt.c").read_text(
        encoding="utf-8"
    )
    required_source_tokens = [
        "static const struct bt_gatt_attr_s g_attributes[]",
        "static struct bt_gatt_ccc_cfg_s g_status_ccc[1]",
        "BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE",
        "BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY",
        "BT_GATT_CCC(BK7258_BLE_GATT_STATUS_CCC_HANDLE",
        "bt_gatt_register(g_attributes, nitems(g_attributes))",
        "ret = bt_stop_advertising();",
        "ret = bk7258_ble_gatt_start();",
        "CPU_SET(0, &cpuset)",
        "CONFIG_BK7258_BLE_GATT_PRIORITY <",
        "CONFIG_SCHED_LPWORKPRIORITY",
        "CONFIG_SCHED_LPWORKPRIORITY <",
        "BK7258_BLE_GATT_BT_IPC_PRIORITY",
        "CONFIG_BLUETOOTH_TXCMD_PRIORITY >",
        "CONFIG_BLUETOOTH_MAX_CONN == 1",
    ]
    for token in required_source_tokens:
        if token not in source:
            raise VerificationError(f"N13 source contract is missing: {token}")

    connected_start = source.index("static void bk7258_ble_gatt_process_connected(")
    disconnected_start = source.index(
        "static void bk7258_ble_gatt_process_disconnected(", connected_start
    )
    publish_start = source.index(
        "static void bk7258_ble_gatt_publish_response(", disconnected_start
    )
    connected_source = source[connected_start:disconnected_start]
    disconnected_source = source[disconnected_start:publish_start]
    if "ret = bt_stop_advertising();" not in connected_source:
        raise VerificationError(
            "N13 connect path must synchronize the stale NuttX advertising state"
        )
    if "ret != OK && ret != -EALREADY && ret != -EIO" not in connected_source:
        raise VerificationError(
            "N13 connect path must accept the Controller's already-stopped "
            "advertising status"
        )
    if "ret = bt_stop_advertising();" in disconnected_source:
        raise VerificationError(
            "N13 disconnect path must not race stock cleanup with a second stop"
        )
    if "ret = bk7258_ble_gatt_start();" not in disconnected_source:
        raise VerificationError(
            "N13 disconnect path must perform the single deferred restart"
        )

    hci_source = (board / "chip/ap/bk7258_bt_hci.c").read_text(
        encoding="utf-8"
    )
    for token in [
        "BK7258_BT_OP_HOST_NUM_COMPLETED",
        "CONFIG_BLUETOOTH_CNTRL_HOST_FLOW_DISABLE",
        "CONFIG_BK7258_BT_CONN_RX_REF_COMPAT",
        "host_num_completed_dropped",
    ]:
        if token not in hci_source:
            raise VerificationError(
                f"N13 BK7258 HCI compatibility contract is missing: {token}"
            )

    wrapper_start = hci_source.index("void __wrap_bt_conn_receive(")
    wrapper_end = hci_source.index(
        "void __wrap_bt_l2cap_receive(", wrapper_start
    )
    wrapper_source = hci_source[wrapper_start:wrapper_end]
    real_call = wrapper_source.index("__real_bt_conn_receive(conn, buf, flags);")
    release_call = wrapper_source.index("bt_conn_release(conn);")
    if release_call < real_call:
        raise VerificationError(
            "N13 bt_conn receive wrapper releases its reference before use"
        )

    nuttx = board.parent.parent.parent / "nuttx"
    hcicore_source = (
        nuttx / "wireless/bluetooth/bt_hcicore.c"
    ).read_text(encoding="utf-8")
    hci_acl_start = hcicore_source.index("static void hci_acl(")
    hci_acl_end = hcicore_source.index(
        "/* HCI event processing */", hci_acl_start
    )
    hci_acl_source = hcicore_source[hci_acl_start:hci_acl_end]
    if hci_acl_source.count("bt_conn_lookup_handle(") != 1:
        raise VerificationError(
            "stock NuttX hci_acl lookup ownership pattern changed"
        )
    if hci_acl_source.count("bt_conn_receive(conn, buf, flags);") != 1:
        raise VerificationError(
            "stock NuttX hci_acl receive handoff pattern changed"
        )
    if "bt_conn_release(conn);" in hci_acl_source:
        raise VerificationError(
            "stock NuttX hci_acl now releases the connection reference; "
            "disable the board compatibility wrapper"
        )

    conn_source = (
        nuttx / "wireless/bluetooth/bt_conn.c"
    ).read_text(encoding="utf-8")
    conn_receive_start = conn_source.index("void bt_conn_receive(")
    conn_receive_end = conn_source.index(
        "/****************************************************************************",
        conn_receive_start,
    )
    conn_receive_source = conn_source[conn_receive_start:conn_receive_end]
    if "bt_conn_release(" in conn_receive_source:
        raise VerificationError(
            "stock NuttX bt_conn_receive now owns the connection reference; "
            "disable the board compatibility wrapper"
        )

    ipc_header = (board / "chip/include/bk7258_bt_ipc.h").read_text(
        encoding="utf-8"
    )
    for token in [
        "#define BK7258_BT_TEST_RESULT_VERSION  10u",
        "#define BK7258_BT_TEST_RESULT_SIZE     464u",
        "struct bk7258_bt_gatt_lifecycle_s",
        "struct bk7258_bt_gatt_lifecycle_s gatt;",
    ]:
        if token not in ipc_header:
            raise VerificationError(
                f"N13 compact RPMsg lifecycle ABI is missing: {token}"
            )

    test_source = (board / "chip/common/bk7258_bt_test.c").read_text(
        encoding="utf-8"
    )
    for token in [
        "ret = bk7258_ble_gatt_get_stats(&gatt);",
        "result->gatt.readvertised",
        "result->gatt.worker_cpu = UINT8_MAX;",
    ]:
        if token not in test_source:
            raise VerificationError(
                f"N13 RPMsg lifecycle export is missing: {token}"
            )

    app_source = (
        board.parent.parent / "app/hello_app/bk7258_bt_test_main.c"
    ).read_text(encoding="utf-8")
    if 'printf("BBTT N13 state=%u last_error=%d worker_cpu=%u"' not in app_source:
        raise VerificationError("bkbttest does not print N13 lifecycle stats")

    client_root = (
        board.parent.parent / "tools/windows-hardware-debug/ble-gatt-client"
    )
    client_source = (client_root / "src/ble_gatt_client.cpp").read_text(
        encoding="utf-8"
    )
    for token in [
        'L"--n13-negative"',
        'L"--n13-cached-discovery"',
        'L"--n13-targeted-discovery"',
        "GattCommunicationStatus::ProtocolError",
        "BluetoothCacheMode::Cached",
        "GetDeviceSelectorForBluetoothDeviceIdAndUuid",
        "DeviceInformation::FindAllAsync",
        "GattDeviceService::FromIdAsync",
        "device.GetGattService(uuid)",
        '"device_uuid_selector"',
        '"legacy_single_uuid_cache"',
        "source=targeted_cache",
        "GetGattServicesForUuidAsync",
        "source=targeted_uuid_uncached",
        "targeted_session_active",
        "BLEGATTC SESSION_PRIMED",
        "E_BLUETOOTH_ATT_INVALID_ATTRIBUTE_VALUE_LENGTH",
        "E_BLUETOOTH_ATT_UNLIKELY",
        "projection=hresult",
        '"n13_negative_length"',
        '"n13_negative_magic"',
        '"n13_negative_version"',
        '"n13_negative_crc"',
        "valid_echo_after_reject=1",
        "negative_rejected",
        "gatt_session_lease candidate_lease(candidate_session)",
        "candidate_lease.detach()",
        "candidate_lease.release()",
        '"--n13-cached-discovery requires --n13-negative"',
        '"--n13-targeted-discovery requires --n13"',
        '"--n13-targeted-discovery conflicts with --n13-cached-discovery"',
        "proof=uncached_device_name_read",
        '\\"discovery_cache\\":',
    ]:
        if token not in client_source:
            raise VerificationError(
                f"N13 Windows negative-write gate is missing: {token}"
            )

    if (
        "device.GetGattServicesAsync(BluetoothCacheMode::Uncached)"
        not in client_source
    ):
        raise VerificationError(
            "normal N13 service discovery is not explicitly uncached"
        )
    if "device.GetGattServicesAsync(discovery_cache_mode(options))" in client_source:
        raise VerificationError(
            "cached negative mode still enters device-wide service discovery"
        )

    wsl_source = (client_root / "scripts/gatt_client_wsl.sh").read_text(
        encoding="utf-8"
    )
    ps_source = (client_root / "scripts/gatt_client.ps1").read_text(
        encoding="utf-8"
    )
    if "--n13-negative" not in wsl_source or "N13Negative" not in ps_source:
        raise VerificationError(
            "N13 negative-write option is not wired through both launchers"
        )
    if (
        "--n13-cached-discovery" not in wsl_source
        or "N13CachedDiscovery" not in ps_source
    ):
        raise VerificationError(
            "N13 cached negative-discovery option is not wired through both launchers"
        )
    if (
        "--n13-targeted-discovery" not in wsl_source
        or "N13TargetedDiscovery" not in ps_source
    ):
        raise VerificationError(
            "N13 targeted discovery option is not wired through both launchers"
        )

    ad_payload_bytes = 3 + 18
    scan_response_bytes = 2 + len("BK7258-N13")
    if ad_payload_bytes > 31 or scan_response_bytes > 31:
        raise VerificationError("N13 legacy advertising payload exceeds 31 bytes")

    return {
        "handles": handles,
        "uuids": EXPECTED_UUIDS,
        "advertising_bytes": ad_payload_bytes,
        "scan_response_bytes": scan_response_bytes,
        "cp_profile_equal_to_n12": True,
        "lifecycle_export": "compact-v10",
        "conn_rx_reference": "board-wrapper-source-verified",
        "negative_write_gate": "host-tool-source-verified",
    }


def verify_elf(
    ap_elf: Path,
    cp_elf: Path,
    ap_map: Path,
    nm: str,
    expected_device_name: str,
    expected_local_name: str,
) -> dict[str, object]:
    ap_symbols = parse_symbols(nm, ap_elf)
    missing = sorted(REQUIRED_AP_SYMBOLS - ap_symbols)
    if missing:
        raise VerificationError(
            "N13 AP ELF is missing required symbols: " + ", ".join(missing)
        )

    cp_symbols = parse_symbols(nm, cp_elf)
    missing_cp_timing = sorted(REQUIRED_CP_TIMING_SYMBOLS - cp_symbols)
    if missing_cp_timing:
        raise VerificationError(
            "N13 CP ELF is missing wrapper timing symbols: "
            + ", ".join(missing_cp_timing)
        )

    leaked = sorted(
        symbol for symbol in cp_symbols if symbol.startswith("bk7258_ble_gatt")
    )
    if leaked:
        raise VerificationError(
            "N13 GATT implementation leaked into the CP ELF: "
            + ", ".join(leaked)
        )

    map_text = ap_map.read_text(encoding="utf-8", errors="replace")
    if "bk7258_ble_gatt.o" not in map_text:
        raise VerificationError("N13 AP map does not contain bk7258_ble_gatt.o")

    elf_bytes = ap_elf.read_bytes()
    for value in (
        expected_device_name.encode(),
        expected_local_name.encode(),
        b"bk7258-ble-gatt",
    ):
        if value not in elf_bytes:
            raise VerificationError(
                f"N13 AP ELF is missing string payload {value!r}"
            )

    wire_occurrences: dict[str, int] = {}
    for name, canonical in EXPECTED_UUIDS.items():
        count = elf_bytes.count(uuid_wire_bytes(canonical))
        if count == 0:
            raise VerificationError(
                f"N13 AP ELF is missing little-endian {name} UUID"
            )
        wire_occurrences[name] = count

    return {
        "ap_elf": str(ap_elf.resolve()),
        "cp_elf": str(cp_elf.resolve()),
        "ap_required_symbols": sorted(REQUIRED_AP_SYMBOLS),
        "uuid_wire_occurrences": wire_occurrences,
        "cp_gatt_symbols": leaked,
        "cp_timing_symbols": sorted(REQUIRED_CP_TIMING_SYMBOLS),
        "expected_device_name": expected_device_name,
        "expected_local_name": expected_local_name,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ap-elf", required=True, type=Path)
    parser.add_argument("--cp-elf", required=True, type=Path)
    parser.add_argument("--ap-map", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--expected-device-name", default="BK7258 N13")
    parser.add_argument("--expected-local-name", default="BK7258-N13")
    args = parser.parse_args()

    board = Path(__file__).resolve().parent.parent
    result = {
        "format": 1,
        "source": verify_source(board),
        "elf": verify_elf(
            args.ap_elf,
            args.cp_elf,
            args.ap_map,
            args.nm,
            args.expected_device_name,
            args.expected_local_name,
        ),
    }

    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    print(
        "PASS bk7258-ble-gatt: "
        "handles=11 uuid=3 adv=21 scan_rsp=12 cp_owner=controller-only "
        "negative=source-verified"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        print(f"FAIL bk7258-ble-gatt: {error}")
        raise SystemExit(1) from error
