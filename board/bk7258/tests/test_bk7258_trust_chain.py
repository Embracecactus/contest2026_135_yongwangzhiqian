#!/usr/bin/env python3
"""Host tests for the fail-closed BK7258 pre-flash trust gate."""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT_DIR = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIR))

from bk7258_trust_chain import (  # noqa: E402
    ANCHOR_ORDER,
    BL1_SEC1_ANCHOR,
    BL1_XY_ANCHOR,
    BL2_SPKI_ANCHOR,
    EXPECTED_SYMBOLS,
    FIXED_TARGET_ADDRESSES,
    LEGACY_TARGET_ADDRESSES,
    TrustChainError,
    command_lines,
    load_p256_private_key,
    validate_contract_document,
    verify_target,
)
from pack_dual_image import (  # noqa: E402
    STANDARD_ALIAS_SOURCES,
    materialize_standard_artifacts,
    stage_trust_bundle,
    verify_standard_artifacts,
)


def artifact(file_name: str) -> dict[str, object]:
    payload = file_name.encode("ascii")
    return {
        "file": file_name,
        "length": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }


def contract() -> tuple[dict[str, object], dict[str, bytes]]:
    xy_hash = bytes(range(32))
    sec1_hash = bytes(range(32, 64))
    spki = bytes((index * 7 + 3) & 0xFF for index in range(91))
    payloads = {
        BL1_XY_ANCHOR: xy_hash,
        BL1_SEC1_ANCHOR: sec1_hash,
        BL2_SPKI_ANCHOR: spki,
    }
    anchors: dict[str, object] = {}
    for name in ANCHOR_ORDER:
        stored_hash = name != BL2_SPKI_ANCHOR
        anchors[name] = {
            "algorithm": "sha256",
            "symbol": EXPECTED_SYMBOLS[name],
            "target_address": FIXED_TARGET_ADDRESSES[name],
            "compatible_target_addresses": [LEGACY_TARGET_ADDRESSES[name]],
            "length": len(payloads[name]),
            "expected_sha256": (
                payloads[name].hex()
                if stored_hash else hashlib.sha256(payloads[name]).hexdigest()
            ),
            "target_representation": (
                "stored-sha256"
                if stored_hash else "der-subject-public-key-info"
            ),
        }
    document = {
        "format": 1,
        "kind": "bk7258-preflash-trust-chain",
        "policy": {
            "preflash_target_match_required": True,
            "mismatch_action": "refuse-flash",
            "normal_download_may_rotate_roots": False,
        },
        "anchors": anchors,
        "artifacts": {
            "bootloader": artifact("bootloader.bin"),
            "bl2": artifact("bl2.bin"),
        },
    }
    return validate_contract_document(document), payloads


def jlink_log(
    document: dict[str, object], payloads: dict[str, bytes],
    omit: str | None = None,
) -> str:
    lines = ["SEGGER J-Link Commander"]
    for name in ANCHOR_ORDER:
        if name == omit:
            continue
        payload = payloads[name]
        payload += b"\x00" * (-len(payload) % 4)
        words = [
            f"{int.from_bytes(payload[offset:offset + 4], 'little'):08X}"
            for offset in range(0, len(payload), 4)
        ]
        entry = document["anchors"][name]
        addresses = [entry["target_address"],
                     *entry["compatible_target_addresses"]]
        for base in addresses:
            # Exercise wrapped output as well as a single-line response.
            for offset in range(0, len(words), 8):
                address = base + offset * 4
                prefix = "J-Link>" if offset == 0 else ""
                lines.append(
                    f"{prefix}{address:08X} = "
                    f"{' '.join(words[offset:offset + 8])}"
                )
    return "\n".join(lines) + "\n"


class TrustChainTest(unittest.TestCase):
    def test_matching_target_passes(self) -> None:
        document, payloads = contract()
        result = verify_target(document, jlink_log(document, payloads))
        self.assertEqual(result["status"], "pass")
        self.assertFalse(result["writes_performed"])
        self.assertEqual(set(result["anchors"]), set(ANCHOR_ORDER))

    def test_wrong_bl2_key_fails(self) -> None:
        document, payloads = contract()
        damaged = dict(payloads)
        damaged[BL2_SPKI_ANCHOR] = bytes([payloads[BL2_SPKI_ANCHOR][0] ^ 1]) \
            + payloads[BL2_SPKI_ANCHOR][1:]
        with self.assertRaisesRegex(TrustChainError, "bl2_mcuboot.*mismatch"):
            verify_target(document, jlink_log(document, damaged))

    def test_wrong_bl1_root_fails(self) -> None:
        document, payloads = contract()
        damaged = dict(payloads)
        damaged[BL1_XY_ANCHOR] = payloads[BL1_XY_ANCHOR][:-1] + b"\xff"
        with self.assertRaisesRegex(TrustChainError, "bl1_manifest.*mismatch"):
            verify_target(document, jlink_log(document, damaged))

    def test_missing_memory_read_fails(self) -> None:
        document, payloads = contract()
        with self.assertRaisesRegex(TrustChainError, "mismatch.*unreadable"):
            verify_target(
                document,
                jlink_log(document, payloads, omit=BL1_SEC1_ANCHOR),
            )

    def test_commands_are_non_halting_reads_only(self) -> None:
        document, _ = contract()
        self.assertEqual(
            command_lines(document),
            [
                "mem32 0x0200fd40,8",
                "mem32 0x02002774,8",
                "mem32 0x0200fd60,8",
                "mem32 0x02002754,8",
                "mem32 0x024d2f00,17",
                "mem32 0x024d27ec,17",
                "exit",
            ],
        )

    def test_contract_cannot_authorize_root_rotation(self) -> None:
        document, _ = contract()
        document["policy"]["normal_download_may_rotate_roots"] = True
        with self.assertRaisesRegex(TrustChainError, "must not rotate"):
            validate_contract_document(document)

    def test_private_key_error_does_not_disclose_path(self) -> None:
        private_path = Path("/tmp/do-not-disclose-private-key.pem")
        with self.assertRaises(TrustChainError) as context:
            load_p256_private_key(private_path)
        self.assertNotIn(str(private_path), str(context.exception))

    def test_clean_package_stages_and_revalidates_trust_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            output = root / "output"
            source.mkdir()
            output.mkdir()
            files = (
                "bootloader.bin",
                "bootloader.elf",
                "bl2.bin",
                "bl2.elf",
                "bk7258-trust-chain.json",
            )
            for index, name in enumerate(files):
                (source / name).write_bytes(bytes([index + 1]) * (index + 2))
            (output / "bootloader.bin").write_bytes(b"stale")

            verified: list[Path] = []

            def record_verify(_document: object, package: Path) -> None:
                verified.append(package)

            contract_path = source / "bk7258-trust-chain.json"
            with patch(
                "pack_dual_image.verify_contract_artifacts",
                side_effect=record_verify,
            ):
                staged = stage_trust_bundle({}, contract_path, output)

            self.assertEqual(verified, [source, output])
            self.assertEqual(staged, output / "bk7258-trust-chain.json")
            for name in files:
                self.assertEqual((output / name).read_bytes(),
                                 (source / name).read_bytes())

    def test_standard_aliases_are_byte_exact_and_manifest_bound(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for index, source_name in enumerate(STANDARD_ALIAS_SOURCES.values()):
                (output / source_name).write_bytes(
                    bytes([index + 17]) * (index + 3)
                )

            document = materialize_standard_artifacts(output)
            verify_standard_artifacts(output, document)
            for alias, source_name in STANDARD_ALIAS_SOURCES.items():
                self.assertEqual(
                    (output / alias).read_bytes(),
                    (output / source_name).read_bytes(),
                )

            (output / "vela_nuttx_cp.bin").write_bytes(b"tampered")
            with self.assertRaisesRegex(TrustChainError, "(length drift|hash gate)"):
                verify_standard_artifacts(output, document)

if __name__ == "__main__":
    unittest.main()
