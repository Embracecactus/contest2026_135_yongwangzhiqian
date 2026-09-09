# SPDX-License-Identifier: Apache-2.0
"""Operator-provisioned device identities, keyed by verified TLS certificates."""

from __future__ import annotations

import json
import os
import re
import stat
from pathlib import Path
from types import MappingProxyType
from collections.abc import Mapping

_DEVICE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}", re.ASCII)
_SHA256 = re.compile(r"[0-9a-f]{64}", re.ASCII)


class DeviceBindings:
    """Immutable per-device leaf-certificate pins; CA validation is also required."""

    def __init__(self, certificates: Mapping[str, str]) -> None:
        if not 1 <= len(certificates) <= 256:
            raise ValueError("invalid device binding count")
        pins = dict(certificates)
        if any(not isinstance(pin, str) or not _SHA256.fullmatch(pin)
               or not isinstance(device, str) or not _DEVICE_ID.fullmatch(device)
               for pin, device in pins.items()):
            raise ValueError("invalid device binding")
        if len(set(pins.values())) != len(pins):
            raise ValueError("duplicate device binding")
        self._certificates = MappingProxyType(pins)

    def device_for_certificate(self, sha256: str) -> str | None:
        return self._certificates.get(sha256)

    def contains_device(self, device_id: str) -> bool:
        return device_id in self._certificates.values()


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate device binding field")
        result[key] = value
    return result


def read_operator_registry(path: Path) -> object:
    """Read a bounded, operator-owned, non-writable-by-others JSON registry."""
    fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as source:
        info = os.fstat(source.fileno())
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid()
                or info.st_mode & 0o022 or not 0 < info.st_size <= 65536):
            raise ValueError("invalid device binding file")
        raw = source.read(65537)
    if len(raw) > 65536:
        raise ValueError("device binding file too large")
    try:
        value = json.loads(raw, object_pairs_hook=_unique_object)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ValueError("invalid device binding JSON") from error
    return value


def load_device_bindings(path: Path) -> DeviceBindings:
    value = read_operator_registry(path)
    if (not isinstance(value, dict) or set(value) != {'format', 'devices'}
            or value['format'] != 'shaniu.device-bindings/1'
            or not isinstance(value['devices'], list)):
        raise ValueError("invalid device binding schema")
    pins: dict[str, str] = {}
    for entry in value['devices']:
        if (not isinstance(entry, dict)
                or set(entry) != {'device_id', 'client_certificate_sha256'}
                or not isinstance(entry['client_certificate_sha256'], str)):
            raise ValueError("invalid device binding entry")
        pin = entry['client_certificate_sha256']
        if pin in pins:
            raise ValueError("duplicate certificate binding")
        pins[pin] = entry['device_id']
    return DeviceBindings(pins)
