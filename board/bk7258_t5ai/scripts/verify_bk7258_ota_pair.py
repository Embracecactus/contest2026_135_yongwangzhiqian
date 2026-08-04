#!/usr/bin/env python3
"""Fail-closed verifier and negative-test suite for BK7258 N15-A bundles."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import tempfile
from pathlib import Path
from typing import Callable

from bk7258_ab_layout import LAYOUT_ID, LayoutError, report as layout_report
from bk7258_crc_expand import ExpansionError, decode, expand
from inspect_bk7258_rbl import (
    VerificationError as RblVerificationError,
    inspect as inspect_rbl,
)
from pack_bk7258_ota_pair import (
    AP_FILE,
    AP_MAX_BODY_SIZE,
    AP_XIP_START,
    BODY_FILE,
    CP_FILE,
    CP_XIP_START,
    MANIFEST_FILE,
    OFFICIAL_SOURCE_HASHES,
    PAIR_SCHEMA,
    RBL_FILE,
    RBL_HEADER_OFFSET,
    S_APP_FILE,
    SDK_RELEASE,
    PairError,
    build_bundle,
    parse_int,
    validate_generation,
    validate_timestamp,
    validate_version,
    write_bundle,
)


class PairVerificationError(RuntimeError):
    """Raised when a bundle is not the canonical deterministic pair."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise PairVerificationError(message)


def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise PairVerificationError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_manifest(path: Path) -> tuple[bytes, dict[str, object]]:
    raw = path.read_bytes()
    try:
        parsed = json.loads(raw.decode("utf-8"), object_pairs_hook=reject_duplicate_keys)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise PairVerificationError(f"invalid pair manifest: {error}") from error
    require(isinstance(parsed, dict), "pair manifest root must be an object")
    return raw, parsed


def read_file(bundle: Path, name: str) -> bytes:
    path = bundle / name
    try:
        return path.read_bytes()
    except OSError as error:
        raise PairVerificationError(f"cannot read {path}: {error}") from error


def verify_official_sdk(source: Path) -> dict[str, object]:
    if source.name != "bk_avdk_smp-release-v3.1.1.9":
        raise PairVerificationError(
            "SDK source must be the exact v3.1.1.9 release directory"
        )
    try:
        layout_report(source)
    except (LayoutError, OSError, ValueError) as error:
        raise PairVerificationError(str(error)) from error

    verified: list[dict[str, str]] = []
    for relative, expected in sorted(OFFICIAL_SOURCE_HASHES.items()):
        path = source / relative
        try:
            observed = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError as error:
            raise PairVerificationError(f"cannot read official input {path}") from error
        require(
            observed == expected,
            f"official v3.1.1.9 source hash drift: {relative}",
        )
        verified.append({"path": relative, "sha256": observed})

    config_path = source / "projects/app_ab/partitions/bk7258/ota_rbl.config"
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PairVerificationError("cannot parse official RBL config") from error
    require(config.get("gzip") == "0", "official RBL gzip policy drift")
    require(config.get("aes") == "0", "official RBL AES policy drift")
    return {
        "release": SDK_RELEASE,
        "source_hashes": verified,
        "algorithm": 0,
    }


def verify_bundle(
    bundle: Path,
    *,
    expected_generation: int | None = None,
    expected_version: str | None = None,
    expected_base_version: str | None = None,
    expected_timestamp: int | None = None,
    sdk_source: Path | None = None,
) -> dict[str, object]:
    manifest_raw, manifest = load_manifest(bundle / MANIFEST_FILE)
    require(manifest.get("schema") == PAIR_SCHEMA, "pair schema drift")
    require(manifest.get("layout_id") == LAYOUT_ID, "pair layout ID drift")

    generation = validate_generation(manifest.get("generation"))
    version = validate_version(manifest.get("version"), "version")
    base_version = validate_version(manifest.get("base_version"), "base_version")
    timestamp = validate_timestamp(manifest.get("timestamp"))

    if expected_generation is not None:
        require(generation == expected_generation, "unexpected pair generation")
    if expected_version is not None:
        require(version == expected_version, "unexpected candidate version")
    if expected_base_version is not None:
        require(base_version == expected_base_version, "unexpected base version")
    if expected_timestamp is not None:
        require(timestamp == expected_timestamp, "unexpected RBL timestamp")

    cp = read_file(bundle, CP_FILE)
    ap = read_file(bundle, AP_FILE)
    body = read_file(bundle, BODY_FILE)
    logical_rbl = read_file(bundle, RBL_FILE)
    physical_rbl = read_file(bundle, S_APP_FILE)

    try:
        decoded = decode(physical_rbl)
    except ExpansionError as error:
        raise PairVerificationError(str(error)) from error
    require(decoded == logical_rbl, "encoded s_app does not decode to pair.rbl")
    try:
        rbl_report = inspect_rbl(logical_rbl, "ab", allow_encoded=False)
    except RblVerificationError as error:
        raise PairVerificationError(str(error)) from error

    try:
        expected_files, expected_manifest = build_bundle(
            cp,
            ap,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
        )
    except (PairError, RblVerificationError, ValueError) as error:
        raise PairVerificationError(str(error)) from error

    actual_files = {
        CP_FILE: cp,
        AP_FILE: ap,
        BODY_FILE: body,
        RBL_FILE: logical_rbl,
        S_APP_FILE: physical_rbl,
        MANIFEST_FILE: manifest_raw,
    }
    for name, expected in expected_files.items():
        require(actual_files[name] == expected, f"non-canonical bundle file: {name}")
    require(manifest == expected_manifest, "pair manifest fields are non-canonical")

    source_report = verify_official_sdk(sdk_source) if sdk_source else None
    return {
        "format": 1,
        "status": "pass",
        "schema": PAIR_SCHEMA,
        "layout_id": LAYOUT_ID,
        "bundle": str(bundle.resolve()),
        "generation": generation,
        "version": version,
        "base_version": base_version,
        "timestamp": timestamp,
        "body_size": rbl_report["body_size"],
        "rbl_header_offset": rbl_report["header_offset"],
        "s_app_sha256": hashlib.sha256(physical_rbl).hexdigest(),
        "integrity_only": True,
        "publisher_authenticated": False,
        "anti_rollback": False,
        "writes_enabled": False,
        "official_sdk": source_report,
    }


def synthetic_component(role: str) -> bytes:
    if role == "cp":
        payload = bytearray((index * 13 + 7) & 0xFF for index in range(0x280))
        struct.pack_into("<II", payload, 0, 0x28002000, CP_XIP_START + 0x121)
        payload[0x100:0x108] = b"BK7236\0\0"
        return bytes(payload)
    payload = bytearray((index * 17 + 3) & 0xFF for index in range(0x240))
    struct.pack_into("<II", payload, 0, 0x28052000, AP_XIP_START + 0x101)
    return bytes(payload)


def rewrite_manifest(bundle: Path, mutate: Callable[[dict[str, object]], None]) -> None:
    _, manifest = load_manifest(bundle / MANIFEST_FILE)
    mutate(manifest)
    (bundle / MANIFEST_FILE).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def mutate_byte(path: Path, offset: int) -> None:
    payload = bytearray(path.read_bytes())
    payload[offset] ^= 1
    path.write_bytes(payload)


def expect_failure(name: str, action: Callable[[], object]) -> None:
    try:
        action()
    except (PairError, PairVerificationError, RblVerificationError, ValueError):
        return
    raise PairVerificationError(f"negative test unexpectedly passed: {name}")


def self_test(sdk_source: Path | None) -> dict[str, object]:
    generation = 7
    version = "2.0.0-test"
    base_version = "1.0.0"
    timestamp = 0x12345678
    cp = synthetic_component("cp")
    ap = synthetic_component("ap")
    negative_count = 0

    # Independent output from official v3.1.1.9 bk_crc16.crc16_data().
    official_crc_packet = bytes.fromhex(
        "000102030405060708090a0b0c0d0e0f"
        "101112131415161718191a1b1c1d1e1f259d"
    )
    require(
        expand(bytes(range(32))) == official_crc_packet,
        "official v3.1.1.9 CRC expansion vector drift",
    )

    with tempfile.TemporaryDirectory(prefix="bk7258-n15a-") as directory:
        root = Path(directory)
        first = root / "first"
        second = root / "second"
        first_files, _ = build_bundle(
            cp,
            ap,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
        )
        second_files, _ = build_bundle(
            cp,
            ap,
            generation=generation,
            version=version,
            base_version=base_version,
            timestamp=timestamp,
        )
        write_bundle(first, first_files)
        write_bundle(second, second_files)
        require(first_files == second_files, "two deterministic builds differ")
        verify_bundle(
            first,
            expected_generation=generation,
            expected_version=version,
            expected_base_version=base_version,
            expected_timestamp=timestamp,
            sdk_source=sdk_source,
        )
        verify_bundle(
            second,
            expected_generation=generation,
            expected_version=version,
            expected_base_version=base_version,
            expected_timestamp=timestamp,
            sdk_source=sdk_source,
        )

        def copied_case(name: str) -> Path:
            destination = root / name
            shutil.copytree(first, destination)
            return destination

        cases: list[tuple[str, Callable[[], object]]] = []

        physical_bad = copied_case("physical-corruption")
        mutate_byte(physical_bad / S_APP_FILE, 1)
        cases.append(("physical corruption", lambda: verify_bundle(physical_bad)))

        header_bad = copied_case("header-corruption")
        mutate_byte(header_bad / RBL_FILE, RBL_HEADER_OFFSET + 4)
        cases.append(("RBL header corruption", lambda: verify_bundle(header_bad)))

        body_bad = copied_case("body-corruption")
        mutate_byte(body_bad / BODY_FILE, 8)
        cases.append(("body corruption", lambda: verify_bundle(body_bad)))

        address_bad = copied_case("address")
        rewrite_manifest(
            address_bad,
            lambda value: value["pair"].update(
                {"physical_offset": value["pair"]["physical_offset"] + 0x1000}
            ),
        )
        cases.append(("address drift", lambda: verify_bundle(address_bad)))

        size_bad = copied_case("size")
        rewrite_manifest(
            size_bad,
            lambda value: value["rbl"]["logical_container"].update(
                {"length": value["rbl"]["logical_container"]["length"] - 1}
            ),
        )
        cases.append(("size drift", lambda: verify_bundle(size_bad)))

        layout_bad = copied_case("layout")
        rewrite_manifest(
            layout_bad, lambda value: value.update({"layout_id": "old-layout"})
        )
        cases.append(("layout drift", lambda: verify_bundle(layout_bad)))

        version_bad = copied_case("version")
        rewrite_manifest(
            version_bad, lambda value: value.update({"version": "bad version"})
        )
        cases.append(("version policy", lambda: verify_bundle(version_bad)))

        generation_bad = copied_case("generation")
        rewrite_manifest(
            generation_bad,
            lambda value: value["components"][1].update(
                {"generation": value["generation"] + 1}
            ),
        )
        cases.append(("mixed generation", lambda: verify_bundle(generation_bad)))

        reset_bad = copied_case("reset-address")
        reset_payload = bytearray((reset_bad / CP_FILE).read_bytes())
        struct.pack_into("<I", reset_payload, 4, 0x02000001)
        (reset_bad / CP_FILE).write_bytes(reset_payload)
        cases.append(("reset address", lambda: verify_bundle(reset_bad)))

        cases.append(
            (
                "expected version mismatch",
                lambda: verify_bundle(first, expected_version="9.9.9"),
            )
        )
        cases.append(
            (
                "expected generation mismatch",
                lambda: verify_bundle(first, expected_generation=generation + 1),
            )
        )
        cases.append(
            (
                "AP size overflow",
                lambda: build_bundle(
                    cp,
                    ap + b"\xff" * (AP_MAX_BODY_SIZE + 1 - len(ap)),
                    generation=generation,
                    version=version,
                    base_version=base_version,
                    timestamp=timestamp,
                ),
            )
        )

        duplicate_bad = copied_case("duplicate-json")
        (duplicate_bad / MANIFEST_FILE).write_text(
            '{"schema":"one","schema":"two"}\n', encoding="utf-8"
        )
        cases.append(("duplicate JSON key", lambda: verify_bundle(duplicate_bad)))

        for name, action in cases:
            expect_failure(name, action)
            negative_count += 1

    return {
        "status": "pass",
        "positive_cases": 2,
        "negative_cases": negative_count,
        "sdk_source_verified": sdk_source is not None,
        "writes_enabled": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--expected-generation", type=parse_int)
    parser.add_argument("--expected-version")
    parser.add_argument("--expected-base-version")
    parser.add_argument("--expected-timestamp", type=parse_int)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        if args.self_test:
            result = self_test(args.sdk_source)
        else:
            if args.bundle is None:
                parser.error("--bundle is required unless --self-test is used")
            if (
                args.expected_generation is None
                or args.expected_version is None
                or args.expected_base_version is None
                or args.expected_timestamp is None
            ):
                parser.error(
                    "bundle verification requires all --expected-* identity fields"
                )
            result = verify_bundle(
                args.bundle,
                expected_generation=args.expected_generation,
                expected_version=args.expected_version,
                expected_base_version=args.expected_base_version,
                expected_timestamp=args.expected_timestamp,
                sdk_source=args.sdk_source,
            )
    except (
        OSError,
        PairError,
        PairVerificationError,
        RblVerificationError,
        ExpansionError,
        ValueError,
    ) as error:
        print(f"BK7258 N15-A pair verification FAIL: {error}")
        return 1

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    elif args.self_test:
        print(
            "BK7258 N15-A pair self-test PASS: "
            f"positive={result['positive_cases']} "
            f"negative={result['negative_cases']} writes_enabled=false"
        )
    else:
        print(
            "BK7258 N15-A pair verification PASS: "
            f"generation={result['generation']} version={result['version']} "
            "writes_enabled=false"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
