# SPDX-License-Identifier: Apache-2.0
"""Bounded, device-authorized HTTPS exposure of verified OTA package members."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import struct
import zipfile
from dataclasses import dataclass
from http import HTTPStatus
from pathlib import PurePosixPath
from typing import TYPE_CHECKING

from .firmware import FirmwareRelease, FirmwareReleases, MAX_PACKAGE_SIZE

if TYPE_CHECKING:
    from .server import GatewayServer


_DIGEST = re.compile(r"[0-9a-f]{64}", re.ASCII)
_RANGE = re.compile(r"bytes=(0|[1-9][0-9]*)-(0|[1-9][0-9]*)", re.ASCII)
_MEMBER = re.compile(r"images/(?:cp|ap)/[^/]+", re.ASCII)
_MAX_RANGE_BYTES = 16_384
_LOCAL_HEADER = struct.Struct("<IHHHHHIIIHH")


class FirmwareContentError(ValueError):
    """A package is not safe to expose through the companion listener."""


@dataclass(frozen=True, slots=True)
class _Member:
    name: str
    offset: int
    size: int
    sha256: str
    media_type: str


@dataclass(slots=True)
class _Package:
    release: FirmwareRelease
    fd: int
    members: dict[str, _Member]


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("utf-8")


def _safe_name(name: object) -> str:
    if not isinstance(name, str) or not name or name.startswith("/") or "\\" in name:
        raise FirmwareContentError("unsafe package member")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        raise FirmwareContentError("unsafe package member")
    return name


def _read_exact(fd: int, offset: int, size: int) -> bytes:
    data = os.pread(fd, size, offset)
    if len(data) != size:
        raise FirmwareContentError("truncated package member")
    return data


def _member_offset(fd: int, info: zipfile.ZipInfo) -> int:
    raw = _read_exact(fd, info.header_offset, _LOCAL_HEADER.size)
    fields = _LOCAL_HEADER.unpack(raw)
    if fields[0] != 0x04034B50:
        raise FirmwareContentError("invalid package local header")
    name_size, extra_size = fields[-2:]
    if _read_exact(fd, info.header_offset + _LOCAL_HEADER.size, name_size) != info.filename.encode("utf-8"):
        raise FirmwareContentError("package member name mismatch")
    return info.header_offset + _LOCAL_HEADER.size + name_size + extra_size


def _read_catalog(catalog: bytes, release: FirmwareRelease, names: set[str]) -> tuple[str, str]:
    try:
        value = json.loads(catalog.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise FirmwareContentError("invalid OTA catalog") from error
    fields = {"format", "board_family", "target", "layout", "version",
              "security_counter", "cp", "ap", "package_id"}
    if not isinstance(value, dict) or set(value) != fields or _canonical(value) != catalog:
        raise FirmwareContentError("noncanonical OTA catalog")
    if (value["format"] != "bk7258.ota/2" or value["board_family"] != release.board_family
            or value["target"] != {"board_family": release.board_family,
                                    "physical_board": release.physical_board}
            or value["layout"] != {"identity": release.layout_identity,
                                   "sha256": release.layout_sha256}
            or value["version"] != release.target_version
            or type(value["security_counter"]) is not int
            or value["security_counter"] < 0):
        raise FirmwareContentError("OTA catalog does not match release")
    base = dict(value)
    package_id = base.pop("package_id")
    if not isinstance(package_id, str) or not _DIGEST.fullmatch(package_id) \
            or hashlib.sha256(_canonical(base)).hexdigest() != package_id:
        raise FirmwareContentError("invalid OTA catalog package id")
    found: list[str] = []
    for artifact in ("cp", "ap"):
        entry = value[artifact]
        if (not isinstance(entry, dict) or set(entry) != {"uri", "size", "sha256"}
                or not isinstance(entry["uri"], str) or not _MEMBER.fullmatch(entry["uri"])
                or type(entry["size"]) is not int or not 0 < entry["size"] <= MAX_PACKAGE_SIZE
                or not isinstance(entry["sha256"], str) or not _DIGEST.fullmatch(entry["sha256"])
                or entry["uri"] not in names):
            raise FirmwareContentError("invalid OTA catalog image")
        if not entry["uri"].startswith(f"images/{artifact}/"):
            raise FirmwareContentError("OTA artifact member mismatch")
        found.append(entry["uri"])
    if len(set(found)) != 2:
        raise FirmwareContentError("duplicate OTA catalog image")
    return found[0], found[1]


class FirmwareContentStore:
    """Prevalidated ZIP_STORED members held by descriptor for bounded reads."""

    def __init__(self, releases: FirmwareReleases, packages: tuple[os.PathLike[str] | str, ...]):
        if not isinstance(releases, FirmwareReleases) or not packages:
            raise FirmwareContentError("firmware content requires releases and packages")
        self._by_manifest: dict[str, _Package] = {}
        opened: list[int] = []
        try:
            for value in packages:
                package = self._load_one(os.fspath(value), releases)
                manifest = package.release.manifest_sha256
                if manifest in self._by_manifest:
                    raise FirmwareContentError("duplicate firmware package")
                self._by_manifest[manifest] = package
                opened.append(package.fd)
            if len(self._by_manifest) != len(packages):
                raise FirmwareContentError("duplicate firmware package")
        except BaseException:
            for fd in opened:
                os.close(fd)
            raise

    def close(self) -> None:
        for package in self._by_manifest.values():
            if package.fd >= 0:
                os.close(package.fd)
                package.fd = -1

    def _load_one(self, name: str, releases: FirmwareReleases) -> _Package:
        fd = os.open(name, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                    or info.st_mode & 0o022 or not 0 < info.st_size <= MAX_PACKAGE_SIZE):
                raise FirmwareContentError("invalid firmware package file")
            with os.fdopen(os.dup(fd), "rb") as source:
                hasher = hashlib.sha256()
                for chunk in iter(lambda: source.read(65536), b""):
                    hasher.update(chunk)
                digest = hasher.hexdigest()
            matches = [row for row in releases.releases
                       if row.package_size_bytes == info.st_size and row.package_sha256 == digest]
            if len(matches) != 1:
                raise FirmwareContentError("package does not uniquely match release registry")
            release = matches[0]
            with os.fdopen(os.dup(fd), "rb") as source:
                with zipfile.ZipFile(source, "r") as archive:
                    infos = archive.infolist()
                    names = [_safe_name(row.filename) for row in infos]
                    if (not infos or len(names) != len(set(names)) or "catalog.json" not in names
                            or "catalog.sig" not in names):
                        raise FirmwareContentError("invalid package members")
                    records: dict[str, tuple[zipfile.ZipInfo, int]] = {}
                    for row in infos:
                        if (row.is_dir() or row.flag_bits != 0 or row.compress_type != zipfile.ZIP_STORED
                                or row.file_size != row.compress_size or row.file_size > MAX_PACKAGE_SIZE):
                            raise FirmwareContentError("compressed or encrypted package member")
                        records[row.filename] = (row, _member_offset(fd, row))
                    catalog_info, catalog_offset = records["catalog.json"]
                    sig_info, sig_offset = records["catalog.sig"]
                    if not 1 <= catalog_info.file_size <= 65536 or not 8 <= sig_info.file_size <= 80:
                        raise FirmwareContentError("invalid OTA catalog members")
                    catalog = _read_exact(fd, catalog_offset, catalog_info.file_size)
                    if hashlib.sha256(catalog).hexdigest() != release.manifest_sha256:
                        raise FirmwareContentError("catalog digest does not match release")
                    cp_name, ap_name = _read_catalog(catalog, release, set(records))
                    members: dict[str, _Member] = {
                        "catalog.json": _Member("catalog.json", catalog_offset, catalog_info.file_size,
                                                hashlib.sha256(catalog).hexdigest(), "application/json"),
                        "catalog.sig": _Member("catalog.sig", sig_offset, sig_info.file_size,
                                               hashlib.sha256(_read_exact(fd, sig_offset, sig_info.file_size)).hexdigest(),
                                               "application/octet-stream"),
                    }
                    parsed = json.loads(catalog.decode("utf-8"))
                    for artifact, member_name in (("cp", cp_name), ("ap", ap_name)):
                        row, offset = records[member_name]
                        expected = parsed[artifact]
                        image = _read_exact(fd, offset, row.file_size)
                        if row.file_size != expected["size"] or hashlib.sha256(image).hexdigest() != expected["sha256"]:
                            raise FirmwareContentError("OTA image does not match catalog")
                        members[member_name] = _Member(member_name, offset, row.file_size,
                                                       expected["sha256"], "application/octet-stream")
            return _Package(release, fd, members)
        except BaseException:
            os.close(fd)
            raise

    @staticmethod
    def _response(status: HTTPStatus, body: bytes = b"", headers: list[tuple[str, str]] | None = None):
        base = [("Cache-Control", "no-store"), ("X-Content-Type-Options", "nosniff"),
                ("Content-Length", str(len(body)))]
        return status, base + (headers or []), body

    def process_request(self, protocol, raw_path: str, headers, gateway: GatewayServer):
        if not raw_path.startswith("/firmware/"):
            return None
        if "?" in raw_path or "#" in raw_path:
            return self._response(HTTPStatus.BAD_REQUEST)
        parts = raw_path.split("/", 4)
        if len(parts) != 5 or parts[:3] != ["", "firmware", "v1"]:
            return self._response(HTTPStatus.NOT_FOUND)
        manifest, name = parts[3], parts[4]
        package = self._by_manifest.get(manifest)
        if package is None or name not in package.members:
            return self._response(HTTPStatus.NOT_FOUND)
        tls = protocol.transport.get_extra_info("ssl_object") if protocol.transport else None
        certificate = tls.getpeercert(binary_form=True) if tls is not None else None
        device_id = (gateway.device_bindings.device_for_certificate(hashlib.sha256(certificate).hexdigest())
                     if certificate is not None and gateway.device_bindings is not None else None)
        connection = gateway.device_connection(device_id) if device_id is not None else None
        ota = None if connection is None else connection.ota_status
        if (device_id != package.release.device_id or ota is None
                or ota.manifest_sha256 != manifest or ota.phase in {6, 7, 8}):
            return self._response(HTTPStatus.FORBIDDEN)
        member = package.members[name]
        ranges = headers.get_all("Range")
        if name in {"catalog.json", "catalog.sig"}:
            if ranges:
                return self._response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
            return self._response(HTTPStatus.OK, _read_exact(package.fd, member.offset, member.size),
                                  [("Content-Type", member.media_type)])
        if len(ranges) != 1:
            return self._response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
        match = _RANGE.fullmatch(ranges[0])
        if match is None:
            return self._response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
        start, end = (int(match.group(1)), int(match.group(2)))
        if start > end or end >= member.size or end - start + 1 > _MAX_RANGE_BYTES:
            return self._response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
        body = _read_exact(package.fd, member.offset + start, end - start + 1)
        return self._response(HTTPStatus.PARTIAL_CONTENT, body, [
            ("Content-Type", member.media_type), ("Accept-Ranges", "bytes"),
            ("Content-Range", f"bytes {start}-{end}/{member.size}"),
        ])
