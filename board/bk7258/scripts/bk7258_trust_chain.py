#!/usr/bin/env python3
"""Emit and verify the BK7258 pre-flash trust-chain contract.

The contract contains public fingerprints and target addresses only.  Private
key material and private-key paths must never be copied into it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any


CONTRACT_FORMAT = 1
CONTRACT_KIND = "bk7258-preflash-trust-chain"
BL1_XY_ANCHOR = "bl1_manifest_root_xy_sha256"
BL1_SEC1_ANCHOR = "bl1_manifest_root_sec1_sha256"
BL2_SPKI_ANCHOR = "bl2_mcuboot_signing_spki_sha256"
ANCHOR_ORDER = (BL1_XY_ANCHOR, BL1_SEC1_ANCHOR, BL2_SPKI_ANCHOR)
BOOT_XIP_BASE = 0x02000000
BL2_LOAD_BASE = 0x28020000
BL2_PRIMARY_XIP_BASE = 0x024D0000
EXPECTED_SYMBOLS = {
    BL1_XY_ANCHOR: "bk7258_bl1_manifest_root_public_key_hash",
    BL1_SEC1_ANCHOR: "bk7258_beken_manifest_root_public_key_hash",
    BL2_SPKI_ANCHOR: "ecdsa_pub_key",
}
FIXED_TARGET_ADDRESSES = {
    BL1_XY_ANCHOR: 0x0200FD40,
    BL1_SEC1_ANCHOR: 0x0200FD60,
    BL2_SPKI_ANCHOR: 0x024D2F00,
}
LEGACY_TARGET_ADDRESSES = {
    BL1_XY_ANCHOR: 0x02002774,
    BL1_SEC1_ANCHOR: 0x02002754,
    BL2_SPKI_ANCHOR: 0x024D27EC,
}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
MEMORY_LINE_RE = re.compile(
    r"^\s*(?:J-Link>)?\s*([0-9a-fA-F]{8})\s*=\s*(.*)$"
)
WORD_RE = re.compile(r"(?<![0-9a-fA-F])([0-9a-fA-F]{8})(?![0-9a-fA-F])")


class TrustChainError(RuntimeError):
    """Raised when a trust contract or target fingerprint is invalid."""


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def parse_uint32(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"not an integer: {value}") from error
    if parsed < 0 or parsed > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"not a uint32: {value}")
    return parsed


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TrustChainError(message)


def load_p256_private_key(path: Path) -> Any:
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError as error:
        raise TrustChainError(
            "cryptography is required to derive public trust fingerprints"
        ) from error

    try:
        key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    except (OSError, ValueError, TypeError) as error:
        raise TrustChainError("cannot load external P-256 private key") from error
    require(
        isinstance(key, ec.EllipticCurvePrivateKey)
        and isinstance(key.curve, ec.SECP256R1),
        "external key must be an unencrypted P-256 private key",
    )
    return key


def symbol_addresses(elf: Path, nm: str, names: set[str]) -> dict[str, int]:
    try:
        output = subprocess.run(
            [nm, "-n", str(elf)],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        raise TrustChainError(f"cannot inspect ELF symbols: {elf}") from error

    symbols: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1] in names:
            try:
                symbols[fields[-1]] = int(fields[0], 16)
            except ValueError as error:
                raise TrustChainError(
                    f"invalid symbol address in {elf}: {line}"
                ) from error
    missing = names - symbols.keys()
    require(not missing, f"missing symbols in {elf}: {', '.join(sorted(missing))}")
    return symbols


def artifact_slice(
    payload: bytes, symbol: int, base: int, length: int, description: str
) -> bytes:
    offset = symbol - base
    require(offset >= 0, f"{description} precedes its image base")
    end = offset + length
    require(end <= len(payload), f"{description} falls outside its raw image")
    return payload[offset:end]


def artifact_entry(name: str, payload: bytes) -> dict[str, object]:
    return {
        "file": name,
        "length": len(payload),
        "sha256": sha256_bytes(payload),
    }


def anchor_entry(
    symbol: str,
    target_address: int,
    length: int,
    expected_sha256: str,
    target_representation: str,
    compatible_target_addresses: list[int],
) -> dict[str, object]:
    return {
        "algorithm": "sha256",
        "symbol": symbol,
        "target_address": target_address,
        "length": length,
        "expected_sha256": expected_sha256,
        "target_representation": target_representation,
        "compatible_target_addresses": compatible_target_addresses,
    }


def emit_contract(args: argparse.Namespace) -> dict[str, object]:
    try:
        from cryptography.hazmat.primitives import serialization
    except ImportError as error:
        raise TrustChainError(
            "cryptography is required to derive public trust fingerprints"
        ) from error

    require(args.boot_xip_base == BOOT_XIP_BASE, "unsupported BL1 XIP base")
    require(args.bl2_load_base == BL2_LOAD_BASE, "unsupported BL2 load base")
    require(
        args.bl2_primary_xip_base == BL2_PRIMARY_XIP_BASE,
        "unsupported primary BL2 XIP base",
    )

    bl1_key = load_p256_private_key(args.bl1_manifest_key)
    mcuboot_key = load_p256_private_key(args.mcuboot_signing_key)

    numbers = bl1_key.public_key().public_numbers()
    xy = numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")
    sec1 = b"\x04" + xy
    xy_hash = hashlib.sha256(xy).digest()
    sec1_hash = hashlib.sha256(sec1).digest()
    spki = mcuboot_key.public_key().public_bytes(
        serialization.Encoding.DER,
        serialization.PublicFormat.SubjectPublicKeyInfo,
    )

    boot_symbols = symbol_addresses(
        args.bootloader_elf,
        args.nm,
        {
            "bk7258_bl1_manifest_root_public_key_hash",
            "bk7258_beken_manifest_root_public_key_hash",
        },
    )
    bl2_symbols = symbol_addresses(
        args.bl2_elf, args.nm, {"ecdsa_pub_key", "ecdsa_pub_key_len"}
    )
    try:
        bootloader = args.bootloader_bin.read_bytes()
        bl2 = args.bl2_bin.read_bytes()
    except OSError as error:
        raise TrustChainError(f"cannot read built trust artifact: {error}") from error

    xy_symbol = boot_symbols["bk7258_bl1_manifest_root_public_key_hash"]
    sec1_symbol = boot_symbols["bk7258_beken_manifest_root_public_key_hash"]
    require(
        artifact_slice(
            bootloader, xy_symbol, args.boot_xip_base, len(xy_hash),
            "BL1 X||Y root hash",
        ) == xy_hash,
        "bootloader binary does not contain the BL1 X||Y root hash derived "
        "from the supplied Manifest key",
    )
    require(
        artifact_slice(
            bootloader, sec1_symbol, args.boot_xip_base, len(sec1_hash),
            "BL1 SEC1 root hash",
        ) == sec1_hash,
        "bootloader binary does not contain the BL1 SEC1 root hash derived "
        "from the supplied Manifest key",
    )

    spki_symbol = bl2_symbols["ecdsa_pub_key"]
    require(
        artifact_slice(
            bl2, spki_symbol, args.bl2_load_base, len(spki),
            "BL2 MCUboot public key",
        ) == spki,
        "BL2 binary does not contain the public key derived from the "
        "supplied MCUboot signing key",
    )
    encoded_length = artifact_slice(
        bl2,
        bl2_symbols["ecdsa_pub_key_len"],
        args.bl2_load_base,
        4,
        "BL2 MCUboot public-key length",
    )
    require(
        int.from_bytes(encoded_length, "little") == len(spki),
        "BL2 public-key length symbol disagrees with its DER key",
    )

    bl2_target = args.bl2_primary_xip_base + (
        spki_symbol - args.bl2_load_base
    )
    document: dict[str, object] = {
        "format": CONTRACT_FORMAT,
        "kind": CONTRACT_KIND,
        "policy": {
            "preflash_target_match_required": True,
            "mismatch_action": "refuse-flash",
            "normal_download_may_rotate_roots": False,
        },
        "anchors": {
            BL1_XY_ANCHOR: anchor_entry(
                "bk7258_bl1_manifest_root_public_key_hash",
                xy_symbol,
                len(xy_hash),
                xy_hash.hex(),
                "stored-sha256",
                [LEGACY_TARGET_ADDRESSES[BL1_XY_ANCHOR]],
            ),
            BL1_SEC1_ANCHOR: anchor_entry(
                "bk7258_beken_manifest_root_public_key_hash",
                sec1_symbol,
                len(sec1_hash),
                sec1_hash.hex(),
                "stored-sha256",
                [LEGACY_TARGET_ADDRESSES[BL1_SEC1_ANCHOR]],
            ),
            BL2_SPKI_ANCHOR: anchor_entry(
                "ecdsa_pub_key",
                bl2_target,
                len(spki),
                sha256_bytes(spki),
                "der-subject-public-key-info",
                [LEGACY_TARGET_ADDRESSES[BL2_SPKI_ANCHOR]],
            ),
        },
        "artifacts": {
            "bootloader": artifact_entry(args.bootloader_bin.name, bootloader),
            "bl2": artifact_entry(args.bl2_bin.name, bl2),
        },
    }
    validate_contract_document(document)
    return document


def validate_artifact(name: str, entry: object) -> None:
    require(isinstance(entry, dict), f"missing artifact entry: {name}")
    require(
        set(entry) == {"file", "length", "sha256"},
        f"unexpected artifact fields: {name}",
    )
    file_name = entry.get("file")
    require(
        isinstance(file_name, str)
        and file_name
        and Path(file_name).name == file_name,
        f"invalid artifact file name: {name}",
    )
    require(
        file_name == ("bootloader.bin" if name == "bootloader" else "bl2.bin"),
        f"unexpected artifact file name: {name}",
    )
    require(
        isinstance(entry.get("length"), int) and entry["length"] > 0,
        f"invalid artifact length: {name}",
    )
    require(
        isinstance(entry.get("sha256"), str)
        and SHA256_RE.fullmatch(entry["sha256"]) is not None,
        f"invalid artifact SHA-256: {name}",
    )


def validate_anchor(name: str, entry: object) -> None:
    require(isinstance(entry, dict), f"missing trust anchor: {name}")
    require(
        set(entry) == {
            "algorithm", "symbol", "target_address", "length",
            "expected_sha256", "target_representation",
            "compatible_target_addresses",
        },
        f"unexpected trust-anchor fields: {name}",
    )
    require(entry.get("algorithm") == "sha256", f"invalid algorithm: {name}")
    require(
        entry.get("symbol") == EXPECTED_SYMBOLS[name],
        f"invalid symbol: {name}",
    )
    require(
        isinstance(entry.get("target_address"), int)
        and 0 <= entry["target_address"] <= 0xFFFFFFFF
        and entry["target_address"] % 4 == 0,
        f"invalid target address: {name}",
    )
    require(
        entry["target_address"] == FIXED_TARGET_ADDRESSES[name],
        f"fixed target address drift: {name}",
    )
    require(
        entry.get("compatible_target_addresses") ==
        [LEGACY_TARGET_ADDRESSES[name]],
        f"legacy target address drift: {name}",
    )
    require(
        isinstance(entry.get("length"), int) and 0 < entry["length"] <= 512,
        f"invalid target length: {name}",
    )
    require(
        isinstance(entry.get("expected_sha256"), str)
        and SHA256_RE.fullmatch(entry["expected_sha256"]) is not None,
        f"invalid expected SHA-256: {name}",
    )
    representation = entry.get("target_representation")
    if name in (BL1_XY_ANCHOR, BL1_SEC1_ANCHOR):
        require(entry["length"] == 32, f"BL1 anchor must be 32 bytes: {name}")
        require(
            representation == "stored-sha256",
            f"invalid BL1 target representation: {name}",
        )
    else:
        require(
            representation == "der-subject-public-key-info",
            f"invalid BL2 target representation: {name}",
        )


def validate_contract_document(document: object) -> dict[str, object]:
    require(isinstance(document, dict), "trust-chain contract must be an object")
    require(
        set(document) == {"format", "kind", "policy", "anchors", "artifacts"},
        "unexpected trust-chain contract fields",
    )
    require(document.get("format") == CONTRACT_FORMAT, "unsupported contract format")
    require(document.get("kind") == CONTRACT_KIND, "invalid contract kind")
    policy = document.get("policy")
    require(isinstance(policy, dict), "trust-chain policy is missing")
    require(
        set(policy) == {
            "preflash_target_match_required", "mismatch_action",
            "normal_download_may_rotate_roots",
        },
        "unexpected trust-chain policy fields",
    )
    require(
        policy.get("preflash_target_match_required") is True,
        "pre-flash target matching must be required",
    )
    require(
        policy.get("mismatch_action") == "refuse-flash",
        "trust mismatch must refuse flashing",
    )
    require(
        policy.get("normal_download_may_rotate_roots") is False,
        "normal download must not rotate trust roots",
    )

    anchors = document.get("anchors")
    require(isinstance(anchors, dict), "trust anchors are missing")
    require(set(anchors) == set(ANCHOR_ORDER), "trust anchor set is incomplete")
    for name in ANCHOR_ORDER:
        validate_anchor(name, anchors[name])

    artifacts = document.get("artifacts")
    require(isinstance(artifacts, dict), "trust artifacts are missing")
    require(set(artifacts) == {"bootloader", "bl2"}, "artifact set is incomplete")
    for name in ("bootloader", "bl2"):
        validate_artifact(name, artifacts[name])
    return document


def load_contract(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TrustChainError(f"cannot load trust-chain contract {path}: {error}") \
            from error
    return validate_contract_document(document)


def verify_contract_artifacts(
    document: dict[str, object], package: Path, nm: str = "arm-none-eabi-nm",
) -> dict[str, object]:
    """Rebind a public contract to the ELF and raw bytes in one package."""

    artifacts = document["artifacts"]
    payloads: dict[str, bytes] = {}
    for name in ("bootloader", "bl2"):
        entry = artifacts[name]
        path = package / entry["file"]
        try:
            payload = path.read_bytes()
        except OSError as error:
            raise TrustChainError(f"cannot read trust artifact {path}: {error}") \
                from error
        require(len(payload) == entry["length"], f"artifact length mismatch: {name}")
        require(
            sha256_bytes(payload) == entry["sha256"],
            f"artifact SHA-256 mismatch: {name}",
        )
        payloads[name] = payload

    boot_symbols = symbol_addresses(
        package / "bootloader.elf",
        nm,
        {
            EXPECTED_SYMBOLS[BL1_XY_ANCHOR],
            EXPECTED_SYMBOLS[BL1_SEC1_ANCHOR],
        },
    )
    bl2_symbols = symbol_addresses(
        package / "bl2.elf", nm, {EXPECTED_SYMBOLS[BL2_SPKI_ANCHOR],
                                   "ecdsa_pub_key_len"}
    )
    anchors = document["anchors"]
    for name in (BL1_XY_ANCHOR, BL1_SEC1_ANCHOR):
        entry = anchors[name]
        symbol = boot_symbols[EXPECTED_SYMBOLS[name]]
        require(
            entry["target_address"] == symbol,
            f"contract target address disagrees with bootloader ELF: {name}",
        )
        stored = artifact_slice(
            payloads["bootloader"], symbol, BOOT_XIP_BASE, entry["length"], name
        )
        require(
            stored.hex() == entry["expected_sha256"],
            f"contract fingerprint disagrees with bootloader bytes: {name}",
        )

    bl2_entry = anchors[BL2_SPKI_ANCHOR]
    spki_symbol = bl2_symbols[EXPECTED_SYMBOLS[BL2_SPKI_ANCHOR]]
    target_address = BL2_PRIMARY_XIP_BASE + (spki_symbol - BL2_LOAD_BASE)
    require(
        bl2_entry["target_address"] == target_address,
        "contract target address disagrees with BL2 ELF",
    )
    spki = artifact_slice(
        payloads["bl2"], spki_symbol, BL2_LOAD_BASE, bl2_entry["length"],
        "BL2 MCUboot public key",
    )
    require(
        sha256_bytes(spki) == bl2_entry["expected_sha256"],
        "contract fingerprint disagrees with BL2 bytes",
    )
    encoded_length = artifact_slice(
        payloads["bl2"], bl2_symbols["ecdsa_pub_key_len"], BL2_LOAD_BASE, 4,
        "BL2 MCUboot public-key length",
    )
    require(
        int.from_bytes(encoded_length, "little") == bl2_entry["length"],
        "BL2 public-key length symbol disagrees with contract",
    )
    return {
        "bootloader_sha256": artifacts["bootloader"]["sha256"],
        "bl2_sha256": artifacts["bl2"]["sha256"],
        "status": "match",
    }


def command_lines(document: dict[str, object]) -> list[str]:
    anchors = document["anchors"]
    lines = []
    for name in ANCHOR_ORDER:
        entry = anchors[name]
        words = (entry["length"] + 3) // 4
        # J-Link Commander parses unprefixed numeric arguments as hexadecimal.
        addresses = [entry["target_address"],
                     *entry["compatible_target_addresses"]]
        for address in addresses:
            lines.append(f"mem32 0x{address:08x},{words:x}")
    lines.append("exit")
    return lines


def parse_jlink_memory(log: str) -> dict[int, bytes]:
    memory: dict[int, bytes] = {}
    for line in log.splitlines():
        match = MEMORY_LINE_RE.match(line)
        if match is None:
            continue
        address = int(match.group(1), 16)
        words = WORD_RE.findall(match.group(2))
        for index, word in enumerate(words):
            word_address = address + index * 4
            payload = int(word, 16).to_bytes(4, "little")
            previous = memory.get(word_address)
            require(
                previous is None or previous == payload,
                f"conflicting J-Link data at 0x{word_address:08x}",
            )
            memory[word_address] = payload
    return memory


def read_target(memory: dict[int, bytes], address: int, length: int) -> bytes:
    start = address & ~3
    end = (address + length + 3) & ~3
    missing = [item for item in range(start, end, 4) if item not in memory]
    if missing:
        raise TrustChainError(
            f"J-Link did not return target memory at 0x{missing[0]:08x}"
        )
    payload = b"".join(memory[item] for item in range(start, end, 4))
    offset = address - start
    return payload[offset:offset + length]


def verify_target(
    document: dict[str, object], jlink_log: str
) -> dict[str, object]:
    memory = parse_jlink_memory(jlink_log)
    require(memory, "J-Link log contains no readable target memory")
    results: dict[str, object] = {}
    anchors = document["anchors"]
    for name in ANCHOR_ORDER:
        entry = anchors[name]
        expected = entry["expected_sha256"]
        probes = [entry["target_address"],
                  *entry["compatible_target_addresses"]]
        observations = []
        matched_address = None
        observed = None
        for address in probes:
            try:
                target = read_target(memory, address, entry["length"])
            except TrustChainError as error:
                observations.append(f"0x{address:08x}=unreadable({error})")
                continue
            candidate = (
                target.hex()
                if entry["target_representation"] == "stored-sha256"
                else sha256_bytes(target)
            )
            observations.append(f"0x{address:08x}={candidate}")
            if candidate == expected:
                matched_address = address
                observed = candidate
                break
        require(
            matched_address is not None,
            f"{name} mismatch: expected={expected} "
            f"targets={';'.join(observations)}",
        )
        results[name] = {
            "status": "match",
            "target_address": matched_address,
            "probe_addresses": probes,
            "length": entry["length"],
            "sha256": observed,
        }
    return {
        "format": 1,
        "status": "pass",
        "kind": "bk7258-preflash-trust-result",
        "anchors": results,
        "writes_performed": False,
    }


def write_json(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main_emit(args: argparse.Namespace) -> int:
    try:
        document = emit_contract(args)
    except (TrustChainError, OSError, ValueError) as error:
        print(f"FAIL bk7258-trust-contract: {error}")
        return 1
    write_json(args.output, document)
    print(
        "PASS bk7258-trust-contract: "
        f"BL1={document['anchors'][BL1_XY_ANCHOR]['expected_sha256']} "
        f"BL2={document['anchors'][BL2_SPKI_ANCHOR]['expected_sha256']}"
    )
    return 0


def main_commands(args: argparse.Namespace) -> int:
    try:
        document = load_contract(args.contract)
    except TrustChainError as error:
        print(f"FAIL bk7258-trust-commands: {error}")
        return 1
    print("\n".join(command_lines(document)))
    return 0


def main_verify(args: argparse.Namespace) -> int:
    try:
        document = load_contract(args.contract)
        log = args.jlink_log.read_text(encoding="utf-8", errors="replace")
        result = verify_target(document, log)
    except (TrustChainError, OSError, ValueError) as error:
        result = {
            "format": 1,
            "status": "fail",
            "kind": "bk7258-preflash-trust-result",
            "reason": str(error),
            "writes_performed": False,
        }
        if args.json is not None:
            write_json(args.json, result)
        print(f"FAIL bk7258-trust-preflight: {error}")
        return 1
    if args.json is not None:
        write_json(args.json, result)
    print("PASS bk7258-trust-preflight: BL1/BL2 target fingerprints match")
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    emit = subparsers.add_parser("emit", help="emit a public-only contract")
    emit.add_argument("--bl1-manifest-key", type=Path, required=True)
    emit.add_argument("--mcuboot-signing-key", type=Path, required=True)
    emit.add_argument("--bootloader-elf", type=Path, required=True)
    emit.add_argument("--bootloader-bin", type=Path, required=True)
    emit.add_argument("--bl2-elf", type=Path, required=True)
    emit.add_argument("--bl2-bin", type=Path, required=True)
    emit.add_argument("--boot-xip-base", type=parse_uint32, default=0x02000000)
    emit.add_argument("--bl2-load-base", type=parse_uint32, required=True)
    emit.add_argument("--bl2-primary-xip-base", type=parse_uint32, required=True)
    emit.add_argument("--nm", default="arm-none-eabi-nm")
    emit.add_argument("--output", type=Path, required=True)
    emit.set_defaults(handler=main_emit)

    commands = subparsers.add_parser(
        "commands", help="emit non-halting J-Link memory-read commands"
    )
    commands.add_argument("--contract", type=Path, required=True)
    commands.set_defaults(handler=main_commands)

    verify = subparsers.add_parser(
        "verify", help="compare a J-Link memory log with the contract"
    )
    verify.add_argument("--contract", type=Path, required=True)
    verify.add_argument("--jlink-log", type=Path, required=True)
    verify.add_argument("--json", type=Path)
    verify.set_defaults(handler=main_verify)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
