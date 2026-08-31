#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Static ownership gate for the maintained BK7258 board/chip/app layers."""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


SDK_INCLUDE_ROOTS = {
    "common",
    "components",
    "driver",
    "modules",
    "os",
    "posix",
    "soc",
}
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp"}
EXCEPTIONS = Path("tools/bk7258/layer_exceptions.json")

_INCLUDE = re.compile(
    r"^\s*#\s*include\s*(?P<open>[<\"])(?P<name>[^>\"]+)[>\"]",
    re.MULTILINE,
)
_RAW_SDK_SYMBOL = re.compile(
    r"\b(?:bk_(?!7258)|gpio_|rtos_)[A-Za-z0-9_]+\b"
)
_RAW_SDK_TYPE = re.compile(
    r"\b(?:bk_err_t|gpio_id_t|gpio_dev_t|gpio_output_state_e|"
    r"bk_dvp_config_t)\b|\bBK_(?:OK|FAIL)\b"
)
_RAW_REGISTER = re.compile(
    r"\b(?:getreg|putreg)(?:8|16|32|64)?\s*\(|\bREG32\b|"
    r"\b0x(?:44|48)[0-9a-fA-F]{6}\b"
)
_CHIP_BOARD_TOKEN = re.compile(
    r"\bBK7258_BOARD_[A-Z0-9_]+\b|\bbk7258_board_[a-z0-9_]+\s*\("
)
_APP_PHYSICAL_TOKEN = re.compile(
    r"\bBK7258_BOARD_[A-Z0-9_]*(?:GPIO|PIN|BUS|CHANNEL|ADDRESS)"
    r"[A-Z0-9_]*\b"
)
_PRODUCT_PROTOCOL = re.compile(
    r"\bBT_GATT_(?:PRIMARY_SERVICE|CHARACTERISTIC|DESCRIPTOR|CCC)\s*\("
    r"|\b[A-Z0-9_]*(?:SERVICE|CONTROL|STATUS)_UUID\b"
)
_HEX_SHA256 = re.compile(r"[0-9a-f]{64}")


@dataclass(frozen=True, order=True)
class Issue:
    path: str
    line: int
    code: str
    message: str


@dataclass(frozen=True)
class Report:
    source_files: int
    kconfig_symbols: int
    legacy_exceptions: int


class LayerError(ValueError):
    """Raised when a source ownership rule is violated."""


def _line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def _without_c_literals(text: str) -> str:
    """Blank comments and literals while preserving offsets and newlines."""

    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|"
        r"'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )

    def blank(match: re.Match[str]) -> str:
        return "".join("\n" if char == "\n" else " " for char in match.group())

    return pattern.sub(blank, text)


def _sources(root: Path) -> list[Path]:
    if not root.is_dir():
        return []
    return sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
        and "bk_idk" not in path.relative_to(root).parts
    )


def _relative(repository: Path, path: Path) -> str:
    return path.relative_to(repository).as_posix()


def _source_issues(repository: Path) -> tuple[list[Issue], int, set[str]]:
    issues: list[Issue] = []
    product_files: set[str] = set()
    count = 0

    for layer in ("boards/bk7258", "app/bk7258", "chips/bk7258"):
        for path in _sources(repository / layer):
            count += 1
            relative = _relative(repository, path)
            text = path.read_text(encoding="utf-8", errors="surrogateescape")
            code = _without_c_literals(text)

            if layer != "chips/bk7258":
                for match in _INCLUDE.finditer(text):
                    if code[match.start()].isspace():
                        continue
                    include = match.group("name")
                    if include == "sdkconfig.h" or \
                            include.split("/", 1)[0] in SDK_INCLUDE_ROOTS:
                        issues.append(Issue(
                            relative,
                            _line_number(text, match.start()),
                            "SDK_INCLUDE",
                            f"{layer.split('/', 1)[0]} must use a chip/NuttX "
                            f"contract instead of SDK header <{include}>",
                        ))

                for pattern, name in (
                    (_RAW_SDK_SYMBOL, "SDK_SYMBOL"),
                    (_RAW_SDK_TYPE, "SDK_TYPE"),
                    (_RAW_REGISTER, "RAW_REGISTER"),
                ):
                    for match in pattern.finditer(code):
                        issues.append(Issue(
                            relative,
                            _line_number(code, match.start()),
                            name,
                            "board/app code must not depend on the raw Beken SDK ABI",
                        ))

            if layer == "chips/bk7258":
                for match in _INCLUDE.finditer(text):
                    if code[match.start()].isspace():
                        continue
                    include = match.group("name")
                    if include.startswith("arch/board/"):
                        issues.append(Issue(
                            relative,
                            _line_number(text, match.start()),
                            "CHIP_TO_BOARD",
                            "chip code must not include a physical-board header",
                        ))
                    if path.parent.name == "include" and \
                            path.name.startswith("bk7258_") and \
                            (include == "sdkconfig.h" or
                             include.split("/", 1)[0] in SDK_INCLUDE_ROOTS):
                        issues.append(Issue(
                            relative,
                            _line_number(text, match.start()),
                            "PUBLIC_SDK_ABI",
                            "public chip contracts must not expose an SDK header",
                        ))
                if path.parent.name == "include" and \
                        path.name.startswith("bk7258_"):
                    for match in _RAW_SDK_TYPE.finditer(code):
                        issues.append(Issue(
                            relative,
                            _line_number(code, match.start()),
                            "PUBLIC_SDK_ABI",
                            "public chip contracts must use NuttX or typed adapter values",
                        ))
                for match in _CHIP_BOARD_TOKEN.finditer(code):
                    issues.append(Issue(
                        relative,
                        _line_number(code, match.start()),
                        "CHIP_TO_BOARD",
                        "chip code must receive physical facts through a typed contract",
                    ))
                if _PRODUCT_PROTOCOL.search(code):
                    product_files.add(relative)

            if layer == "app/bk7258":
                for match in _APP_PHYSICAL_TOKEN.finditer(code):
                    issues.append(Issue(
                        relative,
                        _line_number(code, match.start()),
                        "APP_PHYSICAL_RESOURCE",
                        "app code must request a service, not own board pins/buses",
                    ))

    return issues, count, product_files


def _menu_role(lines: list[str], start: int) -> str | None:
    for line in lines[start + 1:]:
        stripped = line.strip()
        if stripped.startswith(("config ", "menu ", "endmenu")):
            break
        if stripped.startswith("depends on") and "BK7258_AP_CORE" in stripped:
            return "cp" if "!BK7258_AP_CORE" in stripped else "ap"
    return None


def _kconfig_issues(repository: Path) -> tuple[list[Issue], int]:
    path = repository / "chips/bk7258/Kconfig"
    lines = path.read_text(encoding="utf-8").splitlines()
    relative = _relative(repository, path)
    menus: list[str | None] = []
    issues: list[Issue] = []
    symbols = 0
    index = 0

    while index < len(lines):
        stripped = lines[index].strip()
        if stripped.startswith("menu "):
            menus.append(_menu_role(lines, index))
            index += 1
            continue
        if stripped == "endmenu":
            if menus:
                menus.pop()
            index += 1
            continue
        if not stripped.startswith("config "):
            index += 1
            continue

        symbols += 1
        symbol = stripped.split(None, 1)[1]
        end = index + 1
        while end < len(lines) and not lines[end].strip().startswith(
            ("config ", "menu ", "endmenu")
        ):
            end += 1
        block = "\n".join(lines[index + 1:end])
        cp_only = re.search(r"depends on[^\n]*!BK7258_AP_CORE", block) is not None
        ap_only = re.search(
            r"depends on[^\n]*(?<!!)\bBK7258_AP_CORE\b", block
        ) is not None
        if cp_only and "ap" in menus:
            issues.append(Issue(
                relative,
                index + 1,
                "KCONFIG_ROLE_MENU",
                f"CP-only {symbol} is nested in an AP-only menu",
            ))
        if ap_only and "cp" in menus:
            issues.append(Issue(
                relative,
                index + 1,
                "KCONFIG_ROLE_MENU",
                f"AP-only {symbol} is nested in a CP-only menu",
            ))
        index = end

    return issues, symbols


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _exception_issues(
    repository: Path, product_files: set[str]
) -> tuple[list[Issue], int]:
    path = repository / EXCEPTIONS
    relative = EXCEPTIONS.as_posix()
    issues: list[Issue] = []
    if not path.is_file() or path.is_symlink():
        return [Issue(relative, 1, "EXCEPTION_FILE", "missing regular exception file")], 0

    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        return [Issue(relative, 1, "EXCEPTION_FILE", f"invalid JSON: {error}")], 0

    rows = document.get("product_protocol_files") if isinstance(document, dict) else None
    if not isinstance(document, dict) or document.get("version") != 1 \
            or not isinstance(rows, list):
        return [Issue(relative, 1, "EXCEPTION_FILE", "unsupported exception schema")], 0

    allowed: dict[str, str] = {}
    for row in rows:
        if not isinstance(row, dict):
            issues.append(Issue(relative, 1, "EXCEPTION_FILE", "exception row is not an object"))
            continue
        name = row.get("path")
        expected = row.get("sha256")
        reason = row.get("reason")
        if not isinstance(name, str) or not isinstance(expected, str) \
                or _HEX_SHA256.fullmatch(expected) is None \
                or not isinstance(reason, str) or not reason.strip():
            issues.append(Issue(relative, 1, "EXCEPTION_FILE", "malformed exception row"))
            continue
        pure = PurePosixPath(name)
        if pure.is_absolute() or ".." in pure.parts \
                or not name.startswith("chips/bk7258/") or name in allowed:
            issues.append(Issue(relative, 1, "EXCEPTION_FILE", f"unsafe or duplicate path: {name}"))
            continue
        allowed[name] = expected

    for name in sorted(product_files - allowed.keys()):
        issues.append(Issue(
            name, 1, "PRODUCT_PROTOCOL_IN_CHIP",
            "product GATT/UUID policy belongs in app; a legacy exception must be hash-bound",
        ))
    for name, expected in sorted(allowed.items()):
        selected = repository.joinpath(*PurePosixPath(name).parts)
        if name not in product_files:
            issues.append(Issue(relative, 1, "EXCEPTION_STALE", f"no protocol marker remains in {name}"))
        elif not selected.is_file() or selected.is_symlink():
            issues.append(Issue(relative, 1, "EXCEPTION_PATH", f"not a regular file: {name}"))
        elif _sha256(selected) != expected:
            issues.append(Issue(
                name, 1, "EXCEPTION_HASH",
                "legacy product-protocol file changed; re-review its layer before updating the hash",
            ))

    return issues, len(allowed)


def audit(repository: Path) -> tuple[list[Issue], Report]:
    repository = repository.resolve(strict=True)
    source_issues, source_files, product_files = _source_issues(repository)
    kconfig_issues, symbols = _kconfig_issues(repository)
    exception_issues, exceptions = _exception_issues(repository, product_files)
    issues = sorted(source_issues + kconfig_issues + exception_issues)
    return issues, Report(source_files, symbols, exceptions)


def verify(repository: Path) -> Report:
    issues, report = audit(repository)
    if issues:
        details = "\n".join(
            f"  {row.path}:{row.line}: [{row.code}] {row.message}"
            for row in issues
        )
        raise LayerError(f"BK7258 layer verification failed:\n{details}")
    return report
