#!/usr/bin/env python3
"""Shared host-side primitives for the BK7258 tooling.

Small, dependency-free helpers used by the framework and its sub-modules:
strict JSON loading, canonical serialization, identifiers, digests, Kconfig
symbols and repository-relative path checks.  Keeping them here prevents
feature verifiers and metadata validators from importing the whole framework
module for a handful of functions.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any

RETIRED_REPOSITORY_ROOTS = ("board/bk7258_t5ai",)
ID_RE = re.compile(r"^[a-z][a-z0-9_-]*$")
# Kconfig symbol names are identifiers, not necessarily all-uppercase.  The
# NuttX tree contains valid mixed-case symbols such as
# CONFIG_LIBC_IPv4_ADDRCONV and CONFIG_FASTDDS_NoExample.
SYMBOL_RE = re.compile(r"^CONFIG_[A-Za-z0-9_]+$")
HASH_RE = re.compile(r"^[0-9a-f]{64}$")


class FrameworkError(ValueError):
    """Any malformed, ambiguous, or unsupported framework input."""


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _unique_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise FrameworkError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique_pairs)
    except FrameworkError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FrameworkError(f"cannot load JSON: {path}") from error
    if not isinstance(value, dict):
        raise FrameworkError(f"JSON root is not an object: {path}")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(value))


def obj(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise FrameworkError(f"{field} must be an object")
    return value


def array(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        raise FrameworkError(f"{field} must be an array")
    return value


def exact(value: dict[str, Any], keys: set[str], field: str) -> None:
    if set(value) != keys:
        raise FrameworkError(f"{field} keys are not exact")


def identifier(value: Any, field: str) -> str:
    if not isinstance(value, str) or not ID_RE.fullmatch(value):
        raise FrameworkError(f"unsafe identifier in {field}")
    return value


def identifiers(value: Any, field: str) -> list[str]:
    values = array(value, field)
    result = [identifier(item, f"{field}[]") for item in values]
    if len(result) != len(set(result)):
        raise FrameworkError(f"duplicate identifier in {field}")
    return result


def relative_path(value: Any, field: str) -> str:
    if (not isinstance(value, str) or not value or "\\" in value or
            "\x00" in value or any(char.isspace() for char in value) or
            value.startswith("/")):
        raise FrameworkError(f"unsafe repository path in {field}")
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise FrameworkError(f"unsafe repository path in {field}")
    if any(value == root or value.startswith(root + "/")
           for root in RETIRED_REPOSITORY_ROOTS):
        raise FrameworkError(f"retired repository path in {field}")
    return value


def digest(value: Any, field: str) -> str:
    if not isinstance(value, str) or not HASH_RE.fullmatch(value):
        raise FrameworkError(f"invalid SHA-256 in {field}")
    return value


def symbols(value: Any, field: str = "symbols") -> dict[str, str | None]:
    values = obj(value, field)
    result: dict[str, str | None] = {}
    for key, item in values.items():
        if not isinstance(key, str) or not SYMBOL_RE.fullmatch(key):
            raise FrameworkError(f"invalid Kconfig symbol in {field}")
        if item is not None and (not isinstance(item, str) or
                                 not (re.fullmatch(r"[A-Za-z0-9_./:+,-]+", item)
                                      or re.fullmatch(r'"[^"\n]*"', item))):
            raise FrameworkError(f"invalid Kconfig value for {key}")
        result[key] = item
    return result
