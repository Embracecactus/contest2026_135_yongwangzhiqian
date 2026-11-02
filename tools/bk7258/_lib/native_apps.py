# SPDX-License-Identifier: Apache-2.0
"""Build and sign the deliberately small BK7258 native-app ABI package."""

from __future__ import annotations

import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import urlparse

from . import toolchain
from . import trust


PACKAGE_FORMAT = "shaniu.native/1"
CATALOG_FORMAT = "shaniu.store/1"
TARGET = "bk7258-ap-armv8m"
ABI = 1
ABI_V2 = 2
# Device grant names in their canonical manifest order; "display" is always
# first and ABI 1 may grant nothing else.
PERMISSIONS = ("display", "motion", "haptic", "sound")
PACKAGE_MAX = 24576
IMAGE_MAX = 16384
MANIFEST_MAX = 1024
EXPORT = "shaniu_native_entry"
_P256_SPKI_PREFIX = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d03010703420004"
)
_ID = re.compile(r"[a-z0-9][a-z0-9-]{0,30}")


def add_arguments(parser):
    commands = parser.add_subparsers(dest="native_app_command", required=True)
    build = commands.add_parser("build", help="compile one unsigned native app ELF")
    build.add_argument(
        "--output", type=Path, required=True, help="new unsigned ELF path"
    )
    build.add_argument(
        "--source", type=Path, help="one independent C source; defaults to breathing.c"
    )
    pack = commands.add_parser("pack", help="build and sign one native app")
    pack.add_argument("--id", required=True)
    pack.add_argument("--name", required=True)
    pack.add_argument("--description", required=True)
    pack.add_argument("--version", type=int, required=True)
    pack.add_argument(
        "--abi", type=int, choices=(ABI, ABI_V2), default=ABI,
        help="1: display frames only; 2: declared input and feedback",
    )
    pack.add_argument(
        "--permissions", default="display",
        help="comma-separated grants: display[,motion][,haptic][,sound]",
    )
    pack.add_argument("--signing-key", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True, help="new output directory")
    pack.add_argument("--openssl", type=Path)
    pack.add_argument(
        "--source", type=Path, help="one independent C source; defaults to breathing.c"
    )

    catalog = commands.add_parser(
        "catalog-entry", help="write one HTTPS store catalog entry for a native package"
    )
    catalog.add_argument("--package", type=Path, required=True)
    catalog.add_argument("--package-url", required=True)
    catalog.add_argument("--output", type=Path, required=True)


def _compact(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def _regular(path: Path, label: str) -> Path:
    return trust._regular(path, label)


def _openssl(path: Path | None) -> Path:
    candidate = path or Path(shutil.which("openssl") or "/usr/bin/openssl")
    return _regular(candidate, "OpenSSL executable")


def _p256_private(key: Path, openssl: Path) -> bytes:
    key = _regular(key, "native app signing key")
    public = trust._public_der(key, openssl, private=True)
    if not public.startswith(_P256_SPKI_PREFIX):
        raise ValueError("native app signer must use P-256")
    return public


def _text(value: str, label: str, limit: int) -> str:
    if (
        not value
        or len(value.encode("utf-8")) > limit
        or any(ord(c) < 32 for c in value)
    ):
        raise ValueError(f"invalid native app {label}")
    return value


def _permissions(abi: int, value: str) -> list[str]:
    names = [part.strip() for part in value.split(",") if part.strip()]
    if abi not in (ABI, ABI_V2) or not names or names[0] != "display":
        raise ValueError("native app permissions must start with display")
    if abi == ABI and names != ["display"]:
        raise ValueError("ABI 1 native apps may only request display")
    order = [PERMISSIONS.index(name) if name in PERMISSIONS else -1 for name in names]
    if -1 in order or order != sorted(set(order)):
        raise ValueError(
            "native app permissions must be unique and ordered as "
            + ",".join(PERMISSIONS)
        )
    return names


def _manifest(
    app_id: str,
    name: str,
    description: str,
    version: int,
    image: bytes,
    abi: int = ABI,
    permissions: list[str] | None = None,
) -> bytes:
    permissions = _permissions(abi, ",".join(permissions or ["display"]))
    if not _ID.fullmatch(app_id):
        raise ValueError("invalid native app ID")
    if not isinstance(version, int) or not 1 <= version <= 0xFFFFFFFF:
        raise ValueError("native app version must be 1..4294967295")
    document = {
        "id": app_id,
        "name": _text(name, "name", 64),
        "description": _text(description, "description", 160),
        "version": version,
        "abi": abi,
        "target": TARGET,
        "sha256": hashlib.sha256(image).hexdigest(),
        "image_size": len(image),
        "permissions": permissions,
    }
    encoded = _compact(document)
    if len(encoded) > MANIFEST_MAX:
        raise ValueError("native app manifest exceeds 1024 bytes")
    return encoded


def app_hash(app_id: str) -> int:
    """FNV-1a, matching bknative_app_hash(): the NFC action 7 app reference."""
    value = 2166136261
    for byte in app_id.encode("utf-8"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def _run(command: list[str], label: str, *, cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={"PATH": os.environ.get("PATH", ""), "LC_ALL": "C"},
        )
    except (OSError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", "")
        raise ValueError(f"{label} failed: {str(detail).strip()}") from error
    return result.stdout


def _relocation_support(repository: Path) -> set[str]:
    source = _regular(
        repository.parent / "nuttx/libs/libc/machine/arm/armv8-m/arch_elf.c",
        "pinned ARMv8-M ELF relocator",
    )
    supported = set(re.findall(r"case\s+(R_ARM_[A-Z0-9_]+)\s*:", source.read_text()))
    if not supported:
        raise ValueError("pinned ARMv8-M ELF relocator has no relocation cases")
    return supported


def _relocation_types(readelf: str, supported: set[str]) -> list[str]:
    relocations = sorted(set(re.findall(r"\b(R_ARM_[A-Z0-9_]+)\b", readelf)))
    unsupported = sorted(set(relocations) - supported)
    if unsupported:
        raise ValueError(
            "native app uses unsupported ARM relocations: " + ", ".join(unsupported)
        )
    return relocations


def _build_image(
    repository: Path, source: Path | None = None
) -> tuple[bytes, dict[str, object]]:
    report = toolchain.verify(repository)
    tools = report.binary_dir
    source = _regular(
        source or repository / "app/bk7258/native-apps/breathing.c",
        "native app source",
    )
    if source.suffix != ".c":
        raise ValueError("native app source must be one C file")
    include = _regular(
        repository / "app/bk7258/bk7258_native_abi.h", "native ABI header"
    )
    del include  # The compiler must receive its directory, not an SDK copy.
    with tempfile.TemporaryDirectory(prefix="bk7258-native-app-") as temporary:
        root = Path(temporary)
        obj = root / "breathing.o"
        elf = root / "breathing.elf"
        common = [
            "-march=armv8-m.main+dsp",
            "-mthumb",
            "-mfpu=fpv5-sp-d16",
            "-mfloat-abi=hard",
            "-Os",
            "-std=gnu11",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-common",
            "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables",
            "-ffunction-sections",
            "-fdata-sections",
            "-I",
            str(repository / "app/bk7258"),
        ]
        _run(
            [
                str(tools / "arm-none-eabi-gcc"),
                *common,
                "-c",
                str(source),
                "-o",
                str(obj),
            ],
            "native app compilation",
        )
        _run(
            [
                str(tools / "arm-none-eabi-ld"),
                "-r",
                "-e",
                EXPORT,
                "-o",
                str(elf),
                str(obj),
            ],
            "native app link",
        )
        header = _run(
            [str(tools / "arm-none-eabi-readelf"), "-h", str(elf)],
            "native app ELF inspection",
        )
        if (
            "Type:                              REL (Relocatable file)" not in header
            or "Machine:                           ARM" not in header
        ):
            raise ValueError("native app is not an ARM ET_REL ELF")
        undefined = _run(
            [str(tools / "arm-none-eabi-nm"), "-u", str(elf)],
            "native app undefined-symbol check",
        )
        if undefined.strip():
            raise ValueError("native app has undefined symbols")
        symbols = _run(
            [str(tools / "arm-none-eabi-nm"), "-g", "--defined-only", str(elf)],
            "native app export check",
        )
        if not re.search(rf"\b{re.escape(EXPORT)}$", symbols, re.MULTILINE):
            raise ValueError(f"native app does not export {EXPORT}")
        sections = _run(
            [str(tools / "arm-none-eabi-readelf"), "-SW", str(elf)],
            "native app section check",
        )
        if re.search(r"\.(?:preinit_array|init_array|fini_array)(?:\.|\s)", sections):
            raise ValueError("native app must not contain init/fini arrays")
        relocations = _relocation_types(
            _run(
                [str(tools / "arm-none-eabi-readelf"), "-rW", str(elf)],
                "native app relocation check",
            ),
            _relocation_support(repository),
        )
        image = elf.read_bytes()
    if not image or len(image) > IMAGE_MAX:
        raise ValueError("native app ELF exceeds 16384 bytes")
    return image, {
        "toolchain": report.gcc_version,
        "undefined_symbols": "0",
        "relocations": relocations,
    }


def _sign(manifest: bytes, key: Path, openssl: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="bk7258-native-sign-") as temporary:
        root = Path(temporary)
        source = root / "manifest.json"
        signature = root / "signature.der"
        source.write_bytes(manifest)
        trust._run(
            [
                str(openssl),
                "dgst",
                "-sha256",
                "-sign",
                str(key),
                "-out",
                str(signature),
                str(source),
            ],
            "native app signing",
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        return signature.read_bytes()


def _parse_package(path: Path) -> tuple[dict[str, object], bytes, bytes]:
    raw = _regular(path, "native package").read_bytes()
    try:
        package = json.loads(raw.decode("utf-8"))
        if not isinstance(package, dict) or set(package) != {
            "format",
            "manifest",
            "signature",
            "image",
        }:
            raise ValueError
        manifest = base64.b64decode(package["manifest"], validate=True)
        image = base64.b64decode(package["image"], validate=True)
        signature = base64.b64decode(package["signature"], validate=True)
        document = json.loads(manifest.decode("utf-8"))
    except (UnicodeError, ValueError, TypeError, json.JSONDecodeError) as error:
        raise ValueError("native package is malformed") from error
    if package["format"] != PACKAGE_FORMAT or _compact(document) != manifest:
        raise ValueError("native package does not use a compact native manifest")
    expected = {
        "id",
        "name",
        "description",
        "version",
        "abi",
        "target",
        "sha256",
        "image_size",
        "permissions",
    }
    if not isinstance(document, dict) or set(document) != expected:
        raise ValueError("native package manifest fields are invalid")
    if document["sha256"] != hashlib.sha256(image).hexdigest() or document[
        "image_size"
    ] != len(image):
        raise ValueError("native package image digest is invalid")
    if len(manifest) > MANIFEST_MAX or len(image) > IMAGE_MAX or len(raw) > PACKAGE_MAX:
        raise ValueError("native package exceeds device limits")
    return document, manifest, signature


def pack(repository: Path, args) -> dict[str, object]:
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("native app output directory already exists")
    openssl = _openssl(args.openssl)
    key = _regular(args.signing_key, "native app signing key")
    public = _p256_private(key, openssl)
    image, checks = _build_image(repository, getattr(args, "source", None))
    abi = getattr(args, "abi", ABI)
    manifest = _manifest(
        args.id,
        args.name,
        args.description,
        args.version,
        image,
        abi,
        _permissions(abi, getattr(args, "permissions", "display")),
    )
    signature = _sign(manifest, key, openssl)
    if not signature or len(signature) > 80:
        raise ValueError("native app signature exceeds device limit")
    package = _compact(
        {
            "format": PACKAGE_FORMAT,
            "manifest": base64.b64encode(manifest).decode("ascii"),
            "signature": base64.b64encode(signature).decode("ascii"),
            "image": base64.b64encode(image).decode("ascii"),
        }
    )
    if len(package) > PACKAGE_MAX:
        raise ValueError("native package exceeds 24576 bytes")
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "image.elf").write_bytes(image)
    (args.output / "manifest.json").write_bytes(manifest)
    (args.output / "publisher.der").write_bytes(public)
    (args.output / "package.json").write_bytes(package + b"\n")
    return {
        "format": PACKAGE_FORMAT,
        "id": args.id,
        "abi": abi,
        "app_hash": f"{app_hash(args.id):08x}",
        "output": str(args.output),
        "image_size": len(image),
        "package_size": len(package),
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "public_key_sha256": hashlib.sha256(public).hexdigest(),
        "checks": checks,
        "installed": False,
    }


def build(repository: Path, args) -> dict[str, object]:
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("native app ELF output already exists")
    image, checks = _build_image(repository, getattr(args, "source", None))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    return {
        "format": "shaniu.native-elf/1",
        "output": str(args.output),
        "image_size": len(image),
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "checks": checks,
        "signed": False,
    }


def catalog_entry(args) -> dict[str, object]:
    document, _manifest_bytes, _signature = _parse_package(args.package)
    parsed = urlparse(args.package_url)
    if (
        parsed.scheme != "https"
        or not parsed.netloc
        or parsed.username
        or parsed.password
    ):
        raise ValueError(
            "native app package URL must be an HTTPS URL without credentials"
        )
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("native catalog output already exists")
    entry = {
        key: document[key]
        for key in (
            "id",
            "name",
            "description",
            "version",
            "abi",
            "target",
            "sha256",
            "image_size",
            "permissions",
        )
    }
    entry["package_url"] = args.package_url
    catalog = _compact({"format": CATALOG_FORMAT, "apps": [entry]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(catalog + b"\n")
    return {
        "format": CATALOG_FORMAT,
        "id": document["id"],
        "output": str(args.output),
        "package_url": args.package_url,
    }


def run(repository: Path, args) -> dict[str, object]:
    if args.native_app_command == "build":
        return build(repository, args)
    if args.native_app_command == "pack":
        return pack(repository, args)
    return catalog_entry(args)
