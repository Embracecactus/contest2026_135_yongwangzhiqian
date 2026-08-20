"""Explicit BK7258 key handling, signing, and public trust evidence."""

from __future__ import annotations

import hashlib
import base64
import os
import stat
import struct
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

from _lib import image as image_domain
from _lib import layout as layout_domain


class TrustError(RuntimeError):
    """Signing material or a public trust binding is invalid."""


@dataclass(frozen=True)
class TrustEvidence:
    mode: str
    algorithm: str | None = None
    bl1_public_fingerprint: str | None = None
    mcuboot_public_fingerprint: str | None = None
    bl1_public_der: str | None = None
    mcuboot_public_der: str | None = None
    bl2_load_address: int | None = None
    bl1_security_counter: int | None = None
    rollback: str | None = None
    images: tuple[dict[str, object], ...] = ()

    def manifest(self) -> dict[str, object]:
        return {
            key: value for key, value in asdict(self).items()
            if value is not None and value != ()
        }


@dataclass(frozen=True)
class PublicSources:
    bl1_source: Path
    mcuboot_source: Path
    bl1_fingerprint: str
    mcuboot_fingerprint: str


@dataclass(frozen=True)
class SignedImage:
    path: Path
    public_fingerprint: str
    sha256: str
    version: str
    security_counter: int


@dataclass(frozen=True)
class SignedRelease:
    image_set: image_domain.ImageSet
    evidence: TrustEvidence


def unsigned() -> TrustEvidence:
    return TrustEvidence(mode="unsigned")


def _regular(path: Path, label: str) -> Path:
    path = path.absolute()
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise TrustError(f"missing {label}: {path}") from error
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise TrustError(f"{label} must be a regular non-symlink file: {path}")
    return path.resolve(strict=True)


def _run(command: list[str], label: str, **kwargs: object) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(command, check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        raise TrustError(f"{label} failed with exit status {error.returncode}") from error
    except OSError as error:
        raise TrustError(f"cannot run {label}: {command[0]}") from error


def _atomic_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise TrustError(f"generated public source target is invalid: {path}")
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _atomic_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise TrustError(f"generated binary target is invalid: {path}")
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _public_der(key: Path, openssl: Path, *, private: bool) -> bytes:
    key = _regular(key, "private key" if private else "public key")
    openssl = _regular(openssl, "OpenSSL executable")
    if not os.access(openssl, os.X_OK):
        raise TrustError(f"OpenSSL is not executable: {openssl}")
    command = [str(openssl), "pkey"]
    if not private:
        command.append("-pubin")
    command.extend(["-in", str(key), "-pubout", "-outform", "DER"])
    result = _run(
        command,
        "public-key derivation",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    public = result.stdout
    if not isinstance(public, bytes) or len(public) != 91 \
            or public[-65] != 0x04:
        raise TrustError("key must be a canonical P-256 SubjectPublicKeyInfo")
    return public


def file_sha256(path: Path) -> str:
    path = _regular(path, "artifact")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def elf_symbol(elf: Path, nm: Path, name: str) -> int:
    elf = _regular(elf, "ELF")
    nm = _regular(nm, "nm executable")
    result = _run(
        [str(nm), "-P", str(elf)],
        "ELF symbol inspection",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    matches = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] == name:
            try:
                matches.append(int(fields[2], 16))
            except ValueError as error:
                raise TrustError(f"ELF symbol value is malformed: {name}") from error
    if len(matches) != 1:
        raise TrustError(f"ELF must expose one {name} symbol")
    return matches[0]


def public_fingerprint(private_key: Path, openssl: Path) -> tuple[str, bytes]:
    """Return SHA-256 of canonical DER SubjectPublicKeyInfo."""

    public = _public_der(private_key, openssl, private=True)
    return hashlib.sha256(public).hexdigest(), public


def _bytes_initializer(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 8):
        rows.append("  " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 8]))
    return ",\n".join(rows)


def write_public_sources(*, bl1_public_key: Path, mcuboot_public_key: Path,
                         openssl: Path, output: Path) -> PublicSources:
    """Materialize public-only BL1 and MCUboot C inputs in the build tree."""

    output = output.absolute()
    if output.is_symlink():
        raise TrustError(f"public source output must not be a symlink: {output}")
    output.mkdir(parents=True, exist_ok=True)
    bl1_der = _public_der(bl1_public_key, openssl, private=False)
    mcuboot_der = _public_der(mcuboot_public_key, openssl, private=False)
    bl1_xy = bl1_der[-64:]
    bl1_uncompressed_hash = hashlib.sha256(b"\x04" + bl1_xy).digest()
    bl1_source = output / "bk7258_bl1_public_key.c"
    mcuboot_source = output / "bk7258_mcuboot_public_key.c"
    _atomic_text(
        bl1_source,
        "/* Generated public-only P-256 BL1 root. */\n"
        "#include <stdint.h>\n\n"
        "__attribute__((used, section(\".rodata.bk7258_bl1_manifest_root_public_key\")))\n"
        "const uint8_t bk7258_bl1_manifest_root_public_key[64] =\n{\n"
        f"{_bytes_initializer(bl1_xy)}\n}};\n\n"
        "__attribute__((used, section(\".rodata.bk7258_beken_manifest_root_public_key_hash\")))\n"
        "const uint8_t bk7258_beken_manifest_root_public_key_hash[32] =\n{\n"
        f"{_bytes_initializer(bl1_uncompressed_hash)}\n}};\n",
    )
    _atomic_text(
        mcuboot_source,
        "/* Generated public-only P-256 MCUboot root. */\n"
        "#include <bootutil/sign_key.h>\n\n"
        "__attribute__((used, section(\".rodata.ecdsa_pub_key\")))\n"
        "const unsigned char ecdsa_pub_key[] =\n{\n"
        f"{_bytes_initializer(mcuboot_der)}\n}};\n\n"
        "__attribute__((used, section(\".rodata.ecdsa_pub_key_len\")))\n"
        "const unsigned int ecdsa_pub_key_len = sizeof(ecdsa_pub_key);\n"
        "const struct bootutil_key bootutil_keys[] =\n"
        "{\n  { .key = ecdsa_pub_key, .len = &ecdsa_pub_key_len },\n};\n"
        "const int bootutil_key_cnt = 1;\n",
    )
    return PublicSources(
        bl1_source,
        mcuboot_source,
        hashlib.sha256(bl1_der).hexdigest(),
        hashlib.sha256(mcuboot_der).hexdigest(),
    )


def _raw_ecdsa_signature(der: bytes) -> bytes:
    if len(der) < 8 or der[0] != 0x30 or der[1] != len(der) - 2:
        raise TrustError("OpenSSL returned a malformed ECDSA signature")
    offset = 2
    values = []
    for _ in range(2):
        if offset + 2 > len(der) or der[offset] != 0x02:
            raise TrustError("OpenSSL ECDSA signature lacks an integer")
        size = der[offset + 1]
        offset += 2
        value = der[offset:offset + size]
        offset += size
        if not value or len(value) > 33 or (len(value) == 33 and value[0] != 0):
            raise TrustError("OpenSSL ECDSA integer is outside P-256")
        value = value.lstrip(b"\0")
        if len(value) > 32:
            raise TrustError("OpenSSL ECDSA integer is outside P-256")
        values.append(value.rjust(32, b"\0"))
    if offset != len(der):
        raise TrustError("OpenSSL ECDSA signature has trailing bytes")
    return b"".join(values)


def _normalize_version(version: str) -> str:
    try:
        core, separator, build_text = version.partition("+")
        major_text, minor_text, revision_text = core.split(".")
        values = (int(major_text), int(minor_text), int(revision_text))
        build = int(build_text) if separator else 0
    except (ValueError, TypeError) as error:
        raise TrustError("MCUboot version must be MAJOR.MINOR.REVISION[+BUILD]") from error
    if not (0 <= values[0] <= 0xff and 0 <= values[1] <= 0xff
            and 0 <= values[2] <= 0xffff and 0 <= build <= 0xffffffff):
        raise TrustError("MCUboot version fields exceed the image header")
    return f"{values[0]}.{values[1]}.{values[2]}+{build}"


def sign_bl1_manifest(*, bl2_image: Path, output: Path, private_key: Path,
                      static_address: int, load_address: int,
                      security_counter: int, openssl: Path) -> Path:
    """Create the previously verified 256-byte Beken-shaped BL1 record."""

    bl2_image = _regular(bl2_image, "BL2 image")
    private_key = _regular(private_key, "BL1 private key")
    openssl = _regular(openssl, "OpenSSL executable")
    output = output.absolute()
    if output.exists() or output.is_symlink():
        raise TrustError(f"BL1 Manifest output already exists: {output}")
    if security_counter <= 0 or not 0 <= static_address <= 0xffffffff \
            or not 0 <= load_address <= 0xffffffff:
        raise TrustError("BL1 Manifest addresses or security counter are invalid")
    image = bl2_image.read_bytes()
    if not image or len(image) > 0xffffffff:
        raise TrustError("BL2 image size is invalid")
    _, public = public_fingerprint(private_key, openssl)
    record = bytearray(b"\xff" * 256)
    fields = (
        0xA1BC2FD8,
        0x00010001,
        security_counter,
        0xD5,
        0x00030619,
        1,
        0,
        0,
        static_address,
        load_address,
        len(image),
        load_address,
    )
    struct.pack_into("<12I", record, 0, *fields)
    record[0x30:0x50] = hashlib.sha256(image).digest()
    struct.pack_into("<I", record, 0x50, 0)
    record[0x54:0x95] = public[-65:]
    with tempfile.TemporaryDirectory(prefix="bk7258-bl1-sign-") as name:
        temporary = Path(name)
        signed_prefix = temporary / "manifest-prefix.bin"
        signature_der = temporary / "manifest-signature.der"
        signed_prefix.write_bytes(record[:0x95])
        _run(
            [
                str(openssl), "dgst", "-sha256", "-sign", str(private_key),
                "-out", str(signature_der), str(signed_prefix),
            ],
            "BL1 Manifest signing",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        record[0x95:0xD5] = _raw_ecdsa_signature(signature_der.read_bytes())
    _atomic_bytes(output, bytes(record))
    return output


def compiled_section(elf: Path, section: str, objcopy: Path) -> bytes:
    """Read one linker-owned public trust section."""

    elf = _regular(elf, "ELF")
    objcopy = _regular(objcopy, "objcopy executable")
    if not section.startswith(".bk7258.trust."):
        raise TrustError(f"invalid trust section: {section}")
    with tempfile.TemporaryDirectory(prefix="bk7258-trust-") as name:
        output = Path(name) / "anchor.bin"
        _run(
            [str(objcopy), f"--dump-section", f"{section}={output}", str(elf)],
            "ELF trust-anchor extraction",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        data = output.read_bytes()
    if not data:
        raise TrustError(f"ELF trust section is empty: {section}")
    return data


def require_key_matches_elf(private_key: Path, elf: Path, section: str, *,
                            openssl: Path, objcopy: Path) -> str:
    fingerprint, public = public_fingerprint(private_key, openssl)
    observed = compiled_section(elf, section, objcopy)
    if section == ".bk7258.trust.bl1":
        xy = public[-64:]
        expected = xy + hashlib.sha256(b"\x04" + xy).digest()
        if observed != expected:
            raise TrustError("private BL1 key does not match the compiled public root")
    elif section == ".bk7258.trust.mcuboot":
        if len(observed) != 96 or observed[:91] != public \
                or observed[91] != 0 or int.from_bytes(observed[92:], "little") != 91:
            raise TrustError("private MCUboot key does not match the compiled public root")
    else:
        raise TrustError(f"unsupported compiled trust section: {section}")
    return fingerprint


def sign_mcuboot(*, input_image: Path, output_image: Path, private_key: Path,
                  bl2_elf: Path, version: str, security_counter: int,
                  slot_size: int, official_imgtool: Path, openssl: Path,
                  objcopy: Path) -> SignedImage:
    """Sign one image using the manifest-pinned official MCUboot imgtool."""

    input_image = _regular(input_image, "unsigned image")
    private_key = _regular(private_key, "MCUboot private key")
    bl2_elf = _regular(bl2_elf, "BL2 ELF")
    official_imgtool = _regular(official_imgtool, "official MCUboot imgtool")
    output_image = output_image.absolute()
    if output_image.exists() or output_image.is_symlink():
        raise TrustError(f"signed output already exists: {output_image}")
    version = _normalize_version(version)
    if security_counter < 0 or slot_size <= 0:
        raise TrustError("security counter and slot size are invalid")

    fingerprint = require_key_matches_elf(
        private_key, bl2_elf, ".bk7258.trust.mcuboot",
        openssl=openssl, objcopy=objcopy,
    )
    output_image.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bk7258-mcuboot-") as name:
        temporary = Path(name)
        signed = temporary / "signed.bin"
        _run(
            [
                sys.executable, str(official_imgtool), "sign",
                "-k", str(private_key),
                "--public-key-format", "full",
                "--max-align", "8", "--align", "1",
                "--version", version,
                "--security-counter", str(security_counter),
                "--pad-header", "--header-size", "0x200",
                "--slot-size", str(slot_size),
                "--boot-record", "SPE", "--endian", "little",
                str(input_image), str(signed),
            ],
            "official MCUboot signing",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        _regular(signed, "signed MCUboot image")
        if signed.stat().st_size > slot_size:
            raise TrustError("signed MCUboot image exceeds its slot")
        _run(
            [
                sys.executable, str(official_imgtool), "verify",
                "-k", str(private_key), str(signed),
            ],
            "official MCUboot signature verification",
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        os.replace(signed, output_image)
    return SignedImage(
        output_image,
        fingerprint,
        file_sha256(output_image),
        version,
        security_counter,
    )


def signed_evidence(*, bl1_private_key: Path, mcuboot_private_key: Path,
                    bl1_elf: Path, bl2_elf: Path,
                    bl1_security_counter: int,
                    bl2_load_address: int,
                    images: dict[str, SignedImage], openssl: Path,
                    objcopy: Path) -> TrustEvidence:
    if set(images) != {"cp", "ap"}:
        raise TrustError("signed evidence requires exactly CP and AP images")
    cp = images["cp"]
    ap = images["ap"]
    if cp.version != ap.version or cp.security_counter != ap.security_counter \
            or cp.public_fingerprint != ap.public_fingerprint:
        raise TrustError("signed CP/AP version, counter or public root mismatch")
    bl1_fingerprint = require_key_matches_elf(
        bl1_private_key, bl1_elf, ".bk7258.trust.bl1",
        openssl=openssl, objcopy=objcopy,
    )
    mcuboot_fingerprint = require_key_matches_elf(
        mcuboot_private_key, bl2_elf, ".bk7258.trust.mcuboot",
        openssl=openssl, objcopy=objcopy,
    )
    if mcuboot_fingerprint != cp.public_fingerprint:
        raise TrustError("signed image root differs from the compiled BL2 root")
    _, bl1_public = public_fingerprint(bl1_private_key, openssl)
    _, mcuboot_public = public_fingerprint(mcuboot_private_key, openssl)
    return TrustEvidence(
        mode="signed",
        algorithm="ecdsa-p256-sha256",
        bl1_public_fingerprint=bl1_fingerprint,
        mcuboot_public_fingerprint=mcuboot_fingerprint,
        bl1_public_der=bl1_public.hex(),
        mcuboot_public_der=mcuboot_public.hex(),
        bl2_load_address=bl2_load_address,
        bl1_security_counter=bl1_security_counter,
        rollback="otp-readonly-plus-explicit-software-floor",
        images=tuple(
            {
                "artifact": name,
                "signed_sha256": row.sha256,
                "version": row.version,
                "security_counter": row.security_counter,
            }
            for name, row in sorted(images.items())
        ),
    )


def signed_release(*, layout: layout_domain.Layout,
                   artifacts: dict[str, Path], bl1_private_key: Path,
                   mcuboot_private_key: Path, bl1_elf: Path, bl2_elf: Path,
                   version: str, security_counter: int,
                   bl1_security_counter: int, official_imgtool: Path,
                   openssl: Path, objcopy: Path, nm: Path) -> SignedRelease:
    """Finalize the explicit signed release stages before container creation."""

    if set(artifacts) != {"boot", "cp", "ap", "bl2"}:
        raise TrustError("signed release inputs must be boot, cp, ap and bl2")
    raw = image_domain.read_artifacts(artifacts)
    copy_size = elf_symbol(bl1_elf, nm, "bk7258_bl1_bl2_copy_size")
    load_address = elf_symbol(bl2_elf, nm, "bk7258_bl2_load_address")
    expected_copy_size = (len(raw["bl2"]) + 31) // 32 * 32
    if copy_size != expected_copy_size:
        raise TrustError(
            f"BL1 copy size does not bind the supplied BL2: {copy_size} != {expected_copy_size}"
        )
    bl2_a = layout.artifact("bl2_a")
    bl2_b = layout.artifact("bl2_b")
    if copy_size > layout.logical_size(bl2_a) \
            or layout.logical_size(bl2_a) != layout.logical_size(bl2_b):
        raise TrustError("BL2 A/B capacity does not match the compiled copy size")

    with tempfile.TemporaryDirectory(prefix="bk7258-signed-release-") as name:
        temporary = Path(name)
        cp_signed = sign_mcuboot(
            input_image=artifacts["cp"],
            output_image=temporary / "cp-signed.bin",
            private_key=mcuboot_private_key,
            bl2_elf=bl2_elf,
            version=version,
            security_counter=security_counter,
            slot_size=layout.logical_size(layout.artifact("cp")),
            official_imgtool=official_imgtool,
            openssl=openssl,
            objcopy=objcopy,
        )
        ap_signed = sign_mcuboot(
            input_image=artifacts["ap"],
            output_image=temporary / "ap-signed.bin",
            private_key=mcuboot_private_key,
            bl2_elf=bl2_elf,
            version=version,
            security_counter=security_counter,
            slot_size=layout.logical_size(layout.artifact("ap")),
            official_imgtool=official_imgtool,
            openssl=openssl,
            objcopy=objcopy,
        )
        manifest_a = sign_bl1_manifest(
            bl2_image=artifacts["bl2"],
            output=temporary / "manifest-a.bin",
            private_key=bl1_private_key,
            static_address=layout.xip_base + layout.logical_offset(bl2_a),
            load_address=load_address,
            security_counter=bl1_security_counter,
            openssl=openssl,
        )
        manifest_b = sign_bl1_manifest(
            bl2_image=artifacts["bl2"],
            output=temporary / "manifest-b.bin",
            private_key=bl1_private_key,
            static_address=layout.xip_base + layout.logical_offset(bl2_b),
            load_address=load_address,
            security_counter=bl1_security_counter,
            openssl=openssl,
        )
        cp_bytes = cp_signed.path.read_bytes()
        ap_bytes = ap_signed.path.read_bytes()
        padded_bl2 = raw["bl2"].ljust(copy_size, bytes([image_domain.ERASE_BYTE]))
        finalized_raw = {
            "boot": raw["boot"],
            "cp": cp_bytes,
            "ap": ap_bytes,
            "pair": image_domain.pair(layout, cp_bytes, ap_bytes),
            "manifest_a": manifest_a.read_bytes(),
            "manifest_b": manifest_b.read_bytes(),
            "bl2_a": padded_bl2,
            "bl2_b": padded_bl2,
        }
        image_set = image_domain.finalize(layout, finalized_raw)
        evidence = signed_evidence(
            bl1_private_key=bl1_private_key,
            mcuboot_private_key=mcuboot_private_key,
            bl1_elf=bl1_elf,
            bl2_elf=bl2_elf,
            bl1_security_counter=bl1_security_counter,
            bl2_load_address=load_address,
            images={"cp": cp_signed, "ap": ap_signed},
            openssl=openssl,
            objcopy=objcopy,
        )
    return SignedRelease(image_set, evidence)


def compare_target_fingerprint(expected: str, observed: str) -> None:
    if len(expected) != 64 or any(character not in "0123456789abcdef" for character in expected):
        raise TrustError("expected public fingerprint is invalid")
    if observed.lower() != expected:
        raise TrustError(
            f"target public fingerprint mismatch: expected={expected} observed={observed.lower()}"
        )


def _public_pem(der: bytes) -> bytes:
    encoded = base64.b64encode(der).decode("ascii")
    lines = [encoded[index:index + 64] for index in range(0, len(encoded), 64)]
    return (
        "-----BEGIN PUBLIC KEY-----\n" + "\n".join(lines)
        + "\n-----END PUBLIC KEY-----\n"
    ).encode("ascii")


def _ecdsa_der(raw: bytes) -> bytes:
    if len(raw) != 64:
        raise TrustError("raw ECDSA signature must contain P-256 r||s")
    encoded = []
    for value in (raw[:32], raw[32:]):
        value = value.lstrip(b"\0") or b"\0"
        if value[0] & 0x80:
            value = b"\0" + value
        encoded.append(b"\x02" + bytes([len(value)]) + value)
    payload = b"".join(encoded)
    return b"\x30" + bytes([len(payload)]) + payload


def _mcuboot_metadata(data: bytes) -> tuple[int, str, int | None]:
    if len(data) < 32:
        raise TrustError("MCUboot image is shorter than its header")
    magic, _, header_size, protected_size, image_size = struct.unpack_from("<IIHHI", data)
    if magic != 0x96F3B83D or header_size != 0x200:
        raise TrustError("MCUboot image header is invalid")
    major, minor, revision, build = struct.unpack_from("<BBHI", data, 20)
    version = f"{major}.{minor}.{revision}+{build}"
    protected = header_size + image_size
    security_counter = None
    if protected_size:
        if protected + protected_size > len(data):
            raise TrustError("MCUboot protected TLV is truncated")
        magic_protected, total_protected = struct.unpack_from("<HH", data, protected)
        if magic_protected != 0x6908 or total_protected != protected_size:
            raise TrustError("MCUboot protected TLV header is invalid")
        offset = protected + 4
        while offset < protected + protected_size:
            if offset + 4 > protected + protected_size:
                raise TrustError("MCUboot protected TLV entry is truncated")
            tlv_type, _, size = struct.unpack_from("<BBH", data, offset)
            offset += 4
            if offset + size > protected + protected_size:
                raise TrustError("MCUboot protected TLV value is truncated")
            if tlv_type == 0x50:
                if size != 4 or security_counter is not None:
                    raise TrustError("MCUboot security counter TLV is malformed")
                security_counter = int.from_bytes(data[offset:offset + size], "little")
            offset += size
    tlv = protected + protected_size
    if tlv + 4 > len(data):
        raise TrustError("MCUboot image TLV is truncated")
    magic_tlv, total = struct.unpack_from("<HH", data, tlv)
    if magic_tlv != 0x6907 or total < 4 or tlv + total > len(data):
        raise TrustError("MCUboot image TLV header is invalid")
    return tlv + total, version, security_counter


def verify_signed_material(*, security: dict[str, object],
                           layout: dict[str, object], images: dict[str, bytes],
                           official_imgtool: Path, openssl: Path) -> None:
    """Cryptographically verify public package evidence without private keys."""

    if security.get("mode") != "signed":
        raise TrustError("package is not signed")
    try:
        bl1_der = bytes.fromhex(str(security["bl1_public_der"]))
        mcuboot_der = bytes.fromhex(str(security["mcuboot_public_der"]))
        crc_data_size = int(layout["crc_data_size"])
        crc_total_size = int(layout["crc_total_size"])
        xip_base = int(layout["xip_base"])
        partitions = layout["partitions"]
    except (KeyError, TypeError, ValueError) as error:
        raise TrustError("signed package evidence is incomplete") from error
    if not isinstance(partitions, list):
        raise TrustError("signed package partitions are malformed")
    rows = {
        row.get("artifact"): row
        for row in partitions if isinstance(row, dict) and row.get("artifact") is not None
    }
    required = {"boot", "cp", "ap", "manifest_a", "manifest_b", "bl2_a", "bl2_b"}
    if not required.issubset(images) or not required.issubset(rows):
        raise TrustError("signed package lacks a required trust artifact")

    def decoded(artifact: str) -> bytes:
        row = rows[artifact]
        data = images[artifact]
        if row.get("type") == "code" and crc_total_size > crc_data_size:
            return image_domain.crc_decode(data, crc_data_size, crc_total_size)
        return data

    boot = decoded("boot")
    bl2_a = decoded("bl2_a")
    bl2_b = decoded("bl2_b")
    bl1_xy = bl1_der[-64:]
    bl1_anchor = bl1_xy + hashlib.sha256(b"\x04" + bl1_xy).digest()
    bl2_anchor = mcuboot_der + b"\0" + (91).to_bytes(4, "little")
    if boot.count(bl1_anchor) != 1 or bl2_a.count(bl2_anchor) != 1 \
            or bl2_b != bl2_a:
        raise TrustError("packaged BL1/BL2 public roots or A/B bytes do not match")

    evidence_rows = security.get("images")
    if not isinstance(evidence_rows, list):
        raise TrustError("signed image evidence is missing")
    evidence = {row.get("artifact"): row for row in evidence_rows if isinstance(row, dict)}
    official_imgtool = _regular(official_imgtool, "official MCUboot imgtool")
    openssl = _regular(openssl, "OpenSSL executable")
    with tempfile.TemporaryDirectory(prefix="bk7258-public-verify-") as name:
        temporary = Path(name)
        mcuboot_pem = temporary / "mcuboot-public.pem"
        bl1_pem = temporary / "bl1-public.pem"
        mcuboot_pem.write_bytes(_public_pem(mcuboot_der))
        bl1_pem.write_bytes(_public_pem(bl1_der))
        for artifact in ("cp", "ap"):
            logical = decoded(artifact)
            length, version, counter = _mcuboot_metadata(logical)
            signed = logical[:length]
            expected_row = evidence.get(artifact, {})
            if hashlib.sha256(signed).hexdigest() != expected_row.get("signed_sha256") \
                    or version != expected_row.get("version") \
                    or counter != expected_row.get("security_counter"):
                raise TrustError(f"signed {artifact} generation evidence does not match")
            signed_path = temporary / f"{artifact}.bin"
            signed_path.write_bytes(signed)
            _run(
                [
                    sys.executable, str(official_imgtool), "verify",
                    "-k", str(mcuboot_pem), str(signed_path),
                ],
                f"public MCUboot {artifact} verification",
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )

        for manifest_name, bl2_name in (
            ("manifest_a", "bl2_a"), ("manifest_b", "bl2_b")
        ):
            manifest = images[manifest_name]
            bl2 = decoded(bl2_name)
            if len(manifest) != 256:
                raise TrustError("BL1 Manifest must be exactly 256 bytes")
            fields = struct.unpack_from("<12I", manifest)
            row = rows[bl2_name]
            expected_static = xip_base + int(row["offset"]) // crc_total_size * crc_data_size
            image_size = fields[10]
            if fields[:8] != (
                0xA1BC2FD8, 0x00010001, int(security["bl1_security_counter"]),
                0xD5, 0x00030619, 1, 0, 0,
            ) or fields[8] != expected_static \
                    or fields[9] != int(security["bl2_load_address"]) \
                    or fields[11] != int(security["bl2_load_address"]) \
                    or not 0 < image_size <= len(bl2) \
                    or manifest[0x30:0x50] != hashlib.sha256(bl2[:image_size]).digest() \
                    or any(value != 0xff for value in bl2[image_size:]) \
                    or manifest[0x54:0x95] != bl1_der[-65:] \
                    or any(value != 0xff for value in manifest[0xD5:]):
                raise TrustError(f"{manifest_name} content does not bind packaged BL2")
            signature = temporary / f"{manifest_name}.der"
            payload = temporary / f"{manifest_name}.signed"
            signature.write_bytes(_ecdsa_der(manifest[0x95:0xD5]))
            payload.write_bytes(manifest[:0x95])
            _run(
                [
                    str(openssl), "dgst", "-sha256", "-verify", str(bl1_pem),
                    "-signature", str(signature), str(payload),
                ],
                f"public {manifest_name} verification",
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
            )
