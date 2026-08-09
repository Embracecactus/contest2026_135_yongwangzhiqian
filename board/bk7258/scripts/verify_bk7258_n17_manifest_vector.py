#!/usr/bin/env python3
"""Verify the public-only N17 signed-manifest ABI test vector."""

from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from typing import Callable


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent
DEFAULT_VECTOR = SCRIPT_DIR / "testdata/n17_manifest_v1/vector.json"
MANIFEST_CORE = BOARD_DIR / "bootloader/boot_n17_manifest_core.c"
MANIFEST_HARNESS = SCRIPT_DIR / "host/bk7258_n17_manifest_harness.c"

MANIFEST_SIZE = 512
SIGNED_SIZE = 448
SIGNATURE_SIZE = 64
P256_ORDER = int(
    "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",
    16,
)
P256_SPKI_PREFIX = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d03010703420004"
)
EXPECTED_LAYOUT_SHA256 = bytes.fromhex(
    "32d3519eada0a7f77a284998e785fdb1daa55c691b3bfaf1a92b4097ce398203"
)
EXPECTED_PAIR_SIZE = 0x275000
RELEASE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+\-]{0,22}")

NEGATIVE_CASES = (
    "every_signed_region_bit",
    "changed_pair_payload",
    "changed_cp_payload",
    "changed_ap_payload",
    "wrong_layout",
    "wrong_product",
    "wrong_board",
    "wrong_chip",
    "unknown_key_id",
    "zero_r",
    "out_of_range_r",
    "high_s_twin",
    "der_signature",
    "noncanonical_text",
    "nonzero_reserved",
    "stale_security_counter",
    "copied_signature_different_domain",
)


class VectorError(RuntimeError):
    """Raised when a vector or mutated case violates the accepted ABI."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VectorError(message)


class CManifestVerifier:
    """ctypes view of the actual portable Tier-1 C parser."""

    def __init__(self, library: Path, public_xy: bytes):
        self.public_xy = (ctypes.c_uint8 * 64).from_buffer_copy(public_xy)
        self.library = ctypes.CDLL(str(library))
        self.verify = self.library.bk7258_n17_manifest_host_verify
        self.verify.argtypes = (
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64,
            ctypes.POINTER(ctypes.c_uint8),
        )
        self.verify.restype = ctypes.c_int

    def __call__(self, manifest: bytes, minimum_counter: int) -> tuple[int, bytes]:
        payload = (ctypes.c_uint8 * len(manifest)).from_buffer_copy(manifest)
        digest = (ctypes.c_uint8 * 32)()
        ret = self.verify(
            payload,
            len(manifest),
            self.public_xy,
            minimum_counter,
            digest,
        )
        return ret, bytes(digest)


def build_c_manifest_verifier(tmpdir: Path, public_xy: bytes) -> CManifestVerifier:
    output = tmpdir / "libbk7258-n17-manifest.so"
    command = (
        "cc",
        "-std=c11",
        "-fPIC",
        "-shared",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-deprecated-declarations",
        f"-I{BOARD_DIR / 'bootloader'}",
        f"-I{BOARD_DIR / 'chip/include'}",
        str(MANIFEST_CORE),
        str(MANIFEST_HARNESS),
        "-lcrypto",
        "-o",
        str(output),
    )
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    require(
        result.returncode == 0,
        "cannot compile N17 Manifest C parser:\n" + result.stderr,
    )
    return CManifestVerifier(output, public_xy)


def read_hex(value: object, name: str, size: int | None = None) -> bytes:
    require(isinstance(value, str), f"{name} is not a hexadecimal string")
    try:
        raw = bytes.fromhex(value)
    except ValueError as error:
        raise VectorError(f"{name} is not valid hexadecimal") from error
    if size is not None:
        require(len(raw) == size, f"{name} must be {size} bytes")
    return raw


def canonical_text(raw: bytes, name: str) -> str:
    try:
        nul = raw.index(0)
    except ValueError as error:
        raise VectorError(f"{name} is missing its canonical NUL") from error
    require(nul > 0, f"{name} is empty")
    require(not any(raw[nul + 1 :]), f"{name} has non-zero padding")
    try:
        return raw[:nul].decode("ascii")
    except UnicodeDecodeError as error:
        raise VectorError(f"{name} is not ASCII") from error


def der_integer(value: int) -> bytes:
    raw = value.to_bytes(32, "big").lstrip(b"\x00") or b"\x00"
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return b"\x02" + bytes((len(raw),)) + raw


def der_signature(r_value: int, s_value: int) -> bytes:
    body = der_integer(r_value) + der_integer(s_value)
    return b"\x30" + bytes((len(body),)) + body


def pem_public_key(public_xy: bytes) -> bytes:
    require(len(public_xy) == 64, "public x || y key must be 64 bytes")
    encoded = base64.b64encode(P256_SPKI_PREFIX + public_xy).decode("ascii")
    lines = [encoded[index : index + 64] for index in range(0, len(encoded), 64)]
    return (
        "-----BEGIN PUBLIC KEY-----\n"
        + "\n".join(lines)
        + "\n-----END PUBLIC KEY-----\n"
    ).encode("ascii")


def openssl_verify(signed: bytes, signature: bytes, public_xy: bytes) -> None:
    r_value = int.from_bytes(signature[:32], "big")
    s_value = int.from_bytes(signature[32:], "big")
    with tempfile.TemporaryDirectory(prefix="bk7258-n17-vector-") as tmp:
        tmpdir = Path(tmp)
        message_path = tmpdir / "signed-region.bin"
        signature_path = tmpdir / "signature.der"
        public_path = tmpdir / "public-key.pem"
        message_path.write_bytes(signed)
        signature_path.write_bytes(der_signature(r_value, s_value))
        public_path.write_bytes(pem_public_key(public_xy))
        result = subprocess.run(
            (
                "openssl",
                "dgst",
                "-sha256",
                "-verify",
                str(public_path),
                "-signature",
                str(signature_path),
                str(message_path),
            ),
            check=False,
            capture_output=True,
            text=True,
        )
    require(result.returncode == 0, "OpenSSL rejected the ECDSA signature")


def validate_manifest(
    manifest: bytes,
    public_xy: bytes,
    *,
    minimum_security_counter: int = 1,
) -> dict[str, object]:
    require(len(manifest) == MANIFEST_SIZE, "manifest size is not 512 bytes")
    require(manifest[:8] == b"BKOTA17S", "domain magic mismatch")
    require(struct.unpack_from("<H", manifest, 0x008)[0] == 1, "version mismatch")
    require(
        struct.unpack_from("<H", manifest, 0x00A)[0] == MANIFEST_SIZE,
        "encoded manifest size mismatch",
    )
    require(
        struct.unpack_from("<H", manifest, 0x00C)[0] == SIGNED_SIZE,
        "encoded signed size mismatch",
    )
    require(
        struct.unpack_from("<H", manifest, 0x00E)[0] == SIGNATURE_SIZE,
        "encoded signature size mismatch",
    )
    require(struct.unpack_from("<I", manifest, 0x010)[0] == 0, "flags are not zero")
    require(struct.unpack_from("<H", manifest, 0x014)[0] == 1, "signature algorithm mismatch")
    require(struct.unpack_from("<H", manifest, 0x016)[0] == 1, "digest algorithm mismatch")
    require(struct.unpack_from("<H", manifest, 0x018)[0] == 1, "image encoding mismatch")
    require(struct.unpack_from("<H", manifest, 0x01A)[0] == 2, "component count mismatch")

    key_id = struct.unpack_from("<I", manifest, 0x01C)[0]
    require(key_id == 1, "unknown key ID")
    security_counter = struct.unpack_from("<Q", manifest, 0x020)[0]
    require(
        security_counter >= minimum_security_counter,
        "security counter is below the accepted floor",
    )

    product = canonical_text(manifest[0x028:0x038], "product_id")
    board = canonical_text(manifest[0x038:0x048], "board_id")
    chip = canonical_text(manifest[0x048:0x058], "chip_id")
    release = canonical_text(manifest[0x078:0x090], "release_version")
    require(product == "openvela-bk7258", "product identity mismatch")
    require(board == "bk7258-t5ai", "board identity mismatch")
    require(chip == "bk7258", "chip identity mismatch")
    require(RELEASE_RE.fullmatch(release) is not None, "release version is not canonical")
    require(manifest[0x058:0x078] == EXPECTED_LAYOUT_SHA256, "layout identity mismatch")
    pair_size = struct.unpack_from("<I", manifest, 0x090)[0]
    require(pair_size == EXPECTED_PAIR_SIZE, "pair physical size mismatch")
    cp_length = struct.unpack_from("<I", manifest, 0x094)[0]
    ap_length = struct.unpack_from("<I", manifest, 0x098)[0]
    require(cp_length > 0 and ap_length > 0, "component length is zero")
    require(manifest[0x09C:0x0A0] == b"\x00" * 4, "reserved0 is not zero")
    require(manifest[0x100:0x1C0] == b"\x00" * 192, "signed reserved bytes are not zero")

    signature = manifest[SIGNED_SIZE:]
    r_value = int.from_bytes(signature[:32], "big")
    s_value = int.from_bytes(signature[32:], "big")
    require(1 <= r_value < P256_ORDER, "signature r is outside the P-256 scalar range")
    require(1 <= s_value < P256_ORDER, "signature s is outside the P-256 scalar range")
    require(s_value <= P256_ORDER // 2, "signature is not low-S")
    openssl_verify(manifest[:SIGNED_SIZE], signature, public_xy)

    return {
        "key_id": key_id,
        "security_counter": security_counter,
        "release_version": release,
        "pair_physical_size": pair_size,
        "cp_logical_length": cp_length,
        "ap_logical_length": ap_length,
        "signed_sha256": hashlib.sha256(manifest[:SIGNED_SIZE]).hexdigest(),
        "manifest_sha256": hashlib.sha256(manifest).hexdigest(),
    }


def recipe_digest(recipe: dict[str, object], mutation_offset: int | None = None) -> str:
    require(recipe.get("algorithm") == "sha256-counter-v1", "unknown payload recipe")
    domain = read_hex(recipe.get("domain_hex"), "payload domain")
    length = recipe.get("length")
    require(isinstance(length, int) and length > 0, "invalid payload length")
    require(mutation_offset is None or 0 <= mutation_offset < length, "invalid mutation offset")

    output_hash = hashlib.sha256()
    produced = 0
    counter = 0
    while produced < length:
        block = bytearray(hashlib.sha256(domain + struct.pack("<Q", counter)).digest())
        block_length = min(len(block), length - produced)
        if mutation_offset is not None and produced <= mutation_offset < produced + block_length:
            block[mutation_offset - produced] ^= 1
        output_hash.update(block[:block_length])
        produced += block_length
        counter += 1
    return output_hash.hexdigest()


def verify_payloads(
    manifest: bytes,
    recipes: dict[str, dict[str, object]],
    mutation: str | None = None,
) -> None:
    fields = {
        "pair": (0x0A0, EXPECTED_PAIR_SIZE),
        "cp": (0x0C0, struct.unpack_from("<I", manifest, 0x094)[0]),
        "ap": (0x0E0, struct.unpack_from("<I", manifest, 0x098)[0]),
    }
    for name, (offset, expected_length) in fields.items():
        recipe = recipes.get(name)
        require(isinstance(recipe, dict), f"missing {name} payload recipe")
        require(recipe.get("length") == expected_length, f"{name} payload length mismatch")
        changed_at = expected_length // 2 if mutation == name else None
        digest = recipe_digest(recipe, changed_at)
        require(bytes.fromhex(digest) == manifest[offset : offset + 32], f"{name} payload digest mismatch")
        require(recipe.get("sha256") == digest if changed_at is None else True, f"{name} recipe digest mismatch")


def mutate_manifest(name: str, manifest: bytes) -> tuple[bytes, int]:
    changed = bytearray(manifest)
    minimum_counter = 1
    if name == "wrong_layout":
        changed[0x058] ^= 1
    elif name == "wrong_product":
        changed[0x028] ^= 1
    elif name == "wrong_board":
        changed[0x038] ^= 1
    elif name == "wrong_chip":
        changed[0x048] ^= 1
    elif name == "unknown_key_id":
        struct.pack_into("<I", changed, 0x01C, 2)
    elif name == "zero_r":
        changed[0x1C0:0x1E0] = b"\x00" * 32
    elif name == "out_of_range_r":
        changed[0x1C0:0x1E0] = P256_ORDER.to_bytes(32, "big")
    elif name == "high_s_twin":
        low_s = int.from_bytes(changed[0x1E0:0x200], "big")
        changed[0x1E0:0x200] = (P256_ORDER - low_s).to_bytes(32, "big")
    elif name == "der_signature":
        r_value = int.from_bytes(changed[0x1C0:0x1E0], "big")
        s_value = int.from_bytes(changed[0x1E0:0x200], "big")
        return bytes(changed[:SIGNED_SIZE]) + der_signature(r_value, s_value), minimum_counter
    elif name == "noncanonical_text":
        changed[0x08F] = 1
    elif name == "nonzero_reserved":
        changed[0x100] = 1
    elif name == "stale_security_counter":
        minimum_counter = struct.unpack_from("<Q", changed, 0x020)[0] + 1
    elif name == "copied_signature_different_domain":
        changed[0x007] ^= 1
    else:
        raise VectorError(f"unknown manifest mutation {name}")
    return bytes(changed), minimum_counter


def expect_rejected(action: Callable[[], object], name: str) -> None:
    try:
        action()
    except (VectorError, OSError):
        return
    raise VectorError(f"negative case unexpectedly accepted: {name}")


def expect_c_rejected(
    verifier: CManifestVerifier,
    manifest: bytes,
    minimum_counter: int,
    name: str,
) -> None:
    status, _ = verifier(manifest, minimum_counter)
    require(status < 0, f"C parser unexpectedly accepted: {name}")


def verify_negative_cases(
    vector: dict[str, object],
    manifest: bytes,
    public_xy: bytes,
    recipes: dict[str, dict[str, object]],
    c_verifier: CManifestVerifier,
) -> tuple[int, int]:
    listed = vector.get("negative_cases")
    require(listed == list(NEGATIVE_CASES), "negative-case list differs from the ABI vector contract")
    checked = 0
    c_checked = 0
    for name in NEGATIVE_CASES:
        if name == "every_signed_region_bit":
            for offset in range(SIGNED_SIZE):
                for bit in range(8):
                    changed = bytearray(manifest)
                    changed[offset] ^= 1 << bit
                    expect_rejected(
                        lambda value=bytes(changed): validate_manifest(value, public_xy),
                        f"{name}@0x{offset:03x}:{bit}",
                    )
                    expect_c_rejected(
                        c_verifier,
                        bytes(changed),
                        1,
                        f"{name}@0x{offset:03x}:{bit}",
                    )
                    checked += 1
                    c_checked += 1
        elif name.startswith("changed_") and name.endswith("_payload"):
            payload_name = name.removeprefix("changed_").removesuffix("_payload")
            expect_rejected(
                lambda value=payload_name: verify_payloads(manifest, recipes, value),
                name,
            )
            checked += 1
        else:
            changed, floor = mutate_manifest(name, manifest)
            expect_rejected(
                lambda value=changed, minimum=floor: validate_manifest(
                    value,
                    public_xy,
                    minimum_security_counter=minimum,
                ),
                name,
            )
            expect_c_rejected(c_verifier, changed, floor, name)
            checked += 1
            c_checked += 1
    return checked, c_checked


def verify_vector(path: Path) -> dict[str, object]:
    try:
        vector = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VectorError(f"cannot read vector {path}: {error}") from error
    require(vector.get("schema") == "bk7258-n17-manifest-vector-v1", "vector schema mismatch")

    manifest = read_hex(vector.get("manifest_hex"), "manifest_hex", MANIFEST_SIZE)
    signed = read_hex(vector.get("signed_region_hex"), "signed_region_hex", SIGNED_SIZE)
    signature = read_hex(vector.get("signature_r_s_hex"), "signature_r_s_hex", SIGNATURE_SIZE)
    public_xy = read_hex(vector.get("public_key_x_y_hex"), "public_key_x_y_hex", 64)
    require(manifest[:SIGNED_SIZE] == signed, "signed region does not match manifest")
    require(manifest[SIGNED_SIZE:] == signature, "raw signature does not match manifest")

    result = validate_manifest(manifest, public_xy)
    require(result["signed_sha256"] == vector.get("signed_region_sha256"), "signed digest mismatch")
    require(result["manifest_sha256"] == vector.get("manifest_sha256"), "manifest digest mismatch")

    expected = vector.get("expected_fields")
    require(isinstance(expected, dict), "missing expected fields")
    for name in (
        "key_id",
        "security_counter",
        "release_version",
        "pair_physical_size",
        "cp_logical_length",
        "ap_logical_length",
    ):
        require(result[name] == expected.get(name), f"expected field mismatch: {name}")

    recipes = vector.get("payload_recipes")
    require(isinstance(recipes, dict), "missing payload recipes")
    verify_payloads(manifest, recipes)
    with tempfile.TemporaryDirectory(prefix="bk7258-n17-c-core-") as tmp:
        c_verifier = build_c_manifest_verifier(Path(tmp), public_xy)
        c_status, c_signed_digest = c_verifier(manifest, 1)
        require(c_status == 0, "portable N17 Manifest C parser rejected the vector")
        require(
            c_signed_digest.hex() == vector.get("signed_region_sha256"),
            "C parser signed-region digest mismatch",
        )
        negative_count, c_negative_count = verify_negative_cases(
            vector, manifest, public_xy, recipes, c_verifier
        )
    result["negative_checks"] = negative_count
    result["c_core_negative_checks"] = c_negative_count
    result["c_core"] = "verified"
    result["openssl"] = "verified"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vector", type=Path, default=DEFAULT_VECTOR)
    args = parser.parse_args()
    try:
        result = verify_vector(args.vector.resolve())
    except (VectorError, OSError) as error:
        print(f"N17 manifest vector verification failed: {error}")
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
