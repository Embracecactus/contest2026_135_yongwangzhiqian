# SPDX-License-Identifier: Apache-2.0
"""Read-only projection of operator-verified, device-bound OTA releases."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType

from .devices import read_operator_registry


REGISTRY_FORMAT = 'shaniu.firmware-release-registry/1'
MAX_RELEASES = 32
MAX_PACKAGE_SIZE = 64 * 1024 * 1024

_DEVICE_ID = re.compile(r'[A-Za-z0-9][A-Za-z0-9._:-]{0,127}', re.ASCII)
_DIGEST = re.compile(r'[0-9a-f]{64}', re.ASCII)
_VERSION = re.compile(
    r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\+([1-9][0-9]*)',
    re.ASCII,
)
_PHYSICAL_BOARD = re.compile(r'[a-z][a-z0-9_]*', re.ASCII)
_LAYOUT_IDENTITY = re.compile(r'[A-Za-z0-9][A-Za-z0-9._:-]{0,127}', re.ASCII)
_FIELDS = {
    'board_family', 'device_id', 'layout_identity', 'layout_sha256',
    'manifest_sha256', 'package_sha256', 'package_size_bytes', 'physical_board',
    'required_source_root_sha256', 'required_source_version', 'target_version',
}


def _generation(version: str) -> int:
    match = _VERSION.fullmatch(version)
    if match is None:
        raise ValueError('invalid firmware version')
    return int(match.group(4))


@dataclass(frozen=True, slots=True)
class FirmwareRelease:
    device_id: str
    manifest_sha256: str
    target_version: str
    required_source_version: str
    required_source_root_sha256: str
    board_family: str
    physical_board: str
    layout_identity: str
    layout_sha256: str
    package_sha256: str
    package_size_bytes: int

    def __post_init__(self) -> None:
        if (not isinstance(self.device_id, str) or not _DEVICE_ID.fullmatch(self.device_id)
                or not isinstance(self.manifest_sha256, str)
                or not _DIGEST.fullmatch(self.manifest_sha256)
                or self.manifest_sha256 in {'0' * 64, 'f' * 64}
                or not isinstance(self.target_version, str)
                or not _VERSION.fullmatch(self.target_version)
                or not isinstance(self.required_source_version, str)
                or not _VERSION.fullmatch(self.required_source_version)
                or not isinstance(self.required_source_root_sha256, str)
                or not _DIGEST.fullmatch(self.required_source_root_sha256)
                or self.board_family != 'bk7258'
                or not isinstance(self.physical_board, str)
                or not _PHYSICAL_BOARD.fullmatch(self.physical_board)
                or not isinstance(self.layout_identity, str)
                or not _LAYOUT_IDENTITY.fullmatch(self.layout_identity)
                or not isinstance(self.layout_sha256, str)
                or not _DIGEST.fullmatch(self.layout_sha256)
                or not isinstance(self.package_sha256, str)
                or not _DIGEST.fullmatch(self.package_sha256)
                or type(self.package_size_bytes) is not int
                or not 0 < self.package_size_bytes <= MAX_PACKAGE_SIZE):
            raise ValueError('invalid firmware release')
        if _generation(self.required_source_version) >= _generation(self.target_version):
            raise ValueError('firmware release generation does not increase')

    def projection(self) -> dict[str, object]:
        return {
            'manifest_sha256': self.manifest_sha256,
            'target_version': self.target_version,
            'required_source_version': self.required_source_version,
            'required_source_root_sha256': self.required_source_root_sha256,
            'board_family': self.board_family,
            'physical_board': self.physical_board,
            'layout_identity': self.layout_identity,
            'layout_sha256': self.layout_sha256,
            'package_sha256': self.package_sha256,
            'package_size_bytes': self.package_size_bytes,
        }


class FirmwareReleases:
    """Immutable catalog; observed firmware remains an eligibility precondition."""

    def __init__(self, releases: tuple[FirmwareRelease, ...] = ()) -> None:
        if len(releases) > MAX_RELEASES or any(
                not isinstance(release, FirmwareRelease) for release in releases):
            raise ValueError('invalid firmware release count')
        identities = [(row.device_id, row.manifest_sha256) for row in releases]
        targets = [(row.device_id, row.target_version) for row in releases]
        if len(set(identities)) != len(identities) or len(set(targets)) != len(targets):
            raise ValueError('duplicate firmware release')
        by_device: dict[str, tuple[FirmwareRelease, ...]] = {}
        for device_id in sorted({row.device_id for row in releases}):
            rows = tuple(sorted(
                (row for row in releases if row.device_id == device_id),
                key=lambda row: (_generation(row.target_version), row.target_version,
                                 row.manifest_sha256),
            ))
            by_device[device_id] = rows
        self._by_device = MappingProxyType(by_device)

    @property
    def device_ids(self) -> frozenset[str]:
        return frozenset(self._by_device)

    @property
    def releases(self) -> tuple[FirmwareRelease, ...]:
        """All immutable rows, for startup-only package binding."""
        return tuple(row for rows in self._by_device.values() for row in rows)

    def compatible(self, device_id: str, observed_version: str | None,
                   observed_root_sha256: str | None) \
            -> tuple[FirmwareRelease, ...]:
        if not isinstance(device_id, str) or not _DEVICE_ID.fullmatch(device_id):
            raise ValueError('invalid firmware release device')
        if observed_version is None or observed_root_sha256 is None:
            return ()
        try:
            _generation(observed_version)
        except (TypeError, ValueError):
            return ()
        if (not isinstance(observed_root_sha256, str)
                or not _DIGEST.fullmatch(observed_root_sha256)):
            return ()
        return tuple(
            row for row in self._by_device.get(device_id, ())
            if row.required_source_version == observed_version
            and row.required_source_root_sha256 == observed_root_sha256
        )


def load_firmware_releases(path: Path) -> FirmwareReleases:
    value = read_operator_registry(path)
    if (not isinstance(value, dict) or set(value) != {'format', 'releases'}
            or value['format'] != REGISTRY_FORMAT
            or not isinstance(value['releases'], list)
            or not 1 <= len(value['releases']) <= MAX_RELEASES):
        raise ValueError('invalid firmware release registry')
    releases: list[FirmwareRelease] = []
    for entry in value['releases']:
        if not isinstance(entry, dict) or set(entry) != _FIELDS:
            raise ValueError('invalid firmware release fields')
        releases.append(FirmwareRelease(**entry))
    return FirmwareReleases(tuple(releases))
