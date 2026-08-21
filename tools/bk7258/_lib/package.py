"""Deterministic, independently verifiable BK7258 delivery container."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Callable, Mapping

from _lib import image as image_domain
from _lib import layout as layout_domain


class PackageError(ValueError):
    """A package is malformed, unsafe, or violates its Flash contract."""


FORMAT = "bk7258.package/1"
MANIFEST = "manifest.json"
OTA_FORMAT = "bk7258.ota/1"
OTA_CATALOG = "catalog.json"
OTA_SIGNATURE = "catalog.sig"
MAX_MEMBERS = 64
MAX_MEMBER_SIZE = 16 * 1024 * 1024
MAX_PACKAGE_SIZE = 64 * 1024 * 1024
DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")


def _ota_catalog(layout: Mapping[str, object],
                 images: list[dict[str, object]],
                 security: Mapping[str, object]) -> bytes:
    by_artifact = {row.get("artifact"): row for row in images}
    generations = {
        row.get("artifact"): row
        for row in security.get("images", [])
        if isinstance(row, dict)
    }
    if set(by_artifact) != {"cp", "ap"} or set(generations) != {"cp", "ap"}:
        raise PackageError("OTA catalog requires exactly one CP/AP generation")
    version = generations["cp"].get("version")
    counter = generations["cp"].get("security_counter")
    if (version, counter) != (
        generations["ap"].get("version"),
        generations["ap"].get("security_counter"),
    ):
        raise PackageError("OTA catalog CP/AP generation does not match")
    base: dict[str, object] = {
        "format": OTA_FORMAT,
        "board_family": "bk7258",
        "layout": {
            "identity": layout.get("identity"),
            "sha256": layout.get("sha256"),
        },
        "version": version,
        "security_counter": counter,
        "cp": {
            "uri": by_artifact["cp"].get("member"),
            "size": by_artifact["cp"].get("size"),
            "sha256": by_artifact["cp"].get("sha256"),
        },
        "ap": {
            "uri": by_artifact["ap"].get("member"),
            "size": by_artifact["ap"].get("size"),
            "sha256": by_artifact["ap"].get("sha256"),
        },
    }
    document = dict(base)
    document["package_id"] = hashlib.sha256(_canonical(base)).hexdigest()
    return _canonical(document)


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("utf-8")


def _safe_member(name: str) -> None:
    path = PurePosixPath(name)
    if (not name or name.startswith("/") or "\\" in name or path.is_absolute()
            or any(part in {"", ".", ".."} for part in path.parts)):
        raise PackageError(f"unsafe package member: {name!r}")


def _entry(name: str, data: bytes) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def create(image_set: image_domain.ImageSet, member_names: Mapping[str, str],
           sdk_evidence: Mapping[str, str], trust_evidence: Mapping[str, object],
           output: Path,
           catalog_signer: Callable[[bytes], bytes] | None = None) \
           -> dict[str, object]:
    """Store already-finalized image bytes without changing them."""

    output = output.absolute()
    if output.exists() or output.is_symlink():
        raise PackageError(f"package output already exists: {output}")
    if set(member_names) != {row.artifact for row in image_set.writes}:
        raise PackageError("member-name mapping must cover every image artifact exactly")
    security_mode = trust_evidence.get("mode")
    if security_mode not in {"signed", "signed-ota", "unsigned"}:
        raise PackageError("trust evidence must explicitly declare signed or unsigned")
    for profile, digest in sdk_evidence.items():
        if not profile or not DIGEST_RE.fullmatch(digest):
            raise PackageError(f"invalid SDK evidence: {profile}")

    members: dict[str, bytes] = {}
    images: list[dict[str, object]] = []
    for row in image_set.writes:
        basename = member_names[row.artifact]
        if Path(basename).name != basename or basename in {"", ".", ".."}:
            raise PackageError(f"image basename must be explicit and flat: {basename!r}")
        member = f"images/{row.artifact}/{basename}"
        _safe_member(member)
        if member in members:
            raise PackageError(f"duplicate package member: {member}")
        members[member] = row.data
        if security_mode == "signed-ota":
            images.append({
                "artifact": row.artifact,
                "target": "inactive",
                "member": member,
                "size": len(row.data),
                "sha256": hashlib.sha256(row.data).hexdigest(),
            })
        else:
            images.append({
                "artifact": row.artifact,
                "partition": row.partition,
                "member": member,
                "offset": row.offset,
                "size": len(row.data),
                "sha256": hashlib.sha256(row.data).hexdigest(),
            })

    layout = image_set.layout
    document: dict[str, object] = {
        "format": FORMAT,
        "layout": {
            "identity": layout.identity,
            "sha256": layout.sha256,
            "name": layout.name,
            "storage_topology": layout.storage_topology,
            "flash_size": layout.flash_size,
            "erase_size": layout.erase_size,
            "crc_data_size": layout.crc_data_size,
            "crc_total_size": layout.crc_total_size,
            "xip_base": layout.xip_base,
            "partitions": [
                {
                    "name": item.name,
                    "offset": item.offset,
                    "size": item.size,
                    "type": item.kind,
                    "read": item.readable,
                    "write": item.writable,
                    "artifact": item.artifact,
                    "policy": item.policy,
                }
                for item in layout.partitions
            ],
        },
        "sdk": [
            {"profile": profile, "tree_sha256": digest}
            for profile, digest in sorted(sdk_evidence.items())
        ],
        "security": dict(trust_evidence),
        "images": images,
        "preserved_external": list(image_set.preserved_external),
        "erases": [
            {"partition": row.partition, "offset": row.offset, "size": row.size}
            for row in image_set.erases
        ],
    }
    if security_mode == "signed-ota":
        if catalog_signer is None:
            raise PackageError("apps-only OTA requires a catalog signer")
        catalog = _ota_catalog(document["layout"], images, trust_evidence)
        signature = catalog_signer(catalog)
        if len(signature) < 8 or len(signature) > 80 \
                or signature[0] != 0x30 or signature[1] != len(signature) - 2:
            raise PackageError("OTA catalog signature is not canonical DER")
        members[OTA_CATALOG] = catalog
        members[OTA_SIGNATURE] = signature
    elif catalog_signer is not None:
        raise PackageError("only apps-only OTA packages may carry a catalog signer")
    manifest = _canonical(document)
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{output.name}.", dir=output.parent)
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with zipfile.ZipFile(temporary, "w", allowZip64=False) as archive:
            archive.writestr(_entry(MANIFEST, manifest), manifest)
            for name in sorted(members):
                archive.writestr(_entry(name, members[name]), members[name])
        verify(temporary)
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    return {
        "package": str(output),
        "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "images": len(images),
        "security": security_mode,
    }


def _read(path: Path) -> tuple[dict[str, object], dict[str, bytes]]:
    path = path.absolute()
    if path.is_symlink() or not path.is_file():
        raise PackageError(f"package must be a regular non-symlink file: {path}")
    if path.stat().st_size > MAX_PACKAGE_SIZE:
        raise PackageError("package exceeds size limit")
    try:
        with zipfile.ZipFile(path, "r") as archive:
            infos = archive.infolist()
            if not infos or len(infos) > MAX_MEMBERS:
                raise PackageError("invalid package member count")
            names = [info.filename for info in infos]
            if len(names) != len(set(names)) or names[0] != MANIFEST:
                raise PackageError("manifest must be the first unique package member")
            result: dict[str, bytes] = {}
            for info in infos:
                _safe_member(info.filename)
                if (info.is_dir() or info.compress_type != zipfile.ZIP_STORED
                        or info.file_size > MAX_MEMBER_SIZE
                        or info.file_size != info.compress_size):
                    raise PackageError(f"invalid package member metadata: {info.filename}")
                result[info.filename] = archive.read(info)
    except (OSError, zipfile.BadZipFile) as error:
        raise PackageError(f"cannot read package: {path}") from error
    try:
        document = json.loads(result[MANIFEST].decode("utf-8"))
    except (KeyError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError("package manifest is not valid UTF-8 JSON") from error
    if not isinstance(document, dict) or _canonical(document) != result[MANIFEST]:
        raise PackageError("package manifest is not canonical")
    return document, result


def _integer(value: object, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise PackageError(f"invalid non-negative integer: {field}")
    return value


def _validate_security(security: dict[str, object]) -> str:
    if security == {"mode": "unsigned"}:
        return "unsigned"
    mode = security.get("mode")
    if mode == "signed-ota":
        expected = {
            "mode", "algorithm", "mcuboot_public_fingerprint",
            "mcuboot_public_der", "rollback", "trailer", "images",
        }
        prefixes = ("mcuboot",)
    else:
        legacy_expected = {
            "mode", "algorithm", "bl1_public_fingerprint",
            "mcuboot_public_fingerprint", "bl1_public_der", "mcuboot_public_der",
            "bl2_load_address", "bl1_security_counter", "rollback", "images",
        }
        expected = legacy_expected if "trailer" not in security else \
            legacy_expected | {"trailer"}
        prefixes = ("bl1", "mcuboot")
    if set(security) != expected or mode not in {"signed", "signed-ota"} \
            or security.get("algorithm") != "ecdsa-p256-sha256" \
            or security.get("rollback") != "otp-readonly-plus-explicit-software-floor":
        raise PackageError("signed package evidence shape is invalid")
    if mode == "signed-ota" and security.get("trailer") != "pending-v1":
        raise PackageError("signed OTA trailer profile is invalid")
    if mode == "signed" and "trailer" in security \
            and security.get("trailer") != "confirmed-v1":
        raise PackageError("signed full trailer profile is invalid")
    for prefix in prefixes:
        name = f"{prefix}_public_fingerprint"
        if not isinstance(security.get(name), str) \
                or not DIGEST_RE.fullmatch(security[name]):
            raise PackageError(f"signed package digest is invalid: {name}")
        encoded = security.get(f"{prefix}_public_der")
        if not isinstance(encoded, str) or len(encoded) != 182:
            raise PackageError(f"signed package public key is invalid: {prefix}")
        try:
            public = bytes.fromhex(encoded)
        except ValueError as error:
            raise PackageError(f"signed package public key is invalid: {prefix}") from error
        if len(public) != 91 or public[-65] != 0x04 \
                or hashlib.sha256(public).hexdigest() != security[f"{prefix}_public_fingerprint"]:
            raise PackageError(f"signed package public fingerprint is invalid: {prefix}")
    if mode == "signed":
        counter = _integer(security.get("bl1_security_counter"),
                           "bl1_security_counter")
        load_address = _integer(security.get("bl2_load_address"),
                                "bl2_load_address")
        if counter == 0 or load_address == 0:
            raise PackageError("BL1 security counter must be positive")
    signed_images = security.get("images")
    if not isinstance(signed_images, list) or len(signed_images) != 2:
        raise PackageError("signed CP/AP evidence is malformed")
    seen: dict[str, tuple[str, int]] = {}
    for row in signed_images:
        if not isinstance(row, dict) or set(row) != {
            "artifact", "signed_sha256", "version", "security_counter"
        }:
            raise PackageError("signed image evidence row is malformed")
        artifact = row.get("artifact")
        digest = row.get("signed_sha256")
        version = row.get("version")
        image_counter = _integer(row.get("security_counter"), "image.security_counter")
        if artifact not in {"cp", "ap"} or artifact in seen \
                or not isinstance(digest, str) or not DIGEST_RE.fullmatch(digest) \
                or not isinstance(version, str) or not version:
            raise PackageError("signed image evidence values are invalid")
        seen[artifact] = (version, image_counter)
    if set(seen) != {"cp", "ap"} or seen["cp"] != seen["ap"]:
        raise PackageError("signed CP/AP generation evidence does not match")
    return str(mode)


def verify(path: Path) -> dict[str, object]:
    document, members = _read(path)
    expected_sections = {
        "format", "layout", "sdk", "security", "images", "erases",
        "preserved_external",
    }
    if set(document) != expected_sections or document.get("format") != FORMAT:
        raise PackageError("unsupported package format")
    layout = document.get("layout")
    images = document.get("images")
    erases = document.get("erases")
    preserved_external = document.get("preserved_external")
    sdk = document.get("sdk")
    security = document.get("security")
    if not isinstance(layout, dict) or not isinstance(images, list) \
            or not isinstance(erases, list) or not isinstance(sdk, list) \
            or not isinstance(preserved_external, list) \
            or not isinstance(security, dict):
        raise PackageError("package manifest sections are malformed")
    security_mode = _validate_security(security)

    expected_layout_fields = {
        "identity", "sha256", "name", "storage_topology", "flash_size",
        "erase_size", "crc_data_size", "crc_total_size", "xip_base",
        "partitions",
    }
    if set(layout) != expected_layout_fields:
        raise PackageError("layout fields are malformed")
    name = layout.get("name")
    storage_topology = layout.get("storage_topology")
    if not isinstance(name, str) or not name \
            or storage_topology not in layout_domain.STORAGE_TOPOLOGIES:
        raise PackageError("layout name or storage topology is invalid")
    flash_size = _integer(layout.get("flash_size"), "layout.flash_size")
    erase_size = _integer(layout.get("erase_size"), "layout.erase_size")
    crc_data_size = _integer(layout.get("crc_data_size"), "layout.crc_data_size")
    crc_total_size = _integer(layout.get("crc_total_size"), "layout.crc_total_size")
    xip_base = _integer(layout.get("xip_base"), "layout.xip_base")
    digest = layout.get("sha256")
    identity = layout.get("identity")
    if not flash_size or not erase_size or not crc_data_size \
            or crc_total_size < crc_data_size:
        raise PackageError("layout geometry is invalid")
    if not isinstance(digest, str) or not DIGEST_RE.fullmatch(digest) \
            or identity != f"bk7258-{digest[:16]}":
        raise PackageError("invalid layout digest")
    partitions = layout.get("partitions")
    if not isinstance(partitions, list) or not partitions:
        raise PackageError("layout partitions are malformed")
    partition_objects: list[layout_domain.Partition] = []
    partition_by_artifact: dict[str, layout_domain.Partition] = {}
    partition_by_name: dict[str, layout_domain.Partition] = {}
    forbidden: list[tuple[int, int, str]] = []
    cursor = 0
    for index, row in enumerate(partitions):
        expected_partition_fields = {
            "name", "offset", "size", "type", "read", "write", "artifact", "policy"
        }
        if not isinstance(row, dict) or set(row) != expected_partition_fields:
            raise PackageError(f"invalid layout partition {index}")
        offset = _integer(row.get("offset"), f"partition[{index}].offset")
        size = _integer(row.get("size"), f"partition[{index}].size")
        partition_name = row.get("name")
        kind = row.get("type")
        readable = row.get("read")
        writable = row.get("write")
        artifact = row.get("artifact")
        policy = row.get("policy")
        if not isinstance(partition_name, str) or not partition_name \
                or kind not in {"code", "data"} \
                or not isinstance(readable, bool) or not isinstance(writable, bool) \
                or (artifact is not None and (not isinstance(artifact, str) or not artifact)) \
                or policy not in layout_domain.POLICIES:
            raise PackageError(f"partition fields are invalid: {index}")
        if partition_name in partition_by_name \
                or (artifact is not None and artifact in partition_by_artifact):
            raise PackageError(f"duplicate partition name or artifact: {partition_name}")
        if not size or offset < cursor or offset + size > flash_size \
                or offset % erase_size or size % erase_size:
            raise PackageError(f"partition is outside Flash: {index}")
        if kind == "code" and crc_total_size > crc_data_size:
            alignment = 1024 * crc_total_size
            if offset % alignment or size % alignment:
                raise PackageError(f"code partition violates CRC alignment: {partition_name}")
        item = layout_domain.Partition(
            partition_name, offset, size, kind, readable, writable, artifact, policy
        )
        partition_objects.append(item)
        partition_by_name[partition_name] = item
        if artifact is not None:
            partition_by_artifact[artifact] = item
        if policy in {"preserve", "immutable"}:
            forbidden.append((offset, offset + size, partition_name))
        cursor = offset + size

    partition_tuple = tuple(partition_objects)
    observed_digest = layout_domain.identity_sha256(
        name=name,
        storage_topology=storage_topology,
        flash_size=flash_size,
        erase_size=erase_size,
        crc_data_size=crc_data_size,
        crc_total_size=crc_total_size,
        xip_base=xip_base,
        partitions=partition_tuple,
    )
    if observed_digest != digest:
        raise PackageError("embedded layout facts do not match the layout digest")

    if any(not isinstance(value, str) or not value for value in preserved_external) \
            or len(preserved_external) != len(set(preserved_external)):
        raise PackageError("preserved external artifacts are malformed")
    preserved_set = set(preserved_external)
    external_set = {
        item.artifact for item in partition_tuple
        if item.policy == "external" and item.artifact is not None
    }
    if not preserved_set.issubset(external_set):
        raise PackageError("package preserves an artifact that is not external")

    expected_members = {MANIFEST}
    if security_mode == "signed-ota":
        expected_members.update({OTA_CATALOG, OTA_SIGNATURE})
    ranges: list[tuple[int, int, str]] = []
    image_artifacts: set[str] = set()
    image_data: dict[str, bytes] = {}
    for index, row in enumerate(images):
        expected_image_fields = (
            {"artifact", "target", "member", "size", "sha256"}
            if security_mode == "signed-ota" else
            {"artifact", "partition", "member", "offset", "size", "sha256"}
        )
        if not isinstance(row, dict) or set(row) != expected_image_fields:
            raise PackageError(f"invalid image row {index}")
        artifact = row.get("artifact")
        member = row.get("member")
        image_digest = row.get("sha256")
        if not isinstance(artifact, str) or artifact in image_artifacts \
                or artifact not in partition_by_artifact:
            raise PackageError(f"invalid or duplicate image artifact: {artifact}")
        partition = partition_by_artifact[artifact]
        if security_mode == "signed-ota":
            if row.get("target") != "inactive" or artifact not in {"cp", "ap"} \
                    or partition.policy != "image":
                raise PackageError(f"OTA image target is invalid: {artifact}")
        elif row.get("partition") != partition.name \
                or partition.policy not in {"image", "external"}:
            raise PackageError(f"image does not match its partition: {artifact}")
        if not isinstance(member, str) or member not in members or member == MANIFEST:
            raise PackageError(f"missing image member: {member}")
        if not isinstance(image_digest, str) or not DIGEST_RE.fullmatch(image_digest):
            raise PackageError(f"invalid image digest: {member}")
        data = members[member]
        if hashlib.sha256(data).hexdigest() != image_digest \
                or len(data) != row.get("size") or not data \
                or (security_mode == "signed-ota" and len(data) != partition.size) \
                or (security_mode != "signed-ota" and len(data) > partition.size):
            raise PackageError(f"image member identity mismatch: {member}")
        if partition.executable and crc_total_size > crc_data_size:
            image_domain.crc_decode(data, crc_data_size, crc_total_size)
        if security_mode != "signed-ota":
            offset = _integer(row.get("offset"), f"image[{index}].offset")
            if offset != partition.offset:
                raise PackageError(f"image offset does not match the layout: {artifact}")
            end = offset + len(data)
            if end > flash_size:
                raise PackageError(f"image exceeds Flash: {member}")
            ranges.append((offset, end, member))
        expected_members.add(member)
        image_artifacts.add(artifact)
        image_data[artifact] = data

    if security_mode == "signed-ota":
        catalog = members.get(OTA_CATALOG)
        signature = members.get(OTA_SIGNATURE)
        expected_catalog = _ota_catalog(layout, images, security)
        if catalog != expected_catalog:
            raise PackageError("OTA catalog does not match signed package facts")
        if signature is None or len(signature) < 8 or len(signature) > 80 \
                or signature[0] != 0x30 \
                or signature[1] != len(signature) - 2:
            raise PackageError("OTA catalog signature is malformed")
    if set(members) != expected_members:
        raise PackageError("package contains undeclared members")

    if security_mode == "signed-ota":
        if image_artifacts != {"cp", "ap"} or preserved_set or erases:
            raise PackageError(
                "apps-only OTA package must contain only CP/AP writes"
            )
    else:
        required_images = {
            item.artifact for item in partition_tuple
            if item.policy == "image" and item.artifact is not None
        }
        if not required_images.issubset(image_artifacts) \
                or (image_artifacts & external_set) | preserved_set != external_set \
                or (image_artifacts & external_set) & preserved_set:
            raise PackageError("image and preserved-external coverage is incomplete")
    if {"cp", "ap", "pair"}.issubset(image_data):
        cp_partition = partition_by_artifact["cp"]
        ap_partition = partition_by_artifact["ap"]
        expected_pair = (
            image_data["cp"].ljust(cp_partition.size, bytes([image_domain.ERASE_BYTE]))
            + image_data["ap"].ljust(ap_partition.size, bytes([image_domain.ERASE_BYTE]))
        )
        if image_data["pair"] != expected_pair:
            raise PackageError("secondary pair does not match packaged CP/AP bytes")

    observed_erases: set[tuple[str, int, int]] = set()
    for index, row in enumerate(erases):
        if not isinstance(row, dict) or set(row) != {"partition", "offset", "size"}:
            raise PackageError(f"invalid erase row {index}")
        partition_name = row.get("partition")
        offset = _integer(row.get("offset"), f"erase[{index}].offset")
        size = _integer(row.get("size"), f"erase[{index}].size")
        if partition_name not in partition_by_name or not size or offset + size > flash_size:
            raise PackageError(f"erase range exceeds Flash: {index}")
        item = partition_by_name[partition_name]
        if item.policy != "clear" or (offset, size) != (item.offset, item.size):
            raise PackageError(f"erase does not match one clear partition: {partition_name}")
        observed_erases.add((partition_name, offset, size))
        ranges.append((offset, offset + size, f"erase:{row.get('partition')}"))
    expected_erases = {
        (item.name, item.offset, item.size)
        for item in partition_tuple if item.policy == "clear"
    }
    if observed_erases != expected_erases:
        raise PackageError("clear partition coverage is incomplete")
    ranges.sort()
    for left, right in zip(ranges, ranges[1:]):
        if left[1] > right[0]:
            raise PackageError(f"overlapping package operations: {left[2]} and {right[2]}")
    for start, end, name in ranges:
        for blocked_start, blocked_end, blocked_name in forbidden:
            if start < blocked_end and blocked_start < end:
                raise PackageError(f"operation {name} touches protected partition {blocked_name}")
    sdk_profiles: set[str] = set()
    for index, row in enumerate(sdk):
        if not isinstance(row, dict) or set(row) != {"profile", "tree_sha256"} \
                or not isinstance(row.get("profile"), str) \
                or not isinstance(row.get("tree_sha256"), str) \
                or not DIGEST_RE.fullmatch(row["tree_sha256"]) \
                or row["profile"] in sdk_profiles:
            raise PackageError(f"invalid SDK evidence row: {index}")
        sdk_profiles.add(row["profile"])
    return {
        "package": str(path.resolve()),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "layout": layout.get("identity"),
        "images": len(images),
        "security": security_mode,
        "preserved_external": len(preserved_external),
    }


def extract(package: Path, output: Path) -> Path:
    verify(package)
    output = output.absolute()
    if output.is_symlink() or (output.exists() and any(output.iterdir())):
        raise PackageError(f"extraction directory must be absent or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    document, members = _read(package)
    try:
        for name, data in members.items():
            destination = output.joinpath(*PurePosixPath(name).parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(data)
    except BaseException:
        shutil.rmtree(output, ignore_errors=True)
        raise
    return output


def flash_contract(package: Path) -> dict[str, object]:
    verify(package)
    document, _ = _read(package)
    layout = document["layout"]
    if document["security"].get("mode") == "signed-ota":
        return {
            "layout": {
                "identity": layout["identity"],
                "sha256": layout["sha256"],
            },
            "target": "inactive",
            "payloads": document["images"],
            "erases": [],
            "preserved_external": [],
            "protected": [
                row for row in layout["partitions"]
                if row["policy"] in {"preserve", "immutable"}
            ],
        }
    return {
        "layout": {"identity": layout["identity"], "sha256": layout["sha256"]},
        "writes": document["images"],
        "erases": document["erases"],
        "preserved_external": document["preserved_external"],
        "protected": [
            row for row in layout["partitions"]
            if row["policy"] in {"preserve", "immutable"}
        ],
    }


def trust_evidence(package: Path) -> dict[str, object]:
    verify(package)
    document, _ = _read(package)
    security = document["security"]
    if not isinstance(security, dict):
        raise PackageError("package security evidence is malformed")
    return dict(security)


def trust_material(package: Path) -> tuple[
    dict[str, object], dict[str, object], dict[str, bytes],
    bytes | None, bytes | None,
]:
    """Return structurally verified public evidence and finalized image bytes."""

    verify(package)
    document, members = _read(package)
    images = {
        row["artifact"]: members[row["member"]]
        for row in document["images"]
    }
    return (
        dict(document["security"]),
        dict(document["layout"]),
        images,
        members.get(OTA_CATALOG),
        members.get(OTA_SIGNATURE),
    )
