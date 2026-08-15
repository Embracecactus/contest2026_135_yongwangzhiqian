#!/usr/bin/env python3
"""Build and verify the payload-bearing BK7258 ``firmware.bkpack``.

This ZIP-compatible file is a small Beken delivery extension, not an openvela
standard artifact.  It wraps an already built and verified dual-image
directory and adds a Windows manual-download plan.  It never compiles, signs,
or accesses hardware.  Its hashes provide deterministic integrity; a real
MCUboot download must still pass the package's target public-root preflight.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from bk7258_trust_chain import (
    TrustChainError,
    load_contract,
    verify_contract_artifacts,
)
from verify_bk7258_factory_layout import (
    VerificationError,
    verify as verify_factory_layout,
)


SCHEMA = "bk7258.payload-bkpack/1"
MANIFEST_MEMBER = "bkpack-manifest.json"
WINDOWS_FLASH_GUIDE = "WINDOWS_FLASH.txt"
DUAL_MANIFEST = "bk7258-dual-image.json"
BUILD_PROFILE = "build-profile.txt"
MAX_MEMBERS = 128
MAX_MEMBER_SIZE = 16 * 1024 * 1024
MAX_TOTAL_SIZE = 32 * 1024 * 1024
MAX_ARCHIVE_SIZE = 40 * 1024 * 1024
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
STANDARD_ALIASES = {
    "vela_cp.bin": "cp-raw.bin",
    "vela_ap.bin": "ap-raw.bin",
}
OPTIONAL_METADATA = (
    "bk7258-factory-layout.json",
    "bk7258-partitions.json",
    "bk7258-rptun-layout.json",
    "mcuboot_pair.json",
    "secureboot-pipeline.json",
    "nuttx-cp.config",
    "nuttx-ap.config",
)


class BkpackError(RuntimeError):
    """A malformed, incomplete, or unsafe BK7258 container."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise BkpackError(message)


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _json_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise BkpackError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _decode_json(payload: bytes, description: str) -> dict[str, Any]:
    try:
        value = json.loads(
            payload.decode("utf-8"), object_pairs_hook=_json_pairs
        )
    except (UnicodeError, json.JSONDecodeError) as error:
        raise BkpackError(f"invalid {description}: {error}") from error
    _require(isinstance(value, dict), f"{description} must be an object")
    return value


def _canonical_json(value: dict[str, Any]) -> bytes:
    return (
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
        + "\n"
    ).encode("utf-8")


def _safe_name(value: Any, description: str = "member") -> str:
    _require(isinstance(value, str) and value and "\x00" not in value,
             f"invalid {description}")
    _require("\\" not in value and not value.startswith("/"),
             f"unsafe {description}: {value!r}")
    path = PurePosixPath(value)
    _require(not path.is_absolute() and ".." not in path.parts,
             f"unsafe {description}: {value!r}")
    # The first format deliberately keeps every payload at the archive root.
    _require(len(path.parts) == 1 and path.name == value,
             f"nested {description} is not supported: {value!r}")
    return value


def _regular_file(root: Path, name: str) -> Path:
    _safe_name(name, "source member")
    path = root / name
    try:
        info = path.lstat()
    except OSError as error:
        raise BkpackError(f"missing source member {name}: {error}") from error
    _require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode),
             f"source member is not a regular file: {name}")
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise BkpackError(f"source member escapes package: {name}") from error
    _require(info.st_size <= MAX_MEMBER_SIZE,
             f"source member is too large: {name}")
    return path


def _read_regular(root: Path, name: str) -> bytes:
    path = _regular_file(root, name)
    try:
        return path.read_bytes()
    except OSError as error:
        raise BkpackError(f"cannot read source member {name}: {error}") from error


def _profile(root: Path) -> tuple[dict[str, str], bytes]:
    payload = _read_regular(root, BUILD_PROFILE)
    try:
        lines = payload.decode("utf-8").splitlines()
    except UnicodeError as error:
        raise BkpackError("build-profile.txt is not UTF-8") from error
    result: dict[str, str] = {}
    for line in lines:
        if not line or line.startswith("#"):
            continue
        _require("=" in line, "malformed build-profile.txt line")
        key, value = line.split("=", 1)
        _require(bool(key) and key not in result,
                 "duplicate/empty build-profile.txt key")
        result[key] = value
    _require(result.get("MCUBOOT_PROFILE") == "true",
             "payload container requires an MCUboot profile")
    _require(result.get("TRUST_CHAIN_PREFLIGHT_REQUIRED") == "true",
             "payload container requires target trust preflight")
    _require(result.get("BL1_MANIFEST_RAW_PAGE") == "false",
             "payload container does not support raw BL1 manifest pages")
    return result, payload


def _table_files(document: dict[str, Any], key: str) -> list[str]:
    table = document.get(key)
    _require(isinstance(table, list), f"dual manifest {key} must be an array")
    result: list[str] = []
    for index, item in enumerate(table):
        _require(isinstance(item, dict), f"dual manifest {key}[{index}] is invalid")
        result.append(_safe_name(item.get("file"), f"{key}[{index}].file"))
    return result


def _image_table_files(document: dict[str, Any], key: str) -> list[str]:
    table = document.get(key)
    _require(isinstance(table, dict) and set(table) == {"cp", "ap"},
             f"dual manifest {key} is invalid")
    result: list[str] = []
    for role in ("cp", "ap"):
        entry = table[role]
        _require(isinstance(entry, dict), f"dual manifest {key}.{role} is invalid")
        result.append(_safe_name(entry.get("file"), f"{key}.{role}.file"))
    return result


def _standard_files(document: dict[str, Any]) -> set[str]:
    standard = document.get("standard_artifacts")
    _require(isinstance(standard, dict), "standard_artifacts is missing")
    _require(standard.get("status") == "generated" and standard.get("version") == 1,
             "standard artifacts were not generated")
    artifacts = standard.get("artifacts")
    _require(isinstance(artifacts, dict) and set(artifacts) == set(STANDARD_ALIASES),
             "standard artifact set is incomplete")
    result: set[str] = set()
    for alias, source in STANDARD_ALIASES.items():
        entry = artifacts[alias]
        _require(isinstance(entry, dict), f"invalid standard artifact {alias}")
        _require(entry.get("file") == alias and entry.get("source_file") == source,
                 f"standard artifact mapping drift: {alias}")
        _require(entry.get("byte_exact") is True,
                 f"standard artifact is not byte-exact: {alias}")
        result.update((alias, source))
    return result


def _segment_index(document: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for key in ("segments", "bl2_segments", "migration_segments"):
        table = document.get(key)
        _require(isinstance(table, list), f"dual manifest {key} must be an array")
        for index, item in enumerate(table):
            _require(isinstance(item, dict), f"invalid {key}[{index}]")
            bkfil = item.get("bkfil")
            _require(isinstance(bkfil, str) and bkfil not in result,
                     f"invalid/duplicate {key}[{index}].bkfil")
            result[bkfil] = item
    return result


def _plan_entry(item: dict[str, Any], payloads: dict[str, bytes]) -> dict[str, Any]:
    name = item.get("name")
    member = _safe_name(item.get("file"), "plan member")
    offset = item.get("physical_offset")
    length = item.get("length")
    end = item.get("physical_end")
    _require(isinstance(name, str) and name, "plan segment name is invalid")
    _require(isinstance(offset, int) and offset >= 0 and
             isinstance(length, int) and length > 0 and end == offset + length,
             f"plan range is invalid: {name}")
    _require(member in payloads and len(payloads[member]) == length,
             f"plan payload length mismatch: {name}")
    return {
        "name": name,
        "member": member,
        "physical_offset": offset,
        "length": length,
        "physical_end": end,
        "sha256": _sha256(payloads[member]),
    }


def _plans(document: dict[str, Any], payloads: dict[str, bytes]) -> dict[str, Any]:
    index = _segment_index(document)
    primary = document["segments"]
    by_name = {item.get("name"): item for item in primary}
    _require(set(("primary_cp_app", "primary_ap_app")) <= set(by_name),
             "primary CP/AP segments are missing")
    apps_items = [by_name["primary_cp_app"], by_name["primary_ap_app"]]

    normal = document.get("normal_update")
    factory = document.get("factory_image")
    _require(isinstance(normal, dict) and isinstance(normal.get("arguments"), list),
             "normal_update.arguments is missing")
    _require(isinstance(factory, dict) and
             isinstance(factory.get("loader_arguments"), list) and
             factory.get("requires_explicit_owner_gate") is True,
             "factory loader/owner gate is missing")

    def select(values: Iterable[Any], description: str) -> list[dict[str, Any]]:
        selected: list[dict[str, Any]] = []
        for value in values:
            _require(isinstance(value, str) and value in index,
                     f"{description} references an unknown segment")
            selected.append(index[value])
        return selected

    normal_items = select(normal["arguments"], "normal plan")
    factory_items = select(factory["loader_arguments"], "factory plan")
    def emit(items: list[dict[str, Any]], destructive: bool) -> dict[str, Any]:
        segments = [_plan_entry(item, payloads) for item in items]
        ordered = sorted(segments, key=lambda row: row["physical_offset"])
        for left, right in zip(ordered, ordered[1:]):
            _require(left["physical_end"] <= right["physical_offset"],
                     "flash plan contains overlapping ranges")
        return {
            "destructive": destructive,
            "requires_explicit_owner_gate": destructive,
            "segments": ordered,
        }

    return {
        "apps": emit(apps_items, False),
        "normal": emit(normal_items, False),
        "factory": emit(factory_items, True),
    }


def _windows_flash_guide(plans: dict[str, Any]) -> bytes:
    """Render the exact Windows manual-download arguments in plain text."""

    lines = [
        "BK7258 Windows manual download plan",
        "",
        "firmware.bkpack is ZIP-compatible; extract it with 7-Zip.",
        "It is a Beken delivery archive, not an openvela standard image.",
        "Use the official Beken Windows loader from the extracted directory.",
        "Replace <PORT_NUMBER> and <BAUD> for the current PC/board.",
        "Example: for COM9, use -p 9 (not -p COM9).",
        "Run the required target public-root preflight before any MCUboot write.",
        "Do not flash vela_cp.bin or vela_ap.bin directly; they are logical",
        "role-qualified openvela images, not CRC-expanded Flash payloads.",
        "",
    ]
    for name in ("apps", "normal", "factory"):
        plan = plans[name]
        arguments = ",".join(
            f"{row['member']}@0x{row['physical_offset']:x}-0x{row['length']:x}"
            for row in plan["segments"]
        )
        title = name.upper()
        if plan["destructive"]:
            title += " (DESTRUCTIVE; explicit owner approval required)"
        lines.extend((
            f"[{title}]",
            "bk_loader.exe download -p <PORT_NUMBER> -b <BAUD> "
            "--uart-type OTHER --mainBin-multi " + arguments +
            " --reboot 1 --fast-link 1",
            "",
        ))
    return ("\r\n".join(lines)).encode("utf-8")


def _deep_verify_directory(root: Path) -> None:
    try:
        verify_factory_layout(root)
        contract = load_contract(root / "bk7258-trust-chain.json")
        verify_contract_artifacts(contract, root)
    except (VerificationError, TrustChainError, OSError, ValueError) as error:
        raise BkpackError(f"source/deployed package verification failed: {error}") from error


def _source_document(source: Path) -> tuple[dict[str, Any], dict[str, bytes]]:
    _require(source.is_dir() and not source.is_symlink(),
             f"source must be a real directory: {source}")
    dual_payload = _read_regular(source, DUAL_MANIFEST)
    dual = _decode_json(dual_payload, DUAL_MANIFEST)
    _require(dual.get("format") == 2 and dual.get("writes_enabled") is False,
             "unsupported/unsafe dual-image manifest")
    profile, profile_payload = _profile(source)
    _deep_verify_directory(source)

    names: set[str] = {DUAL_MANIFEST, BUILD_PROFILE}
    for key in ("segments", "bl2_segments", "migration_segments"):
        names.update(_table_files(dual, key))
    names.update(_image_table_files(dual, "raw_images"))
    names.update(_image_table_files(dual, "crc_images"))
    secondary = dual.get("secondary_pair")
    _require(isinstance(secondary, dict), "MCUboot secondary_pair is missing")
    names.add(_safe_name(secondary.get("file"), "secondary_pair.file"))
    trust = dual.get("trust_chain")
    _require(isinstance(trust, dict) and
             trust.get("preflash_target_match_required") is True,
             "target trust-chain preflight is not required")
    names.add(_safe_name(trust.get("file"), "trust_chain.file"))
    names.update(_standard_files(dual))
    names.update(("bootloader.bin", "bootloader.elf", "bl2.bin", "bl2.elf"))
    names.update(name for name in OPTIONAL_METADATA if (source / name).is_file())

    payloads = {name: _read_regular(source, name) for name in sorted(names)}
    plans = _plans(dual, payloads)
    payloads[WINDOWS_FLASH_GUIDE] = _windows_flash_guide(plans)
    _require(sum(map(len, payloads.values())) <= MAX_TOTAL_SIZE,
             "package payload exceeds total size limit")
    for alias, original in STANDARD_ALIASES.items():
        _require(payloads[alias] == payloads[original],
                 f"standard alias is not byte-exact: {alias}")

    members = [
        {"name": name, "size": len(payload), "sha256": _sha256(payload)}
        for name, payload in sorted(payloads.items())
    ]
    document: dict[str, Any] = {
        "schema": SCHEMA,
        "version": 1,
        "source": {
            "dual_manifest": DUAL_MANIFEST,
            "dual_manifest_sha256": _sha256(dual_payload),
            "build_profile": BUILD_PROFILE,
            "build_profile_sha256": _sha256(profile_payload),
            "layout_id": dual.get("layout_id"),
            "physical_board": profile.get("PHYSICAL_BOARD"),
            "cp_config": profile.get("CP_CONFIG_NAME"),
            "ap_config": profile.get("AP_CONFIG_NAME"),
        },
        "integrity": {
            "authenticated": False,
            "payload_hashes": "sha256",
            "target_preflight_required": True,
            "hardware_verified": False,
            "signing_invoked": False,
        },
        "standard_artifacts": {
            alias: {"source": source_name, "byte_exact": True}
            for alias, source_name in STANDARD_ALIASES.items()
        },
        "members": members,
        "plans": plans,
    }
    return document, payloads


def _zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | 0o644) << 16
    info.extra = b""
    info.comment = b""
    return info


def create(source: Path, output: Path) -> dict[str, Any]:
    """Create one deterministic container from a verified package directory."""

    document, payloads = _source_document(source.expanduser())
    _require(len(payloads) + 1 <= MAX_MEMBERS,
             "package contains too many members")
    manifest_payload = _canonical_json(document)
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    os.close(fd)
    temporary = Path(temporary_name)
    try:
        with zipfile.ZipFile(temporary, "w", allowZip64=False) as archive:
            archive.writestr(_zip_info(MANIFEST_MEMBER), manifest_payload)
            for name in sorted(payloads):
                archive.writestr(_zip_info(name), payloads[name])
        result = verify(temporary)
        os.replace(temporary, output)
        output.chmod(0o644)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    result["package"] = str(output)
    result["output"] = str(output)
    return result


def _archive_members(package: Path) -> tuple[dict[str, Any], dict[str, bytes]]:
    try:
        info = package.lstat()
    except OSError as error:
        raise BkpackError(f"cannot stat package {package}: {error}") from error
    _require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode),
             "package must be a regular non-symlink file")
    _require(info.st_size <= MAX_ARCHIVE_SIZE, "package archive is too large")
    try:
        archive = zipfile.ZipFile(package)
    except (OSError, zipfile.BadZipFile) as error:
        raise BkpackError(f"cannot open package {package}: {error}") from error
    payloads: dict[str, bytes] = {}
    with archive:
        infos = archive.infolist()
        _require(1 <= len(infos) <= MAX_MEMBERS, "archive member count is invalid")
        total = 0
        for entry in infos:
            name = _safe_name(entry.filename, "archive member")
            _require(name not in payloads, f"duplicate archive member: {name}")
            mode = (entry.external_attr >> 16) & 0o170000
            _require(mode == stat.S_IFREG, f"archive member is not regular: {name}")
            _require(not entry.is_dir() and entry.compress_type == zipfile.ZIP_STORED,
                     f"archive member must be stored regular data: {name}")
            _require(entry.file_size == entry.compress_size <= MAX_MEMBER_SIZE,
                     f"archive member size is invalid: {name}")
            total += entry.file_size
            _require(total <= MAX_TOTAL_SIZE, "archive payload exceeds total size limit")
            try:
                payloads[name] = archive.read(entry)
            except (OSError, RuntimeError, zipfile.BadZipFile) as error:
                raise BkpackError(f"cannot read archive member {name}: {error}") from error
        _require(infos[0].filename == MANIFEST_MEMBER,
                 "container manifest must be the first member")
        _require([entry.filename for entry in infos[1:]] ==
                 sorted(entry.filename for entry in infos[1:]),
                 "payload members are not in deterministic order")
    manifest_payload = payloads.pop(MANIFEST_MEMBER, None)
    _require(manifest_payload is not None, "container manifest is missing")
    document = _decode_json(manifest_payload, MANIFEST_MEMBER)
    _require(_canonical_json(document) == manifest_payload,
             "container manifest is not canonical JSON")
    return document, payloads


def _validate_document(document: dict[str, Any], payloads: dict[str, bytes]) -> None:
    _require(set(document) == {
        "schema", "version", "source", "integrity", "standard_artifacts",
        "members", "plans",
    }, "container manifest keys are invalid")
    _require(document.get("schema") == SCHEMA and document.get("version") == 1,
             "unsupported container schema/version")
    integrity = document.get("integrity")
    _require(integrity == {
        "authenticated": False,
        "payload_hashes": "sha256",
        "target_preflight_required": True,
        "hardware_verified": False,
        "signing_invoked": False,
    }, "container integrity/trust boundary is invalid")
    members = document.get("members")
    _require(isinstance(members, list), "container members must be an array")
    expected: dict[str, dict[str, Any]] = {}
    for row in members:
        _require(isinstance(row, dict) and set(row) == {"name", "size", "sha256"},
                 "container member record is invalid")
        name = _safe_name(row.get("name"), "manifest member")
        _require(name not in expected, f"duplicate manifest member: {name}")
        _require(isinstance(row.get("size"), int) and row["size"] >= 0 and
                 isinstance(row.get("sha256"), str) and len(row["sha256"]) == 64,
                 f"invalid member hash/size: {name}")
        try:
            int(row["sha256"], 16)
        except ValueError as error:
            raise BkpackError(f"invalid member SHA-256: {name}") from error
        expected[name] = row
    _require(set(expected) == set(payloads), "archive contains missing/extra members")
    for name, payload in payloads.items():
        row = expected[name]
        _require(row["size"] == len(payload) and row["sha256"] == _sha256(payload),
                 f"member hash/size mismatch: {name}")

    aliases = document.get("standard_artifacts")
    _require(isinstance(aliases, dict) and set(aliases) == set(STANDARD_ALIASES),
             "container standard artifacts are invalid")
    for alias, source in STANDARD_ALIASES.items():
        _require(aliases[alias] == {"source": source, "byte_exact": True},
                 f"container standard mapping drift: {alias}")
        _require(alias in payloads and source in payloads and
                 payloads[alias] == payloads[source],
                 f"container standard alias mismatch: {alias}")

    source = document.get("source")
    _require(isinstance(source, dict) and
             set(source) == {
                 "dual_manifest", "dual_manifest_sha256", "build_profile",
                 "build_profile_sha256", "layout_id", "physical_board",
                 "cp_config", "ap_config",
             } and
             source.get("dual_manifest") == DUAL_MANIFEST and
             source.get("build_profile") == BUILD_PROFILE and
             source.get("dual_manifest_sha256") == _sha256(payloads[DUAL_MANIFEST]) and
             source.get("build_profile_sha256") == _sha256(payloads[BUILD_PROFILE]),
             "container source identity is invalid")
    dual = _decode_json(payloads[DUAL_MANIFEST], DUAL_MANIFEST)
    _require(source.get("layout_id") == dual.get("layout_id"),
             "container layout identity drift")
    expected_plans = _plans(dual, payloads)
    _require(document.get("plans") == expected_plans,
             "container flash plans do not match payload manifest")
    _require(payloads.get(WINDOWS_FLASH_GUIDE) ==
             _windows_flash_guide(expected_plans),
             "Windows manual-download plan does not match payload manifest")


def _write_payloads(destination: Path, payloads: dict[str, bytes]) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    for name, payload in payloads.items():
        path = destination / _safe_name(name)
        with path.open("xb") as stream:
            stream.write(payload)


def verify(package: Path) -> dict[str, Any]:
    """Verify every archive member, plan, alias and existing trust contract."""

    package = package.expanduser()
    if not package.is_absolute():
        package = Path.cwd() / package
    document, payloads = _archive_members(package)
    _validate_document(document, payloads)
    with tempfile.TemporaryDirectory(prefix="bk7258-bkpack-verify-") as temporary:
        root = Path(temporary) / "payload"
        _write_payloads(root, payloads)
        _deep_verify_directory(root)
    return {
        "status": "pass",
        "schema": SCHEMA,
        "package": str(package),
        "sha256": _sha256(package.read_bytes()),
        "member_count": len(payloads),
        "authenticated": False,
        "target_preflight_required": True,
        "hardware_verified": False,
        "plans": sorted(document["plans"]),
        "manifest": document,
    }


verify_package = verify


def materialize(package: Path, output: Path) -> Path:
    """Verify first, then atomically materialize the payload directory."""

    package = package.expanduser()
    if not package.is_absolute():
        package = Path.cwd() / package
    verify(package)
    document, payloads = _archive_members(package)
    _validate_document(document, payloads)
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        _require(output.is_dir() and not output.is_symlink() and not any(output.iterdir()),
                 f"materialize output must be absent or empty: {output}")
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    staged = temporary / "payload"
    try:
        _write_payloads(staged, payloads)
        (staged / MANIFEST_MEMBER).write_bytes(_canonical_json(document))
        _deep_verify_directory(staged)
        if output.exists():
            output.rmdir()
        os.replace(staged, output)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    shutil.rmtree(temporary, ignore_errors=True)
    return output


materialize_package = materialize


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create_parser = commands.add_parser("create")
    create_parser.add_argument("--source", type=Path, required=True)
    create_parser.add_argument("--output", type=Path, required=True)
    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("--package", type=Path, required=True)
    extract_parser = commands.add_parser("extract")
    extract_parser.add_argument("--package", type=Path, required=True)
    extract_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "create":
            result = create(args.source, args.output)
        elif args.command == "verify":
            result = verify(args.package)
        else:
            destination = materialize(args.package, args.output)
            result = {
                "status": "pass",
                "package": str(args.package.resolve()),
                "output": str(destination),
                "hardware_verified": False,
            }
    except (BkpackError, OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"FAIL bk7258-bkpack: {error}")
        return 1
    printable = dict(result)
    printable.pop("manifest", None)
    print(json.dumps(printable, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
