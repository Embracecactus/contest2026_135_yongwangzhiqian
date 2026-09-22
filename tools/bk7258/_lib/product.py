# SPDX-License-Identifier: Apache-2.0

"""Board-declared BK7258 product delivery and recovery images."""

from __future__ import annotations

import csv
import hashlib
import json
import os
import re
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Mapping

from _lib import build as build_domain
from _lib import image as image_domain
from _lib import layout as layout_domain
from _lib import package as package_domain


class ProductError(ValueError):
    """A release policy or product delivery violates its declared contract."""


POLICY_FORMAT = "bk7258.release-policy/1"
BASE_EVIDENCE_FORMAT = "bk7258.accepted-base/1"
DELIVERY_FORMAT = "bk7258.product-delivery/1"
DELIVERY_MANIFEST = "release.json"
DELIVERY_CHECKSUMS = "SHA256SUMS"
DELIVERY_FLASHING = "FLASHING.md"
GATEWAY_RELEASE_REGISTRY_FORMAT = "shaniu.firmware-release-registry/1"
MAX_GATEWAY_RELEASES = 32
MAX_GATEWAY_PACKAGE_SIZE = 64 * 1024 * 1024
MAX_DELIVERY_SIZE = 128 * 1024 * 1024
VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\." r"(0|[1-9][0-9]*)\+([1-9][0-9]*)$"
)
DIRECTIVE_RE = re.compile(r"#\s*([A-Z][A-Z0-9_]*)=(.+)")
DIGEST_RE = re.compile(r"^[0-9a-f]{64}$")
DEVICE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
CAPTURE_METHOD_RE = re.compile(r"^[a-z][a-z0-9._-]{0,63}$")
MAX_VERSION_GENERATION = (1 << 32) - 1
RELEASE_POLICIES = frozenset(
    {
        "replace",
        "preserve",
        "device-unique",
        "transactional",
        "factory-init",
        "immutable",
    }
)
FACTORY_MODES = frozenset({"provision-required", "external-provisioned", "device-firstboot"})


@dataclass(frozen=True)
class ReleasePolicy:
    source: Path
    factory_mode: str
    partitions: tuple[tuple[str, str], ...]
    sha256: str

    @property
    def by_partition(self) -> dict[str, str]:
        return dict(self.partitions)


def factory_software_plan(
    layout: layout_domain.Layout, policy: ReleasePolicy
) -> dict[str, object]:
    """Describe unsigned hardware bytes still needed for one target-bound BIN."""

    selected = policy.by_partition
    if set(selected) != {row.name for row in layout.partitions}:
        raise ProductError("factory policy does not cover the selected layout")
    classifications: list[dict[str, object]] = []
    target_ranges: list[dict[str, object]] = []
    position = 0
    for row in sorted(layout.partitions, key=lambda item: item.offset):
        if row.offset > position:
            target_ranges.append(
                {
                    "name": "unallocated",
                    "offset": position,
                    "size": row.offset - position,
                    "source": "same-device-snapshot",
                }
            )
        release_policy = selected[row.name]
        if row.name == "factory_state":
            source = "factory-transaction"
        elif release_policy == "replace":
            source = "signed-build"
        elif release_policy == "factory-init":
            source = "erased-user-state"
        elif release_policy == "transactional":
            source = "reset-transaction-state"
        else:
            source = "same-device-snapshot"
            target_ranges.append(
                {
                    "name": row.name,
                    "offset": row.offset,
                    "size": row.size,
                    "source": source,
                }
            )
        classifications.append(
            {
                "name": row.name,
                "offset": row.offset,
                "size": row.size,
                "release_policy": release_policy,
                "source": source,
            }
        )
        position = row.end
    if position < layout.flash_size:
        target_ranges.append(
            {
                "name": "unallocated",
                "offset": position,
                "size": layout.flash_size - position,
                "source": "same-device-snapshot",
            }
        )
    return {
        "format": "bk7258.factory-software-plan/1",
        "factory_mode": policy.factory_mode,
        "layout": {"identity": layout.identity, "sha256": layout.sha256},
        "flash_size": layout.flash_size,
        "partitions": classifications,
        "target_snapshot_required": target_ranges,
        "materialized": False,
    }


@dataclass(frozen=True)
class BaseEvidence:
    source: Path
    data: bytes
    physical_board: str
    layout_identity: str
    layout_sha256: str
    flash_size: int
    base_sha256: str
    device_id: str
    capture_method: str
    sha256: str


@dataclass(frozen=True)
class RecoveryImage:
    data: bytes
    base_sha256: str
    base_evidence_sha256: str
    states: tuple[dict[str, object], ...]


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return package_domain._canonical(value)


def _regular_bytes(path: Path, label: str) -> bytes:
    path = path.absolute()
    if path.is_symlink() or not path.is_file():
        raise ProductError(f"{label} must be a regular non-symlink file: {path}")
    return path.read_bytes()


def _partition_rows(layout: layout_domain.Layout) -> tuple[dict[str, object], ...]:
    return tuple(
        {
            "name": row.name,
            "offset": row.offset,
            "size": row.size,
            "artifact": row.artifact,
            "policy": row.policy,
        }
        for row in layout.partitions
    )


def _parse_policy(
    data: bytes,
    source: Path,
    partitions: tuple[Mapping[str, object], ...],
) -> ReleasePolicy:
    try:
        text = data.decode("utf-8")
    except UnicodeError as error:
        raise ProductError("release policy must be UTF-8") from error

    directives: dict[str, str] = {}
    rows: list[tuple[str, str]] = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            match = DIRECTIVE_RE.fullmatch(line)
            if match is not None:
                key, value = match.groups()
                if key in directives:
                    raise ProductError(
                        f"duplicate release policy directive: {source}:{number}:{key}"
                    )
                directives[key] = value.strip()
            continue
        fields = [field.strip() for field in next(csv.reader([raw]))]
        if len(fields) != 2:
            raise ProductError(f"{source}:{number}: expected Partition,ReleasePolicy")
        name, policy = fields
        if not name or policy not in RELEASE_POLICIES:
            raise ProductError(f"invalid release policy row: {source}:{number}")
        rows.append((name, policy))

    if (
        set(directives) != {"FORMAT", "FACTORY_MODE"}
        or directives.get("FORMAT") != POLICY_FORMAT
        or directives.get("FACTORY_MODE") not in FACTORY_MODES
    ):
        raise ProductError("release policy directives are incomplete or unsupported")
    names = [name for name, _ in rows]
    if len(names) != len(set(names)):
        raise ProductError("release policy contains duplicate partitions")

    layout_by_name: dict[str, Mapping[str, object]] = {}
    for row in partitions:
        name = row.get("name")
        if not isinstance(name, str) or name in layout_by_name:
            raise ProductError("layout partition names are malformed")
        layout_by_name[name] = row
    if set(names) != set(layout_by_name):
        missing = sorted(set(layout_by_name) - set(names))
        extra = sorted(set(names) - set(layout_by_name))
        raise ProductError(
            f"release policy coverage mismatch: missing={missing} extra={extra}"
        )

    selected = dict(rows)
    for name, layout_row in layout_by_name.items():
        build_policy = layout_row.get("policy")
        release_policy = selected[name]
        if build_policy in {"image", "external"} and release_policy != "replace":
            raise ProductError(
                f"firmware partition must use replace release policy: {name}"
            )
        if build_policy == "clear" and release_policy not in {
            "transactional",
            "factory-init",
        }:
            raise ProductError(
                f"clear partition needs transactional/factory-init policy: {name}"
            )
        if build_policy == "preserve" and release_policy == "replace":
            raise ProductError(
                f"preserved build partition cannot use replace policy: {name}"
            )
        if build_policy == "immutable" and release_policy not in {
            "device-unique",
            "immutable",
        }:
            raise ProductError(
                f"immutable build partition has unsafe release policy: {name}"
            )

    return ReleasePolicy(
        source=source,
        factory_mode=directives["FACTORY_MODE"],
        partitions=tuple(rows),
        sha256=_digest(data),
    )


def load_policy(path: Path, layout: layout_domain.Layout) -> ReleasePolicy:
    """Load complete release semantics without duplicating layout geometry."""

    path = path.absolute()
    return _parse_policy(
        _regular_bytes(path, "release policy"),
        path.resolve(strict=True),
        _partition_rows(layout),
    )


def report_policy(policy: ReleasePolicy) -> dict[str, object]:
    return {
        "format": POLICY_FORMAT,
        "factory_mode": policy.factory_mode,
        "sha256": policy.sha256,
        "partitions": [
            {"name": name, "policy": value} for name, value in policy.partitions
        ],
    }


def _layout_binding(
    layout: layout_domain.Layout | Mapping[str, object],
) -> dict[str, object]:
    if isinstance(layout, layout_domain.Layout):
        flash_size = layout.flash_size
        identity = layout.identity
        sha256 = layout.sha256
    else:
        flash_size = layout.get("flash_size")
        identity = layout.get("identity")
        sha256 = layout.get("sha256")
    if (
        not isinstance(flash_size, int)
        or isinstance(flash_size, bool)
        or flash_size <= 0
        or not isinstance(identity, str)
        or not identity
        or not isinstance(sha256, str)
        or DIGEST_RE.fullmatch(sha256) is None
    ):
        raise ProductError("accepted-base layout identity is malformed")
    return {
        "flash_size": flash_size,
        "identity": identity,
        "sha256": sha256,
    }


def _target_binding(target: Mapping[str, object]) -> dict[str, str]:
    if (
        set(target) != {"board_family", "physical_board"}
        or target.get("board_family") != "bk7258"
        or not isinstance(target.get("physical_board"), str)
        or re.fullmatch(r"[a-z][a-z0-9_]*", str(target["physical_board"])) is None
    ):
        raise ProductError("accepted-base target identity is malformed")
    return {
        "board_family": "bk7258",
        "physical_board": str(target["physical_board"]),
    }


def _parse_base_evidence(
    data: bytes,
    source: Path,
    expected_target: Mapping[str, object],
    expected_layout: layout_domain.Layout | Mapping[str, object],
) -> BaseEvidence:
    try:
        document = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ProductError("accepted-base evidence is not valid UTF-8 JSON") from error
    if (
        not isinstance(document, dict)
        or _canonical(document) != data
        or set(document) != {"base", "capture", "format", "layout", "target"}
        or document.get("format") != BASE_EVIDENCE_FORMAT
    ):
        raise ProductError("accepted-base evidence is non-canonical or unsupported")

    target = _target_binding(expected_target)
    layout = _layout_binding(expected_layout)
    base = document.get("base")
    capture = document.get("capture")
    if (
        document.get("target") != target
        or document.get("layout") != layout
        or not isinstance(base, dict)
        or set(base) != {"sha256", "size"}
        or base.get("size") != layout["flash_size"]
        or not isinstance(base.get("sha256"), str)
        or DIGEST_RE.fullmatch(base["sha256"]) is None
        or not isinstance(capture, dict)
        or set(capture) != {"device_id", "method"}
        or not isinstance(capture.get("device_id"), str)
        or DEVICE_ID_RE.fullmatch(capture["device_id"]) is None
        or not isinstance(capture.get("method"), str)
        or CAPTURE_METHOD_RE.fullmatch(capture["method"]) is None
    ):
        raise ProductError(
            "accepted-base evidence does not match the selected board/layout"
        )
    return BaseEvidence(
        source=source,
        data=data,
        physical_board=target["physical_board"],
        layout_identity=str(layout["identity"]),
        layout_sha256=str(layout["sha256"]),
        flash_size=int(layout["flash_size"]),
        base_sha256=str(base["sha256"]),
        device_id=str(capture["device_id"]),
        capture_method=str(capture["method"]),
        sha256=_digest(data),
    )


def load_base_evidence(
    path: Path,
    target: Mapping[str, object],
    layout: layout_domain.Layout | Mapping[str, object],
) -> BaseEvidence:
    """Load operator-accepted readback evidence bound to one board and layout."""

    path = path.absolute()
    return _parse_base_evidence(
        _regular_bytes(path, "accepted-base evidence"),
        path.resolve(strict=True),
        target,
        layout,
    )


def create_base_evidence(
    *,
    physical_board: str,
    layout: layout_domain.Layout,
    base: Path,
    device_id: str,
    capture_method: str,
    output: Path,
) -> dict[str, object]:
    """Accept one complete readback as the recovery base for one device."""

    target = _target_binding(
        {"board_family": "bk7258", "physical_board": physical_board}
    )
    layout_row = _layout_binding(layout)
    if DEVICE_ID_RE.fullmatch(device_id) is None:
        raise ProductError(
            "device ID must use 1-128 letters, digits, '.', '_', ':' or '-'"
        )
    if CAPTURE_METHOD_RE.fullmatch(capture_method) is None:
        raise ProductError("capture method must be a lowercase machine identifier")
    base_data = _regular_bytes(base, "accepted device readback")
    if len(base_data) != layout.flash_size:
        raise ProductError(
            "accepted device readback must cover the complete Flash: "
            f"size=0x{len(base_data):x} flash=0x{layout.flash_size:x}"
        )
    document = {
        "base": {"sha256": _digest(base_data), "size": len(base_data)},
        "capture": {"device_id": device_id, "method": capture_method},
        "format": BASE_EVIDENCE_FORMAT,
        "layout": layout_row,
        "target": target,
    }
    data = _canonical(document)
    output = output.absolute()
    if output.suffix.lower() != ".json":
        raise ProductError("accepted-base evidence output must use .json")
    if output.exists() or output.is_symlink():
        raise ProductError(f"accepted-base evidence already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        _parse_base_evidence(data, temporary, target, layout)
        package_domain._publish_no_replace(temporary, output, "accepted-base evidence")
    finally:
        temporary.unlink(missing_ok=True)
    return {
        "base_sha256": document["base"]["sha256"],
        "device_id": device_id,
        "evidence": str(output),
        "physical_board": physical_board,
        "sha256": _digest(data),
        "size": len(base_data),
    }


def relocate_base(
    *,
    source_layout: layout_domain.Layout,
    layout: layout_domain.Layout,
    base: Path,
    output: Path,
) -> dict[str, object]:
    """Relocate same-device data verbatim by partition name. Newly declared
    preserve ranges may only cover old layout gaps and retain their exact bytes.
    Ciphertext is neither interpreted, cleared nor rewritten.
    """

    geometry = (
        "flash_size",
        "erase_size",
        "crc_data_size",
        "crc_total_size",
        "xip_base",
        "storage_topology",
    )
    if any(
        getattr(source_layout, field) != getattr(layout, field) for field in geometry
    ):
        raise ProductError("base relocation cannot change device geometry")
    if source_layout.sha256 == layout.sha256:
        raise ProductError("base relocation requires different layouts")
    protected = {"preserve", "immutable"}
    source_rows = {
        row.name: row for row in source_layout.partitions if row.policy in protected
    }
    target_rows = {
        row.name: row for row in layout.partitions if row.policy in protected
    }
    if not source_rows or not source_rows.keys() <= target_rows.keys():
        raise ProductError("base relocation must retain every protected partition")
    new_rows = [row for name, row in target_rows.items() if name not in source_rows]
    for row in new_rows:
        if (
            row.policy != "preserve"
            or row.kind != "data"
            or not row.readable
            or not row.writable
            or row.artifact is not None
            or row.offset % layout.erase_size
            or row.size % layout.erase_size
        ):
            raise ProductError(
                "new protected partition is not a preservable data range"
            )
        if any(
            row.offset < old.end and old.offset < row.end
            for old in source_layout.partitions
        ):
            raise ProductError("new protected partition overlaps the source layout")
    source = _regular_bytes(base, "current same-device base")
    if len(source) != layout.flash_size:
        raise ProductError("base relocation requires a complete Flash snapshot")
    output = output.absolute()
    if output.exists() or output.is_symlink():
        raise ProductError(f"relocated base already exists: {output}")
    result = bytearray(source)
    mappings = []
    for name, old in source_rows.items():
        target = target_rows[name]
        if (
            old.kind != "data"
            or target.kind != old.kind
            or target.size != old.size
            or target.policy != old.policy
            or target.readable != old.readable
            or target.writable != old.writable
        ):
            raise ProductError(f"protected partition contract changed: {name}")
        if old.policy == "immutable" and target.offset != old.offset:
            raise ProductError(f"immutable partition cannot move: {name}")
        # Always read bytes from the original snapshot so overlapping source
        # and target ranges cannot pollute later relocation inputs.
        payload = source[old.offset : old.end]
        result[target.offset : target.end] = payload
        mappings.append(
            {
                "partition": name,
                "source_offset": old.offset,
                "target_offset": target.offset,
                "size": target.size,
                "sha256": _digest(payload),
            }
        )
    for name, old in source_rows.items():
        target = target_rows[name]
        if result[target.offset : target.end] != source[old.offset : old.end]:
            raise ProductError(f"relocated partition mismatch: {name}")
    carried_forward_gaps = []
    for row in new_rows:
        payload = source[row.offset : row.end]
        if result[row.offset : row.end] != payload:
            raise ProductError(f"new protected partition was not preserved: {row.name}")
        carried_forward_gaps.append(
            {
                "partition": row.name,
                "offset": row.offset,
                "size": row.size,
                "sha256": _digest(payload),
                "action": "preserved-unallocated",
            }
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(result)
            stream.flush()
            os.fsync(stream.fileno())
        package_domain._publish_no_replace(temporary, output, "relocated base")
    finally:
        temporary.unlink(missing_ok=True)
    return {
        "format": "bk7258.base-relocation/1",
        "output": str(output),
        "source_layout": source_layout.sha256,
        "target_layout": layout.sha256,
        "source_sha256": _digest(source),
        "sha256": _digest(result),
        "partitions": mappings,
        "carried_forward_gaps": carried_forward_gaps,
    }


def _package_version(document: Mapping[str, object]) -> str | None:
    security = document.get("security")
    if not isinstance(security, dict) or security.get("mode") == "unsigned":
        return None
    images = security.get("images")
    if not isinstance(images, list) or len(images) != 2:
        raise ProductError("signed package has no coherent CP/AP version")
    versions = {row.get("version") for row in images if isinstance(row, dict)}
    if len(versions) != 1:
        raise ProductError("signed package CP/AP versions do not match")
    version = versions.pop()
    if not isinstance(version, str) or VERSION_RE.fullmatch(version) is None:
        raise ProductError("signed package version is malformed")
    version_generation(version)
    return version


def version_generation(version: str) -> int:
    """Return a wire-safe product generation from a canonical version."""

    match = VERSION_RE.fullmatch(version) if isinstance(version, str) else None
    if match is None:
        raise ProductError("version must use MAJOR.MINOR.PATCH+GENERATION")
    generation = int(match.group(4))
    if generation > MAX_VERSION_GENERATION:
        raise ProductError("version generation exceeds uint32")
    return generation


def _package_target_layout(
    package: Path,
) -> tuple[dict[str, object], dict[str, bytes], dict[str, object]]:
    report = package_domain.verify(package)
    document, members = package_domain._read(package)
    target = document.get("target")
    layout = document.get("layout")
    if not isinstance(target, dict) or not isinstance(layout, dict):
        raise ProductError("product delivery requires a target-bound package")
    return document, members, report


def operation_impact(
    package: Path,
    policy: ReleasePolicy,
    *,
    transport: str,
) -> dict[str, object]:
    """Project verified package operations into a transport-specific impact.

    This is descriptive release evidence.  It does not claim an unknown
    startup migration is safe and it does not turn an OTA into a full-image
    operation.
    """

    if transport not in {"full-bin", "ota"}:
        raise ProductError("operation impact transport must be full-bin or ota")

    document, _, report = _package_target_layout(package)
    layout = document.get("layout")
    if (
        not isinstance(layout, dict)
        or not isinstance(layout.get("flash_size"), int)
        or isinstance(layout["flash_size"], bool)
        or layout["flash_size"] <= 0
    ):
        raise ProductError("operation impact package layout is malformed")

    partitions = layout.get("partitions")
    if not isinstance(partitions, list):
        raise ProductError("operation impact package partitions are malformed")
    parsed_policy = _parse_policy(
        _regular_bytes(policy.source, "release policy"),
        policy.source,
        tuple(partitions),
    )
    if parsed_policy.sha256 != policy.sha256:
        raise ProductError("release policy changed while projecting operations")

    contract = package_domain.flash_contract(package)
    if contract.get("layout") != {
        "identity": layout.get("identity"),
        "sha256": layout.get("sha256"),
    }:
        raise ProductError("operation impact package contract layout changed")

    impact: dict[str, object] = {
        "layout": {
            "flash_size": layout["flash_size"],
            "identity": layout.get("identity"),
            "sha256": layout.get("sha256"),
        },
        "release_policy": report_policy(parsed_policy),
        "startup_migration": {
            "status": "unknown",
            "unconditional_start_allowed": False,
        },
        "transport": transport,
    }

    if transport == "full-bin":
        if report["security"] == "signed-ota" or "writes" not in contract:
            raise ProductError("full-bin impact requires a non-OTA package")
        impact["full_bin"] = {
            "device_unique_data": "trusted-same-device-base-required",
            "erases": [{"offset": 0, "size": layout["flash_size"]}],
            "flash_offset": 0,
            "flash_size": layout["flash_size"],
            "scope": "complete-flash",
            "writes": [{"offset": 0, "size": layout["flash_size"]}],
            "package_overlay": {
                "erases": contract["erases"],
                "writes": contract["writes"],
            },
        }
        return impact

    payloads = contract.get("payloads")
    if (
        report["security"] != "signed-ota"
        or contract.get("target") != "inactive"
        or not isinstance(payloads, list)
        or {row.get("artifact") for row in payloads if isinstance(row, dict)}
        != {"cp", "ap"}
    ):
        raise ProductError("OTA impact requires an inactive CP/AP OTA package")
    if contract.get("erases") != []:
        raise ProductError("OTA package-level erase operations are unsupported")
    impact["ota"] = {
        "full_flash_base": "not-required",
        "package_erase_operations": [],
        "payloads": payloads,
        "target": "inactive",
        "target_device_erase_granularity": "device-managed-unknown",
    }
    return impact


def _validate_build_manifest(
    data: bytes,
    package_document: Mapping[str, object],
    package_security: str,
) -> dict[str, object]:
    try:
        document = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ProductError("build manifest is not valid UTF-8 JSON") from error
    if (
        not isinstance(document, dict)
        or document.get("format") not in build_domain.TARGET_BOUND_FORMATS
        or document.get("target") != package_document.get("target")
    ):
        raise ProductError("build manifest does not match the package target")
    if document["format"] == build_domain.BUILD_MANIFEST_FORMAT:
        try:
            build_domain.validate_provenance(document.get("provenance"))
        except build_domain.BuildError as error:
            raise ProductError(str(error)) from error
    build_layout = document.get("layout")
    package_layout = package_document.get("layout")
    if (
        not isinstance(build_layout, dict)
        or not isinstance(package_layout, dict)
        or build_layout.get("identity") != package_layout.get("identity")
        or build_layout.get("sha256") != package_layout.get("sha256")
    ):
        raise ProductError("build manifest does not match the package layout")
    expected_boot = "direct" if package_security == "unsigned" else "mcuboot"
    if document.get("boot") != expected_boot:
        raise ProductError("build manifest boot mode does not match the package")

    if package_security == "unsigned":
        finalized = document.get("finalized_flash")
        images = package_document.get("images")
        if not isinstance(finalized, dict) or not isinstance(images, list):
            raise ProductError("direct build manifest has no finalized images")
        packaged = {
            row.get("artifact"): (row.get("size"), row.get("sha256"))
            for row in images
            if isinstance(row, dict)
        }
        if set(finalized) != set(packaged):
            raise ProductError("direct build/package artifact coverage changed")
        for artifact, row in finalized.items():
            if not isinstance(row, dict) or packaged[artifact] != (
                row.get("size"),
                row.get("sha256"),
            ):
                raise ProductError(f"direct build/package artifact changed: {artifact}")
    return document


def validate_build_manifest_evidence(
    build_manifest: Path,
    package: Path,
) -> dict[str, object]:
    """Validate a copied build manifest against its release package facts."""

    package_document, _, package_report = _package_target_layout(package)
    security = str(package_report["security"])
    if security not in {"unsigned", "signed", "signed-ota"}:
        raise ProductError("build evidence package security is unsupported")
    return _validate_build_manifest(
        _regular_bytes(build_manifest, "build manifest"),
        package_document,
        security,
    )


def _base(path: Path, expected_sha256: str, flash_size: int) -> bytes:
    if (
        not isinstance(expected_sha256, str)
        or DIGEST_RE.fullmatch(expected_sha256.lower()) is None
    ):
        raise ProductError("base SHA-256 must contain 64 hexadecimal digits")
    data = _regular_bytes(path, "device recovery base")
    if len(data) != flash_size:
        raise ProductError(
            "device recovery base must cover the complete Flash: "
            f"size=0x{len(data):x} flash=0x{flash_size:x}"
        )
    observed = _digest(data)
    if observed != expected_sha256.lower():
        raise ProductError(
            "device recovery base SHA-256 mismatch: "
            f"expected={expected_sha256.lower()} observed={observed}"
        )
    return data


def _range_state(
    *,
    name: str,
    policy: str,
    offset: int,
    size: int,
    before: bytes,
    after: bytes,
    action: str,
) -> dict[str, object]:
    return {
        "action": action,
        "after_sha256": _digest(after),
        "before_sha256": _digest(before),
        "name": name,
        "offset": offset,
        "policy": policy,
        "size": size,
    }


def materialize_recovery(
    package: Path,
    policy: ReleasePolicy,
    base: Path,
    base_evidence: BaseEvidence,
) -> RecoveryImage:
    """Overlay verified firmware on one accepted complete device readback."""

    document, members, package_report = _package_target_layout(package)
    if package_report["security"] == "signed-ota":
        raise ProductError("an OTA package cannot materialize a recovery image")
    layout = document["layout"]
    partitions = layout.get("partitions")
    flash_size = layout.get("flash_size")
    if (
        not isinstance(partitions, list)
        or not isinstance(flash_size, int)
        or isinstance(flash_size, bool)
        or flash_size <= 0
    ):
        raise ProductError("package layout is malformed")
    target = document.get("target")
    if not isinstance(target, dict):
        raise ProductError("package target is malformed")
    refreshed_evidence = load_base_evidence(base_evidence.source, target, layout)
    if refreshed_evidence.sha256 != base_evidence.sha256:
        raise ProductError("accepted-base evidence changed during materialization")
    parsed_policy = _parse_policy(
        _regular_bytes(policy.source, "release policy"),
        policy.source,
        tuple(partitions),
    )
    if parsed_policy.sha256 != policy.sha256:
        raise ProductError("release policy changed during materialization")
    selected = policy.by_partition
    base_data = _base(base, base_evidence.base_sha256, flash_size)
    output = bytearray(base_data)
    by_name = {row["name"]: row for row in partitions if isinstance(row, dict)}
    touched: set[str] = set()

    for row in partitions:
        if not isinstance(row, dict):
            raise ProductError("package partition row is malformed")
        name = row.get("name")
        offset = row.get("offset")
        size = row.get("size")
        if (
            not isinstance(name, str)
            or not isinstance(offset, int)
            or not isinstance(size, int)
        ):
            raise ProductError("package partition geometry is malformed")
        if selected[name] == "transactional":
            output[offset : offset + size] = bytes([image_domain.ERASE_BYTE]) * size
            touched.add(name)

    images = document.get("images")
    if not isinstance(images, list):
        raise ProductError("package image rows are malformed")
    for row in images:
        if not isinstance(row, dict):
            raise ProductError("package image row is malformed")
        partition_name = row.get("partition")
        member = row.get("member")
        if (
            not isinstance(partition_name, str)
            or partition_name not in by_name
            or not isinstance(member, str)
            or member not in members
        ):
            raise ProductError("package image placement is malformed")
        partition = by_name[partition_name]
        if selected[partition_name] != "replace":
            raise ProductError(
                f"package writes non-replace partition: {partition_name}"
            )
        offset = partition["offset"]
        size = partition["size"]
        output[offset : offset + size] = bytes([image_domain.ERASE_BYTE]) * size
        data = members[member]
        output[offset : offset + len(data)] = data
        touched.add(partition_name)

    full_update = document.get("full_update")
    if full_update is not None:
        if not isinstance(full_update, dict):
            raise ProductError("full update payload is malformed")
        partition_name = full_update.get("partition")
        member = full_update.get("member")
        if (
            not isinstance(partition_name, str)
            or partition_name not in by_name
            or not isinstance(member, str)
            or member not in members
        ):
            raise ProductError("full update placement is malformed")
        partition = by_name[partition_name]
        offset = partition["offset"]
        data = members[member]
        before = base_data[offset : offset + len(data)]
        if selected[partition_name] != "replace" and data != before:
            raise ProductError(
                f"full update changes protected device data: {partition_name}"
            )
        output[offset : offset + len(data)] = data
        touched.add(partition_name)

    for row in document.get("erases", []):
        if not isinstance(row, dict):
            raise ProductError("package erase row is malformed")
        partition_name = row.get("partition")
        if (
            not isinstance(partition_name, str)
            or partition_name not in by_name
            or selected[partition_name] not in {"replace", "transactional"}
        ):
            raise ProductError("package erase violates release policy")
        offset = by_name[partition_name]["offset"]
        size = by_name[partition_name]["size"]
        output[offset : offset + size] = bytes([image_domain.ERASE_BYTE]) * size
        touched.add(partition_name)

    states: list[dict[str, object]] = []
    cursor = 0
    for row in partitions:
        name = row["name"]
        offset = row["offset"]
        size = row["size"]
        if cursor < offset:
            before = base_data[cursor:offset]
            after = bytes(output[cursor:offset])
            if before != after:
                raise ProductError("recovery changed an unmapped Flash range")
            states.append(
                _range_state(
                    name=f"@gap-0x{cursor:x}",
                    policy="preserve",
                    offset=cursor,
                    size=offset - cursor,
                    before=before,
                    after=after,
                    action="preserved-gap",
                )
            )
        before = base_data[offset : offset + size]
        after = bytes(output[offset : offset + size])
        release_policy = selected[name]
        if (
            release_policy in {"preserve", "device-unique", "factory-init", "immutable"}
            and before != after
        ):
            raise ProductError(f"recovery changed protected partition: {name}")
        if release_policy == "transactional":
            expected = bytes([image_domain.ERASE_BYTE]) * size
            if after != expected:
                raise ProductError(f"transactional partition was not reset: {name}")
            action = "reset"
        elif name in touched:
            action = "replaced" if release_policy == "replace" else "preserved"
        elif release_policy == "replace":
            action = "carried-forward-external"
            if before != after:
                raise ProductError(f"carried external partition changed: {name}")
        else:
            action = "preserved"
        states.append(
            _range_state(
                name=name,
                policy=release_policy,
                offset=offset,
                size=size,
                before=before,
                after=after,
                action=action,
            )
        )
        cursor = offset + size
    if cursor < flash_size:
        before = base_data[cursor:flash_size]
        after = bytes(output[cursor:flash_size])
        if before != after:
            raise ProductError("recovery changed the trailing unmapped Flash range")
        states.append(
            _range_state(
                name=f"@gap-0x{cursor:x}",
                policy="preserve",
                offset=cursor,
                size=flash_size - cursor,
                before=before,
                after=after,
                action="preserved-gap",
            )
        )

    return RecoveryImage(
        data=bytes(output),
        base_sha256=_digest(base_data),
        base_evidence_sha256=base_evidence.sha256,
        states=tuple(states),
    )


def _file_row(path: str, data: bytes) -> dict[str, object]:
    return {"path": path, "sha256": _digest(data), "size": len(data)}


def _checksums(members: Mapping[str, bytes]) -> bytes:
    return "".join(
        f"{_digest(members[name])}  {name}\n"
        for name in sorted(members)
        if name != DELIVERY_CHECKSUMS
    ).encode("ascii")


def _flashing(document: Mapping[str, object]) -> bytes:
    target = document["target"]
    components = document["components"]
    recovery = components["recovery"]
    ota = components["ota"]
    lines = [
        "# BK7258 product delivery",
        "",
        f"Target board: `{target['physical_board']}`",
        f"Version: `{document['version']}`",
        "",
        "## Wired recovery",
        "",
        "The recovery BIN is bound to one operator-accepted device readback.",
        f"Use it only on device `{recovery['accepted_base']['device_id']}`, whose",
        "complete base SHA-256 is " f"`{recovery['accepted_base']['sha256']}`.",
        "Do not chip-erase and do not copy this image to another unit.",
        "",
        f"- file: `{recovery['operator']['path']}`",
        "- Flash start: `0x000000`",
        f"- Flash end (exclusive): `0x{recovery['operator']['flash_end']:06x}`",
        f"- SHA-256: `{recovery['operator']['sha256']}`",
        "",
        "## Factory",
        "",
        "No universal factory image is included. Manufacturing provisioning",
        "must assign per-device MAC/RF/Bluetooth state before a factory image",
        "can be released.",
        "",
        "## OTA",
        "",
    ]
    if ota["status"] == "included":
        lines.extend(
            (
                f"- file: `{ota['package']['path']}`",
                "- required source version: " f"`{ota['required_source_version']}`",
                "- required source-device MCUboot root: "
                f"`{ota['required_source_root']}`",
                "- compatibility status: operator precondition; confirm both "
                "values from the device before applying OTA",
            )
        )
    else:
        lines.append("No OTA component is included in this delivery.")
    if recovery["security"] == "signed":
        lines.extend(
            (
                "",
                "Security: **signed full recovery**.",
                "Installed MCUboot root: " f"`{recovery['installed_root']}`.",
            )
        )
    else:
        lines.extend(
            (
                "",
                "Security: **UNSIGNED diagnostic recovery; not a production release**.",
            )
        )
    return ("\n".join(lines) + "\n").encode("utf-8")


def create_delivery(
    *,
    package: Path,
    build_manifest: Path,
    policy: ReleasePolicy,
    recovery: RecoveryImage,
    base_evidence: BaseEvidence,
    version: str,
    output: Path,
    ota_package: Path | None = None,
    ota_required_source_version: str | None = None,
    package_verifier: Callable[[Path], object] | None = None,
) -> dict[str, object]:
    """Create one deterministic, board-bound product delivery ZIP."""

    version_generation(version)
    package = package.absolute()
    build_manifest = build_manifest.absolute()
    output = output.absolute()
    if output.suffix.lower() != ".zip":
        raise ProductError("product delivery output must use the .zip suffix")
    if output.exists() or output.is_symlink():
        raise ProductError(f"product delivery output already exists: {output}")

    package_document, _, package_report = _package_target_layout(package)
    package_security = str(package_report["security"])
    if package_security not in {"unsigned", "signed"}:
        raise ProductError("recovery component must be direct or signed-full")
    if package_security == "signed":
        if package_verifier is None:
            raise ProductError(
                "signed recovery delivery requires cryptographic verification"
            )
        package_verifier(package)
    signed_version = _package_version(package_document)
    if signed_version is not None and signed_version != version:
        raise ProductError("recovery package version differs from product version")
    build_data = _regular_bytes(build_manifest, "build manifest")
    _validate_build_manifest(build_data, package_document, package_security)

    target = package_document["target"]
    layout = package_document["layout"]
    flash_size = layout["flash_size"]
    if len(recovery.data) != flash_size:
        raise ProductError("recovery image does not cover the complete Flash")
    refreshed_evidence = load_base_evidence(base_evidence.source, target, layout)
    if (
        refreshed_evidence.sha256 != base_evidence.sha256
        or recovery.base_evidence_sha256 != base_evidence.sha256
        or recovery.base_sha256 != base_evidence.base_sha256
    ):
        raise ProductError("recovery and accepted-base evidence do not match")
    policy_data = _regular_bytes(policy.source, "release policy")
    embedded_policy = _parse_policy(
        policy_data, policy.source, tuple(layout["partitions"])
    )
    if embedded_policy.sha256 != policy.sha256:
        raise ProductError("release policy changed while delivery was created")

    board = target["physical_board"]
    safe_version = version.replace("+", "-")
    recovery_package_path = f"recovery/firmware-{board}-v{safe_version}-recovery.bkpack"
    operator_path = f"recovery/{board}-v{safe_version}-full-flash.bin"
    base_evidence_path = "evidence/accepted-base.json"
    build_path = "evidence/build-manifest.json"
    policy_path = "evidence/release-policy.csv"
    package_data = _regular_bytes(package, "recovery package")
    members: dict[str, bytes] = {
        base_evidence_path: base_evidence.data,
        build_path: build_data,
        policy_path: policy_data,
        recovery_package_path: package_data,
        operator_path: recovery.data,
    }

    ota_component: dict[str, object]
    if ota_package is None:
        if ota_required_source_version is not None:
            raise ProductError("--ota-required-source-version requires an OTA package")
        ota_component = {"status": "not-included"}
    else:
        if (
            ota_required_source_version is None
            or VERSION_RE.fullmatch(ota_required_source_version) is None
        ):
            raise ProductError(
                "OTA required source version must use " "MAJOR.MINOR.PATCH+GENERATION"
            )
        if version_generation(ota_required_source_version) >= version_generation(
            version
        ):
            raise ProductError(
                "OTA required source generation must precede target generation"
            )
        ota_document, _, ota_report = _package_target_layout(ota_package)
        if (
            ota_report["security"] != "signed-ota"
            or ota_document.get("target") != target
            or ota_document.get("layout", {}).get("identity") != layout["identity"]
            or ota_document.get("layout", {}).get("sha256") != layout["sha256"]
        ):
            raise ProductError("OTA package target/layout is incompatible")
        if _package_version(ota_document) != version:
            raise ProductError("OTA target version differs from product version")
        if package_verifier is None:
            raise ProductError("OTA delivery requires cryptographic verification")
        package_verifier(ota_package)
        security = ota_document["security"]
        required_source_root = security.get("mcuboot_public_fingerprint")
        if not isinstance(required_source_root, str) or not DIGEST_RE.fullmatch(
            required_source_root
        ):
            raise ProductError("OTA package trusted root is malformed")
        ota_path = f"ota/{ota_required_source_version}-to-{safe_version}.bkpack"
        ota_data = _regular_bytes(ota_package, "OTA package")
        members[ota_path] = ota_data
        ota_component = {
            "compatibility": "operator-precondition",
            "package": _file_row(ota_path, ota_data),
            "required_source_root": required_source_root,
            "required_source_version": ota_required_source_version,
            "status": "included",
        }

    installed_root: str | None = None
    if package_security == "signed":
        security = package_document.get("security")
        if (
            not isinstance(security, dict)
            or not isinstance(security.get("mcuboot_public_fingerprint"), str)
            or DIGEST_RE.fullmatch(str(security["mcuboot_public_fingerprint"])) is None
        ):
            raise ProductError("signed recovery installed root is malformed")
        installed_root = str(security["mcuboot_public_fingerprint"])

    factory_status = {
        "provision-required": "requires-provisioning",
        "external-provisioned": "not-included",
        "device-firstboot": "requires-device-firstboot",
    }[policy.factory_mode]
    release = {
        "build_manifest": {
            "path": build_path,
            "sha256": _digest(build_data),
        },
        "components": {
            "factory": {
                "mode": policy.factory_mode,
                "status": factory_status,
            },
            "ota": ota_component,
            "recovery": {
                "accepted_base": {
                    "device_id": base_evidence.device_id,
                    "evidence": _file_row(base_evidence_path, base_evidence.data),
                    "sha256": recovery.base_sha256,
                },
                "boot": "direct" if package_security == "unsigned" else "mcuboot",
                "installed_root": installed_root,
                "operator": {
                    **_file_row(operator_path, recovery.data),
                    "flash_end": flash_size,
                    "flash_offset": 0,
                },
                "package": _file_row(recovery_package_path, package_data),
                "partition_states": list(recovery.states),
                "scope": "accepted-device-base",
                "security": package_security,
                "status": "included",
            },
        },
        "format": DELIVERY_FORMAT,
        "layout": {
            "flash_size": flash_size,
            "identity": layout["identity"],
            "sha256": layout["sha256"],
        },
        "release_policy": {
            "factory_mode": policy.factory_mode,
            "format": POLICY_FORMAT,
            "path": policy_path,
            "sha256": _digest(policy_data),
        },
        "target": target,
        "version": version,
    }
    release_data = _canonical(release)
    members[DELIVERY_MANIFEST] = release_data
    members[DELIVERY_FLASHING] = _flashing(release)
    members[DELIVERY_CHECKSUMS] = _checksums(members)

    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with zipfile.ZipFile(temporary, "w", allowZip64=False) as archive:
            archive.writestr(
                package_domain._entry(DELIVERY_MANIFEST, release_data),
                release_data,
            )
            for name in sorted(set(members) - {DELIVERY_MANIFEST}):
                archive.writestr(
                    package_domain._entry(name, members[name]), members[name]
                )
        verify_delivery(temporary, package_verifier=package_verifier)
        package_domain._publish_no_replace(temporary, output, "product delivery")
    finally:
        temporary.unlink(missing_ok=True)
    return {
        "delivery": str(output),
        "device_id": base_evidence.device_id,
        "factory": factory_status,
        "ota": ota_component["status"],
        "operator_sha256": _digest(recovery.data),
        "operator_size": len(recovery.data),
        "physical_board": board,
        "sha256": _digest(output.read_bytes()),
        "version": version,
    }


def _temporary_package(data: bytes) -> tuple[Path, int]:
    descriptor, name = tempfile.mkstemp(prefix="bk7258-product-package-")
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    return Path(name), len(data)


def _member(
    document: Mapping[str, object], members: Mapping[str, bytes], label: str
) -> bytes:
    path = document.get("path")
    size = document.get("size")
    digest = document.get("sha256")
    if (
        not isinstance(path, str)
        or path not in members
        or not isinstance(size, int)
        or isinstance(size, bool)
        or not isinstance(digest, str)
        or DIGEST_RE.fullmatch(digest) is None
    ):
        raise ProductError(f"delivery {label} metadata is malformed")
    data = members[path]
    if len(data) != size or _digest(data) != digest:
        raise ProductError(f"delivery {label} identity changed")
    return data


def verify_delivery(
    path: Path,
    *,
    package_verifier: Callable[[Path], object] | None = None,
) -> dict[str, object]:
    """Independently verify one product delivery and its embedded packages."""

    path = path.absolute()
    try:
        members = package_domain._read_stored_zip(
            path,
            label="product delivery",
            first_member=DELIVERY_MANIFEST,
            maximum_size=MAX_DELIVERY_SIZE,
        )
    except package_domain.PackageError as error:
        raise ProductError(str(error)) from error
    try:
        document = json.loads(members[DELIVERY_MANIFEST].decode("utf-8"))
    except (KeyError, UnicodeError, json.JSONDecodeError) as error:
        raise ProductError("product release manifest is not valid JSON") from error
    if (
        not isinstance(document, dict)
        or _canonical(document) != members[DELIVERY_MANIFEST]
        or set(document)
        != {
            "build_manifest",
            "components",
            "format",
            "layout",
            "release_policy",
            "target",
            "version",
        }
        or document.get("format") != DELIVERY_FORMAT
        or not isinstance(document.get("version"), str)
        or VERSION_RE.fullmatch(document["version"]) is None
    ):
        raise ProductError("product release manifest is unsupported")
    version_generation(document["version"])
    target = document.get("target")
    layout_summary = document.get("layout")
    policy_row = document.get("release_policy")
    build_row = document.get("build_manifest")
    components = document.get("components")
    if (
        not isinstance(target, dict)
        or set(target) != {"board_family", "physical_board"}
        or target.get("board_family") != "bk7258"
        or not isinstance(target.get("physical_board"), str)
        or re.fullmatch(r"[a-z][a-z0-9_]*", str(target.get("physical_board"))) is None
        or not isinstance(layout_summary, dict)
        or set(layout_summary) != {"flash_size", "identity", "sha256"}
        or not isinstance(policy_row, dict)
        or set(policy_row)
        != {
            "factory_mode",
            "format",
            "path",
            "sha256",
        }
        or not isinstance(build_row, dict)
        or set(build_row) != {"path", "sha256"}
        or not isinstance(components, dict)
        or set(components) != {"factory", "ota", "recovery"}
    ):
        raise ProductError("product release target/layout/components are malformed")

    recovery_row = components["recovery"]
    factory_row = components["factory"]
    ota_row = components["ota"]
    if (
        not isinstance(recovery_row, dict)
        or set(recovery_row)
        != {
            "accepted_base",
            "boot",
            "installed_root",
            "operator",
            "package",
            "partition_states",
            "scope",
            "security",
            "status",
        }
        or recovery_row.get("status") != "included"
        or recovery_row.get("scope") != "accepted-device-base"
        or recovery_row.get("security") not in {"unsigned", "signed"}
        or not isinstance(factory_row, dict)
        or set(factory_row) != {"mode", "status"}
        or factory_row.get("status") not in {"requires-provisioning", "not-included"}
        or not isinstance(ota_row, dict)
        or ota_row.get("status") not in {"included", "not-included"}
    ):
        raise ProductError("product component contract is malformed")

    package_meta = recovery_row.get("package")
    operator_meta = recovery_row.get("operator")
    if (
        not isinstance(package_meta, dict)
        or set(package_meta) != {"path", "sha256", "size"}
        or not isinstance(operator_meta, dict)
        or set(operator_meta)
        != {
            "flash_end",
            "flash_offset",
            "path",
            "sha256",
            "size",
        }
    ):
        raise ProductError("recovery file metadata is malformed")
    package_data = _member(package_meta, members, "recovery package")
    operator_data = _member(operator_meta, members, "recovery operator")
    operator = recovery_row["operator"]
    flash_size = layout_summary.get("flash_size")
    if (
        not isinstance(flash_size, int)
        or isinstance(flash_size, bool)
        or flash_size <= 0
        or len(operator_data) != flash_size
        or operator.get("flash_offset") != 0
        or operator.get("flash_end") != flash_size
    ):
        raise ProductError("recovery operator is not one complete Flash image")

    accepted_base = recovery_row.get("accepted_base")
    if (
        not isinstance(accepted_base, dict)
        or set(accepted_base)
        != {
            "device_id",
            "evidence",
            "sha256",
        }
        or not isinstance(accepted_base.get("device_id"), str)
        or DEVICE_ID_RE.fullmatch(accepted_base["device_id"]) is None
        or not isinstance(accepted_base.get("sha256"), str)
        or DIGEST_RE.fullmatch(accepted_base["sha256"]) is None
    ):
        raise ProductError("recovery accepted-base metadata is malformed")
    evidence_meta = accepted_base.get("evidence")
    if not isinstance(evidence_meta, dict) or set(evidence_meta) != {
        "path",
        "sha256",
        "size",
    }:
        raise ProductError("accepted-base evidence file metadata is malformed")
    base_evidence_data = _member(evidence_meta, members, "accepted-base evidence")
    parsed_base = _parse_base_evidence(
        base_evidence_data,
        Path(str(accepted_base["evidence"]["path"])),
        target,
        layout_summary,
    )
    if (
        parsed_base.device_id != accepted_base["device_id"]
        or parsed_base.base_sha256 != accepted_base["sha256"]
    ):
        raise ProductError("recovery accepted-base identity changed")

    temporary, _ = _temporary_package(package_data)
    try:
        package_document, package_members, package_report = _package_target_layout(
            temporary
        )
        if package_report["security"] == "signed":
            if package_verifier is None:
                raise ProductError(
                    "signed recovery delivery needs cryptographic verification"
                )
            package_verifier(temporary)
    finally:
        temporary.unlink(missing_ok=True)
    if (
        package_document.get("target") != target
        or package_document.get("layout", {}).get("identity")
        != layout_summary.get("identity")
        or package_document.get("layout", {}).get("sha256")
        != layout_summary.get("sha256")
        or package_document.get("layout", {}).get("flash_size") != flash_size
        or package_report["security"] != recovery_row["security"]
        or recovery_row.get("boot")
        != ("direct" if recovery_row["security"] == "unsigned" else "mcuboot")
    ):
        raise ProductError("recovery package target/layout/security changed")
    package_security = package_document.get("security")
    installed_root = recovery_row.get("installed_root")
    if recovery_row["security"] == "unsigned":
        if installed_root is not None:
            raise ProductError("unsigned recovery cannot install a trust root")
    elif (
        not isinstance(package_security, dict)
        or not isinstance(installed_root, str)
        or DIGEST_RE.fullmatch(installed_root) is None
        or package_security.get("mcuboot_public_fingerprint") != installed_root
    ):
        raise ProductError("signed recovery installed root changed")
    package_version = _package_version(package_document)
    if package_version is not None and package_version != document["version"]:
        raise ProductError("recovery package version changed")

    build_path = build_row.get("path")
    build_digest = build_row.get("sha256")
    if (
        not isinstance(build_path, str)
        or build_path not in members
        or not isinstance(build_digest, str)
        or DIGEST_RE.fullmatch(build_digest) is None
        or _digest(members[build_path]) != build_digest
    ):
        raise ProductError("delivery build manifest identity changed")
    _validate_build_manifest(
        members[build_path], package_document, str(package_report["security"])
    )

    policy_path = policy_row.get("path")
    if (
        not isinstance(policy_path, str)
        or policy_path not in members
        or policy_row.get("format") != POLICY_FORMAT
        or policy_row.get("factory_mode") != factory_row.get("mode")
        or not isinstance(policy_row.get("sha256"), str)
        or _digest(members[policy_path]) != policy_row["sha256"]
    ):
        raise ProductError("delivery release policy identity changed")
    parsed_policy = _parse_policy(
        members[policy_path],
        Path(policy_path),
        tuple(package_document["layout"]["partitions"]),
    )
    if parsed_policy.factory_mode != factory_row.get("mode"):
        raise ProductError("factory mode changed")

    states = recovery_row.get("partition_states")
    if not isinstance(states, list) or not states:
        raise ProductError("recovery partition state evidence is missing")
    cursor = 0
    state_by_name: dict[str, dict[str, object]] = {}
    for row in states:
        if not isinstance(row, dict) or set(row) != {
            "action",
            "after_sha256",
            "before_sha256",
            "name",
            "offset",
            "policy",
            "size",
        }:
            raise ProductError("recovery partition state row is malformed")
        name = row.get("name")
        offset = row.get("offset")
        size = row.get("size")
        before = row.get("before_sha256")
        after = row.get("after_sha256")
        if (
            not isinstance(name, str)
            or name in state_by_name
            or not isinstance(offset, int)
            or not isinstance(size, int)
            or isinstance(offset, bool)
            or isinstance(size, bool)
            or offset != cursor
            or size <= 0
            or offset + size > flash_size
            or not isinstance(before, str)
            or DIGEST_RE.fullmatch(before) is None
            or not isinstance(after, str)
            or DIGEST_RE.fullmatch(after) is None
            or _digest(operator_data[offset : offset + size]) != after
        ):
            raise ProductError("recovery partition state geometry/hash changed")
        state_by_name[name] = row
        cursor += size
    if cursor != flash_size:
        raise ProductError("recovery state evidence does not cover complete Flash")

    policy_map = parsed_policy.by_partition
    partition_by_name = {
        row["name"]: row for row in package_document["layout"]["partitions"]
    }
    for name, release_policy in policy_map.items():
        row = state_by_name.get(name)
        partition = partition_by_name[name]
        if (
            row is None
            or row.get("policy") != release_policy
            or row.get("offset") != partition["offset"]
            or row.get("size") != partition["size"]
        ):
            raise ProductError(f"release policy state changed: {name}")
        if release_policy in {
            "preserve",
            "device-unique",
            "factory-init",
            "immutable",
        } and (
            row.get("action") != "preserved"
            or row.get("before_sha256") != row.get("after_sha256")
        ):
            raise ProductError(f"protected partition was not preserved: {name}")
        if release_policy == "transactional":
            start = partition["offset"]
            end = start + partition["size"]
            if (
                row.get("action") != "reset"
                or operator_data[start:end]
                != bytes([image_domain.ERASE_BYTE]) * partition["size"]
            ):
                raise ProductError(f"transactional partition was not reset: {name}")

    image_partitions: set[str] = set()
    for row in package_document["images"]:
        partition_name = row["partition"]
        partition = partition_by_name[partition_name]
        state = state_by_name[partition_name]
        if policy_map[partition_name] != "replace" or state.get("action") != "replaced":
            raise ProductError("package write/release policy evidence changed")
        start = partition["offset"]
        expected = bytearray([image_domain.ERASE_BYTE] * partition["size"])
        data = package_members[row["member"]]
        expected[: len(data)] = data
        if operator_data[start : start + partition["size"]] != expected:
            raise ProductError(f"recovery bytes changed: {partition_name}")
        image_partitions.add(partition_name)
    for name, value in policy_map.items():
        if value == "replace" and name not in image_partitions:
            state = state_by_name[name]
            if state.get("action") != "carried-forward-external" or state.get(
                "before_sha256"
            ) != state.get("after_sha256"):
                raise ProductError(f"external replacement state changed: {name}")

    gateway_release: dict[str, object] | None = None
    expected_members = {
        DELIVERY_MANIFEST,
        DELIVERY_CHECKSUMS,
        DELIVERY_FLASHING,
        accepted_base["evidence"]["path"],
        build_path,
        policy_path,
        recovery_row["package"]["path"],
        recovery_row["operator"]["path"],
    }
    if ota_row["status"] == "included":
        if (
            set(ota_row)
            != {
                "compatibility",
                "package",
                "required_source_root",
                "required_source_version",
                "status",
            }
            or ota_row.get("compatibility") != "operator-precondition"
            or not isinstance(ota_row.get("required_source_version"), str)
            or VERSION_RE.fullmatch(ota_row["required_source_version"]) is None
            or version_generation(ota_row["required_source_version"])
            >= version_generation(document["version"])
            or not isinstance(ota_row.get("required_source_root"), str)
            or DIGEST_RE.fullmatch(ota_row["required_source_root"]) is None
        ):
            raise ProductError("OTA compatibility metadata is malformed")
        if not isinstance(ota_row.get("package"), dict) or set(ota_row["package"]) != {
            "path",
            "sha256",
            "size",
        }:
            raise ProductError("OTA package metadata is malformed")
        ota_data = _member(ota_row["package"], members, "OTA package")
        expected_members.add(ota_row["package"]["path"])
        ota_temporary, _ = _temporary_package(ota_data)
        try:
            ota_document, _, ota_report = _package_target_layout(ota_temporary)
            _, _, _, ota_catalog, _ = package_domain.trust_material(ota_temporary)
            if ota_catalog is None:
                raise ProductError("OTA delivery has no signed catalog")
            if package_verifier is None:
                raise ProductError("OTA delivery needs cryptographic verification")
            package_verifier(ota_temporary)
            if _digest(ota_temporary.read_bytes()) != ota_row["package"]["sha256"]:
                raise ProductError("OTA package changed during verification")
        finally:
            ota_temporary.unlink(missing_ok=True)
        if (
            ota_report["security"] != "signed-ota"
            or ota_document.get("target") != target
            or ota_document.get("layout", {}).get("identity")
            != layout_summary["identity"]
            or _package_version(ota_document) != document["version"]
            or ota_document.get("security", {}).get("mcuboot_public_fingerprint")
            != ota_row["required_source_root"]
        ):
            raise ProductError("OTA package compatibility changed")
        gateway_release = {
            "board_family": target["board_family"],
            "device_id": accepted_base["device_id"],
            "layout_identity": layout_summary["identity"],
            "layout_sha256": layout_summary["sha256"],
            "manifest_sha256": _digest(ota_catalog),
            "package_sha256": ota_row["package"]["sha256"],
            "package_size_bytes": ota_row["package"]["size"],
            "physical_board": target["physical_board"],
            "required_source_root_sha256": ota_row["required_source_root"],
            "required_source_version": ota_row["required_source_version"],
            "target_version": document["version"],
        }
        if not 0 < gateway_release["package_size_bytes"] <= MAX_GATEWAY_PACKAGE_SIZE:
            raise ProductError("OTA package exceeds the Gateway release limit")
    elif set(ota_row) != {"status"}:
        raise ProductError("absent OTA component has unexpected metadata")

    if (
        set(members) != expected_members
        or members[DELIVERY_CHECKSUMS] != _checksums(members)
        or members[DELIVERY_FLASHING] != _flashing(document)
    ):
        raise ProductError("product delivery members/checksums/guide changed")
    return {
        "delivery": str(path),
        "device_id": accepted_base["device_id"],
        "factory": factory_row["status"],
        "firmware_release": gateway_release,
        "ota": ota_row["status"],
        "operator_sha256": recovery_row["operator"]["sha256"],
        "operator_size": flash_size,
        "physical_board": target["physical_board"],
        "sha256": _digest(path.read_bytes()),
        "version": document["version"],
    }


def create_gateway_release_registry(
    deliveries: tuple[Path, ...],
    output: Path,
    *,
    package_verifier: Callable[[Path], object],
) -> dict[str, object]:
    """Verify product deliveries and publish a metadata-only Gateway registry."""

    if not callable(package_verifier):
        raise ProductError(
            "Gateway release registry requires cryptographic verification"
        )
    if not 1 <= len(deliveries) <= MAX_GATEWAY_RELEASES:
        raise ProductError(
            f"Gateway release registry requires 1-{MAX_GATEWAY_RELEASES} deliveries"
        )

    releases: list[dict[str, object]] = []
    for delivery in deliveries:
        report = verify_delivery(delivery, package_verifier=package_verifier)
        release = report.get("firmware_release")
        if not isinstance(release, dict):
            raise ProductError(f"delivery has no verified OTA component: {delivery}")
        package_size = release.get("package_size_bytes")
        if (
            type(package_size) is not int
            or not 0 < package_size <= MAX_GATEWAY_PACKAGE_SIZE
        ):
            raise ProductError("OTA package exceeds the Gateway release limit")
        releases.append(dict(release))

    identities = [(row["device_id"], row["manifest_sha256"]) for row in releases]
    targets = [(row["device_id"], row["target_version"]) for row in releases]
    if len(set(identities)) != len(identities):
        raise ProductError("duplicate device/release manifest in Gateway registry")
    if len(set(targets)) != len(targets):
        raise ProductError("duplicate device/target version in Gateway registry")

    releases.sort(
        key=lambda row: (
            str(row["device_id"]),
            version_generation(str(row["target_version"])),
            str(row["target_version"]),
            str(row["manifest_sha256"]),
        )
    )
    data = _canonical(
        {
            "format": GATEWAY_RELEASE_REGISTRY_FORMAT,
            "releases": releases,
        }
    )

    output = output.absolute()
    if output.suffix.lower() != ".json":
        raise ProductError("Gateway release registry output must use the .json suffix")
    if output.exists() or output.is_symlink():
        raise ProductError(f"Gateway release registry output already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    temporary = Path(temporary_name)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        package_domain._publish_no_replace(
            temporary, output, "Gateway release registry"
        )
        if hasattr(os, "O_DIRECTORY"):
            directory = os.open(output.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        temporary.unlink(missing_ok=True)

    return {
        "output": str(output),
        "releases": len(releases),
        "sha256": _digest(data),
    }


def release_identity(
    manifest: build_domain.BuildManifest,
    version: str,
    product: str | None,
    artifact_id: str | None,
) -> dict[str, object] | None:
    return artifact_identity(
        manifest.physical_board, manifest.provenance, version, product, artifact_id
    )


def artifact_identity(
    board: str,
    provenance: dict[str, object] | None,
    version: str,
    product: str | None,
    artifact_id: str | None,
) -> dict[str, object] | None:
    if provenance is None:
        if product is not None or artifact_id is not None:
            raise ValueError(
                "new artifact identity requires a build manifest with actual profiles"
            )
        return None  # Historical /2 invocation retains its names.
    selected = product or provenance["product"]
    if provenance["product"] is not None and selected != provenance["product"]:
        raise ValueError("release product differs from build provenance")
    if (
        not isinstance(selected, str)
        or re.fullmatch(r"[a-z][a-z0-9_-]{0,47}", selected) is None
    ):
        raise ValueError("release requires an explicit --product or build product")
    identifier = artifact_id
    if (
        not isinstance(identifier, str)
        or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,47}", identifier) is None
    ):
        raise ValueError("artifact-id must be a safe stable identifier")
    profiles = provenance["profiles"]
    profile = "__".join(PurePosixPath(profiles[role]).name for role in ("cp", "ap"))
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,95}", profile) is None:
        raise ValueError("profile cannot form a safe artifact name")
    return {
        "product": selected,
        "chip": "bk7258",
        "board": board,
        "profile": profile,
        "version": version,
        "artifact_id": identifier,
        "security_counter": version_generation(version),
        "counter_policy": "legacy-version-build-equals-security-counter",
    }


def artifact_stem(identity: dict[str, object], kind: str) -> str:
    if kind not in {"full", "ota"}:
        raise ValueError("unsupported artifact kind")
    return (
        f"{identity['product']}-{identity['chip']}-{identity['board']}-"
        f"{identity['profile']}-v{identity['version']}-b{identity['artifact_id']}-{kind}"
    )


def load_release(
    root: Path, expected_mode: str
) -> tuple[dict[str, object], Path, Path, Path | None, Path | None]:
    """Load one atomic signed-release directory without trusting its paths."""

    root = root.absolute()
    if root.is_symlink() or not root.is_dir():
        raise package_domain.PackageError(
            f"{expected_mode} release must be a non-symlink directory: {root}"
        )
    summary = root / "release.json"
    if summary.is_symlink() or not summary.is_file():
        raise package_domain.PackageError(
            f"{expected_mode} release has no regular release.json"
        )
    data = summary.read_bytes()
    try:
        document = json.loads(data.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise package_domain.PackageError(
            f"{expected_mode} release.json is malformed"
        ) from error
    canonical = (
        json.dumps(document, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")
    if (
        not isinstance(document, dict)
        or data != canonical
        or document.get("format") != "bk7258.release/2"
        or document.get("mode") != expected_mode
    ):
        raise package_domain.PackageError(
            f"unsupported {expected_mode} release directory"
        )

    def member(row: object, label: str) -> Path:
        if (
            not isinstance(row, dict)
            or not isinstance(row.get("path"), str)
            or not isinstance(row.get("sha256"), str)
            or re.fullmatch(r"[0-9a-f]{64}", row["sha256"]) is None
        ):
            raise package_domain.PackageError(
                f"{expected_mode} release {label} metadata is malformed"
            )
        relative = PurePosixPath(row["path"])
        if (
            relative.is_absolute()
            or not relative.parts
            or any(part in {"", ".", ".."} for part in relative.parts)
        ):
            raise package_domain.PackageError(
                f"{expected_mode} release {label} path is unsafe"
            )
        selected = root.joinpath(*relative.parts)
        if selected.is_symlink() or not selected.is_file():
            raise package_domain.PackageError(
                f"{expected_mode} release {label} is not a regular file"
            )
        try:
            selected.resolve(strict=True).relative_to(root.resolve(strict=True))
        except (OSError, ValueError) as error:
            raise package_domain.PackageError(
                f"{expected_mode} release {label} escapes its directory"
            ) from error
        digest = hashlib.sha256()
        with selected.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != row["sha256"]:
            raise package_domain.PackageError(
                f"{expected_mode} release {label} hash changed"
            )
        if "size" in row and row["size"] != selected.stat().st_size:
            raise package_domain.PackageError(
                f"{expected_mode} release {label} size changed"
            )
        return selected

    package = member(document.get("package"), "package")
    build_manifest = member(document.get("build_manifest"), "build manifest")
    if "identity" in document:
        identity = document["identity"]
        if not isinstance(identity, dict):
            raise ValueError("release artifact identity is malformed")
        copied_build = json.loads(build_manifest.read_text(encoding="utf-8"))
        provenance = build_domain.validate_provenance(copied_build.get("provenance"))
        target = copied_build.get("target")
        if not isinstance(target, dict) or target != document.get("target"):
            raise ValueError("release artifact target differs from build evidence")
        expected = artifact_identity(
            target.get("physical_board"),
            provenance,
            document.get("version"),
            identity.get("product"),
            identity.get("artifact_id"),
        )
        if identity != expected or identity["security_counter"] != document.get(
            "generation"
        ):
            raise ValueError("release artifact identity differs from build evidence")
        if package.name != artifact_stem(identity, expected_mode) + ".bkpack":
            raise ValueError("release package name differs from artifact identity")
    operator = (
        member(document.get("operator"), "operator")
        if expected_mode == "full"
        else None
    )
    materialization = document.get("materialization")
    base_evidence = None
    if expected_mode == "full":
        if not isinstance(materialization, dict):
            raise package_domain.PackageError(
                "full release has no materialization evidence"
            )
        base_evidence = member(
            materialization.get("accepted_base"), "accepted-base evidence"
        )
    return document, package, build_manifest, operator, base_evidence


def release_product(
    repository: Path,
    *,
    full_release: Path,
    base: Path,
    output: Path,
    ota_release: Path | None,
    ota_required_source_version: str | None,
    package_verifier: Callable[[Path], object],
) -> dict[str, object]:
    """Validate existing release evidence and assemble one product delivery.

    Verification is supplied by the caller; this operation never signs or
    provisions keys and has no CLI, transport or device-control dependency.
    """
    full, full_package, build_manifest, operator, base_evidence_path = load_release(
        full_release, "full"
    )
    if operator is None or base_evidence_path is None:
        raise package_domain.PackageError(
            "full release has no recovery operator/base evidence"
        )
    materialization = full.get("materialization")
    if (
        not isinstance(materialization, dict)
        or set(materialization)
        != {
            "accepted_base",
            "flash_end",
            "flash_offset",
            "flash_size",
        }
        or materialization.get("flash_offset") != 0
        or materialization.get("flash_end") != materialization.get("flash_size")
        or operator.stat().st_size != materialization.get("flash_size")
    ):
        raise package_domain.PackageError(
            "full release is not a complete-Flash recovery release"
        )
    manifest = validate_build_manifest_evidence(build_manifest, full_package)
    target = full.get("target")
    layout = full.get("layout")
    manifest_target = manifest.get("target")
    manifest_layout = manifest.get("layout")
    if (
        not isinstance(target, dict)
        or not isinstance(layout, dict)
        or not isinstance(manifest_target, dict)
        or not isinstance(manifest_layout, dict)
        or target != manifest_target
        or layout.get("identity") != manifest_layout.get("identity")
        or layout.get("sha256") != manifest_layout.get("sha256")
        or full.get("version") is None
    ):
        raise package_domain.PackageError(
            "full release summary/build manifest identity changed"
        )
    package_verifier(full_package)
    physical_board = target.get("physical_board")
    if not isinstance(physical_board, str):
        raise package_domain.PackageError("full release target is malformed")
    preset = build_domain.board_preset(repository, physical_board)
    selected_layout = layout_domain.load(preset.partition)
    if selected_layout.identity != layout.get(
        "identity"
    ) or selected_layout.sha256 != layout.get("sha256"):
        raise package_domain.PackageError(
            "full release layout differs from the selected board declaration"
        )
    policy = load_policy(preset.release_policy, selected_layout)
    base_evidence = load_base_evidence(base_evidence_path, target, selected_layout)
    accepted_base = materialization.get("accepted_base")
    if (
        not isinstance(accepted_base, dict)
        or set(accepted_base) != {"device_id", "path", "sha256", "size"}
        or accepted_base.get("device_id") != base_evidence.device_id
        or accepted_base.get("sha256") != base_evidence.sha256
        or accepted_base.get("size") != len(base_evidence.data)
    ):
        raise package_domain.PackageError("full release accepted-base identity changed")
    recovery = materialize_recovery(full_package, policy, base, base_evidence)
    if recovery.data != operator.read_bytes():
        raise package_domain.PackageError(
            "full release operator differs from policy materialization"
        )

    ota_package = None
    if ota_release is None:
        if ota_required_source_version is not None:
            raise package_domain.PackageError(
                "--ota-required-source-version requires --ota-release"
            )
    else:
        ota, ota_package, ota_manifest, ota_operator, ota_base = load_release(
            ota_release, "ota"
        )
        ota_build = validate_build_manifest_evidence(ota_manifest, ota_package)
        ota_target = ota_build.get("target")
        ota_layout = ota_build.get("layout")
        if (
            ota_operator is not None
            or ota_base is not None
            or ota_target != target
            or not isinstance(ota_layout, dict)
            or ota_layout.get("identity") != selected_layout.identity
            or ota_layout.get("sha256") != selected_layout.sha256
            or ota.get("target") != target
            or ota.get("layout") != layout
            or ota.get("version") != full.get("version")
        ):
            raise package_domain.PackageError(
                "full and OTA release identities are incompatible"
            )
        if ota_required_source_version is None:
            raise package_domain.PackageError(
                "--ota-release requires --ota-required-source-version"
            )
        package_verifier(ota_package)

    report = create_delivery(
        package=full_package,
        build_manifest=build_manifest,
        policy=policy,
        recovery=recovery,
        base_evidence=base_evidence,
        version=str(full["version"]),
        output=output,
        ota_package=ota_package,
        ota_required_source_version=ota_required_source_version,
        package_verifier=package_verifier,
    )
    return report
