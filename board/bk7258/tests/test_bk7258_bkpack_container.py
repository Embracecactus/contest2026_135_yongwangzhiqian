#!/usr/bin/env python3
"""Focused host-only tests for the payload-bearing BK7258 container."""

from __future__ import annotations

import json
import stat
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock


SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

import bk7258_bkpack as bkpack  # noqa: E402


def segment(name: str, file_name: str, offset: int, payload: bytes) -> dict[str, object]:
    return {
        "name": name,
        "file": file_name,
        "physical_offset": offset,
        "length": len(payload),
        "physical_end": offset + len(payload),
        "sha256": bkpack._sha256(payload),
        "bkfil": f"{file_name}@0x{offset:x}-0x{len(payload):x}",
    }


class BkpackContainerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bkpack-test-")
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        payloads = {
            "bl_crc.bin": b"B" * 16,
            "app_crc_flash.bin": b"C" * 16,
            "app1_crc_flash.bin": b"A" * 16,
            "bl2_crc.bin": b"2" * 16,
            "bl2_secondary_crc.bin": b"2" * 16,
            "all-app-factory.bin": b"F" * 32,
            "littlefs_factory_clear.bin": b"\xff" * 16,
            "s_app_mcuboot.bin": b"S" * 32,
            "app.bin": b"c-raw",
            "app1.bin": b"a-raw",
            "app_crc.bin": b"c-crc",
            "app1_crc.bin": b"a-crc",
            "bootloader.bin": b"bl1",
            "bl2.bin": b"bl2",
            "cp-raw.bin": b"cp-standard",
            "ap-raw.bin": b"ap-standard",
            "bootloader.elf": b"elf-bl1",
            "bl2.elf": b"elf-bl2",
            "bk7258-trust-chain.json": b"{}",
        }
        payloads.update({
            "vela_nuttx_cp.bin": payloads["cp-raw.bin"],
            "vela_nuttx_ap.bin": payloads["ap-raw.bin"],
        })
        payloads[bkpack.STANDARD_MANIFEST] = json.dumps({
            "schema": "openvela.nuttx-artifacts/1",
            "version": 1,
            "dual_core": True,
            "generic_alias": None,
            "roles": {
                "cp": {
                    "source_file": "cp-raw.bin",
                    "alias": "vela_nuttx_cp.bin",
                    "sha256": bkpack._sha256(payloads["cp-raw.bin"]),
                    "size": len(payloads["cp-raw.bin"]),
                    "byte_exact": True,
                },
                "ap": {
                    "source_file": "ap-raw.bin",
                    "alias": "vela_nuttx_ap.bin",
                    "sha256": bkpack._sha256(payloads["ap-raw.bin"]),
                    "size": len(payloads["ap-raw.bin"]),
                    "byte_exact": True,
                },
            },
        }, sort_keys=True, separators=(",", ":")).encode()
        for name, payload in payloads.items():
            (self.source / name).write_bytes(payload)

        # The source root represents the explicit resolved-product contract.
        # Keep the fixture independent from the verifier's module default by
        # staging the CSV under the package's fixed member name.
        partition_payload = (
            SCRIPT_ROOT.parent / "partitions/bk7258/auto_partitions.csv"
        ).read_bytes()
        (self.source / bkpack.PARTITION_LAYOUT_MEMBER).write_bytes(
            partition_payload
        )
        partition_layout = bkpack._layout_from_payload(partition_payload)
        self.layout_id = partition_layout.layout_id
        self.layout_sha256 = partition_layout.layout_sha256
        self.flash_size = partition_layout.flash_size

        boot = segment("primary_bootloader", "bl_crc.bin", 0x0000, payloads["bl_crc.bin"])
        cp = segment("primary_cp_app", "app_crc_flash.bin", 0x1000, payloads["app_crc_flash.bin"])
        ap = segment("primary_ap_app", "app1_crc_flash.bin", 0x2000, payloads["app1_crc_flash.bin"])
        bl2 = segment("primary_bl2", "bl2_crc.bin", 0x4000, payloads["bl2_crc.bin"])
        bl2b = segment("secondary_bl2", "bl2_secondary_crc.bin", 0x5000,
                       payloads["bl2_secondary_crc.bin"])
        prefix = segment("factory_prefix", "all-app-factory.bin", 0x0000,
                         payloads["all-app-factory.bin"])
        clear = segment("littlefs_clear", "littlefs_factory_clear.bin", 0x3000,
                        payloads["littlefs_factory_clear.bin"])
        standard = {
            alias: {
                "file": alias,
                "source_file": source,
                "length": len(payloads[alias]),
                "sha256": bkpack._sha256(payloads[alias]),
                "source_sha256": bkpack._sha256(payloads[source]),
                "byte_exact": True,
            }
            for alias, source in bkpack.STANDARD_ALIASES.items()
        }
        dual = {
            "format": 2,
            "layout_id": self.layout_id,
            "layout_sha256": self.layout_sha256,
            "layout": {
                "layout_id": self.layout_id,
                "layout_sha256": self.layout_sha256,
                "layout_source": "board/bk7258/partitions/test/layout.csv",
                "flash_size": self.flash_size,
            },
            "writes_enabled": False,
            "segments": [boot, cp, ap],
            "bl2_segments": [bl2, bl2b],
            "migration_segments": [prefix, clear],
            "secondary_pair": {"file": "s_app_mcuboot.bin"},
            "raw_images": {
                "cp": {"file": "app.bin"}, "ap": {"file": "app1.bin"},
            },
            "crc_images": {
                "cp": {"file": "app_crc.bin"}, "ap": {"file": "app1_crc.bin"},
            },
            "trust_chain": {
                "file": "bk7258-trust-chain.json",
                "preflash_target_match_required": True,
            },
            "standard_artifacts": {
                "status": "generated", "version": 1, "artifacts": standard,
            },
            "normal_update": {
                "arguments": [boot["bkfil"], cp["bkfil"], ap["bkfil"],
                              bl2["bkfil"], bl2b["bkfil"]],
            },
            "factory_image": {
                "requires_explicit_owner_gate": True,
                "loader_arguments": [prefix["bkfil"], clear["bkfil"],
                                     bl2["bkfil"], bl2b["bkfil"]],
            },
        }
        (self.source / bkpack.DUAL_MANIFEST).write_text(
            json.dumps(dual), encoding="utf-8"
        )
        (self.source / bkpack.BUILD_PROFILE).write_text(
            "MCUBOOT_PROFILE=true\n"
            "TRUST_CHAIN_PREFLIGHT_REQUIRED=true\n"
            "BL1_MANIFEST_RAW_PAGE=false\n"
            f"PARTITION_LAYOUT_ID={self.layout_id}\n"
            f"PARTITION_LAYOUT_SHA256={self.layout_sha256}\n"
            "PARTITION_LAYOUT_SOURCE=board/bk7258/partitions/test/layout.csv\n"
            f"PARTITION_LAYOUT_SOURCE_SHA256={bkpack._sha256(partition_payload)}\n"
            "PHYSICAL_BOARD=t5_board\n"
            "CP_CONFIG_NAME=cp_test\n"
            "AP_CONFIG_NAME=ap_test\n",
            encoding="utf-8",
        )
        self.verify_patch = mock.patch.object(bkpack, "_deep_verify_directory")
        self.deep_verify = self.verify_patch.start()

    def tearDown(self) -> None:
        self.verify_patch.stop()
        self.temporary.cleanup()

    def _rewrite(
        self, source: Path, destination: Path, *, change: dict[str, bytes] | None = None,
        extra: tuple[zipfile.ZipInfo, bytes] | None = None,
    ) -> None:
        change = change or {}
        with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination, "w") as output:
            for entry in original.infolist():
                output.writestr(bkpack._zip_info(entry.filename),
                                change.get(entry.filename, original.read(entry)))
            if extra is not None:
                output.writestr(extra[0], extra[1])

    def test_deterministic_create_verify_and_atomic_materialize(self) -> None:
        first = self.root / "first.bkpack"
        second = self.root / "second.bkpack"
        bkpack.create(self.source, first)
        bkpack.create(self.source, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        result = bkpack.verify(first)
        self.assertFalse(result["authenticated"])
        self.assertTrue(result["target_preflight_required"])
        self.assertEqual(result["plans"], ["apps", "factory", "normal"])
        self.assertEqual(result["layout_id"], self.layout_id)
        self.assertEqual(result["layout_sha256"], self.layout_sha256)
        manifest = result["manifest"]
        self.assertEqual(manifest["source"]["layout_source"],
                         "board/bk7258/partitions/test/layout.csv")
        self.assertTrue(all(
            plan["layout_id"] == self.layout_id and
            plan["layout_sha256"] == self.layout_sha256 and
            plan["flash_size"] == self.flash_size
            for plan in manifest["plans"].values()))
        contract = bkpack.flash_contract(first, self.source)
        self.assertTrue(contract["source_verified"])
        self.assertEqual(contract["layout"], {
            "layout_id": self.layout_id,
            "layout_sha256": self.layout_sha256,
            "layout_source": "board/bk7258/partitions/test/layout.csv",
            "flash_size": self.flash_size,
        })
        self.assertEqual(manifest["partition_layout"]["member"],
                         bkpack.PARTITION_LAYOUT_MEMBER)
        self.assertEqual(
            manifest["partition_layout"]["source_sha256"],
            bkpack._sha256((self.source / bkpack.PARTITION_LAYOUT_MEMBER).read_bytes()),
        )
        standard = set(manifest["standard_artifacts"])
        plan_members = {
            item["member"]
            for plan in manifest["plans"].values()
            for item in plan["segments"]
        }
        self.assertEqual(standard, {"vela_nuttx_cp.bin", "vela_nuttx_ap.bin"})
        self.assertTrue(standard.isdisjoint(plan_members))
        self.assertNotIn("app.bin", standard)
        self.assertNotIn("app1.bin", standard)
        factory = result["manifest"]["plans"]["factory"]["segments"]
        self.assertEqual({item["name"] for item in factory}, {
            "factory_prefix", "littlefs_clear", "primary_bl2", "secondary_bl2",
        })
        output = self.root / "expanded"
        self.assertEqual(bkpack.materialize(first, output), output)
        self.assertEqual((output / "vela_nuttx_cp.bin").read_bytes(),
                         (output / "cp-raw.bin").read_bytes())
        guide = (output / bkpack.WINDOWS_FLASH_GUIDE).read_text(encoding="utf-8")
        self.assertIn("<PORT_NUMBER>", guide)
        self.assertIn("for COM9, use -p 9", guide)
        self.assertIn("app_crc_flash.bin@0x1000-0x10", guide)
        self.assertIn("Partition SHA-256: " + self.layout_sha256, guide)
        self.assertIn("Do not flash vela_nuttx_cp.bin", guide)
        self.assertTrue((output / bkpack.STANDARD_MANIFEST).is_file())
        self.assertTrue((output / bkpack.MANIFEST_MEMBER).is_file())

    def test_raw_manifest_pages_are_rejected(self) -> None:
        profile = self.source / bkpack.BUILD_PROFILE
        profile.write_text(
            profile.read_text(encoding="utf-8").replace(
                "BL1_MANIFEST_RAW_PAGE=false",
                "BL1_MANIFEST_RAW_PAGE=true",
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(bkpack.BkpackError, "raw BL1 manifest"):
            bkpack.create(self.source, self.root / "unsupported.bkpack")

    def test_layout_binding_and_flash_ranges_fail_closed(self) -> None:
        profile = self.source / bkpack.BUILD_PROFILE
        profile.write_text(
            profile.read_text(encoding="utf-8").replace(
                "PARTITION_LAYOUT_SHA256=" + self.layout_sha256,
                "PARTITION_LAYOUT_SHA256=" + "2" * 64,
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(bkpack.BkpackError,
                                    "build profile and dual manifest layout SHA-256 disagree"):
            bkpack.create(self.source, self.root / "layout-drift.bkpack")

        profile.write_text(
            profile.read_text(encoding="utf-8").replace(
                "PARTITION_LAYOUT_SHA256=" + "2" * 64,
                "PARTITION_LAYOUT_SHA256=" + self.layout_sha256,
            ),
            encoding="utf-8",
        )
        dual_path = self.source / bkpack.DUAL_MANIFEST
        dual = json.loads(dual_path.read_text(encoding="utf-8"))
        ap = next(item for item in dual["segments"]
                  if item["name"] == "primary_ap_app")
        old_bkfil = ap["bkfil"]
        ap["physical_offset"] = self.flash_size
        ap["physical_end"] = self.flash_size + ap["length"]
        ap["bkfil"] = f"{ap['file']}@0x{self.flash_size:x}-0x{ap['length']:x}"
        dual["normal_update"]["arguments"] = [
            ap["bkfil"] if item == old_bkfil else item
            for item in dual["normal_update"]["arguments"]
        ]
        dual_path.write_text(json.dumps(dual), encoding="utf-8")
        with self.assertRaisesRegex(bkpack.BkpackError,
                                    "range exceeds the bound partition layout"):
            bkpack.create(self.source, self.root / "range-drift.bkpack")

    def test_flash_contract_rejects_materialized_source_drift(self) -> None:
        package = self.root / "good.bkpack"
        bkpack.create(self.source, package)
        (self.source / "app_crc_flash.bin").write_bytes(b"changed")
        with self.assertRaisesRegex(
                bkpack.BkpackError,
                "Flash source plan member differs from verified container"):
            bkpack.flash_contract(package, self.source)

    def test_flash_contract_is_package_self_contained(self) -> None:
        package = self.root / "good.bkpack"
        bkpack.create(self.source, package)
        with mock.patch.object(bkpack, "REPOSITORY_ROOT", self.root / "missing"):
            contract = bkpack.flash_contract(package)
        self.assertTrue(contract["package_layout_verified"])
        self.assertIsNone(contract["source"])

        (self.source / bkpack.PARTITION_LAYOUT_MEMBER).unlink()
        materialized_contract = bkpack.flash_contract(package, self.source)
        self.assertTrue(materialized_contract["source_verified"])
        self.assertEqual(materialized_contract["source"],
                         str(self.source.resolve()))

    def test_embedded_partition_member_tamper_and_missing_fail_closed(self) -> None:
        good = self.root / "good.bkpack"
        bkpack.create(self.source, good)

        tampered = self.root / "tampered-layout.bkpack"
        self._rewrite(
            good,
            tampered,
            change={bkpack.PARTITION_LAYOUT_MEMBER: b"alternate CSV"},
        )
        with self.assertRaisesRegex(bkpack.BkpackError, "hash/size mismatch"):
            bkpack.verify(tampered)

        missing = self.root / "missing-layout.bkpack"
        with zipfile.ZipFile(good) as original, zipfile.ZipFile(missing, "w") as output:
            for entry in original.infolist():
                if entry.filename != bkpack.PARTITION_LAYOUT_MEMBER:
                    output.writestr(bkpack._zip_info(entry.filename), original.read(entry))
        with self.assertRaisesRegex(bkpack.BkpackError, "missing/extra"):
            bkpack.verify(missing)

        alternate = (
            SCRIPT_ROOT.parent / "partitions/bk7258/secureboot_xip_cp_ap.csv"
        ).read_bytes()
        (self.source / bkpack.PARTITION_LAYOUT_MEMBER).write_bytes(alternate)
        with self.assertRaisesRegex(bkpack.BkpackError, "Flash source member"):
            bkpack.flash_contract(good, self.source)

    def test_tamper_duplicate_extra_and_compression_are_rejected(self) -> None:
        good = self.root / "good.bkpack"
        bkpack.create(self.source, good)

        tampered = self.root / "tampered.bkpack"
        self._rewrite(good, tampered, change={"vela_nuttx_cp.bin": b"changed"})
        with self.assertRaisesRegex(bkpack.BkpackError, "hash/size mismatch"):
            bkpack.verify(tampered)

        duplicate = self.root / "duplicate.bkpack"
        with zipfile.ZipFile(good) as original, zipfile.ZipFile(duplicate, "w") as output:
            for entry in original.infolist():
                output.writestr(bkpack._zip_info(entry.filename), original.read(entry))
            output.writestr(bkpack._zip_info("vela_nuttx_cp.bin"), b"again")
        with self.assertRaisesRegex(bkpack.BkpackError, "duplicate"):
            bkpack.verify(duplicate)

        extra = self.root / "extra.bkpack"
        self._rewrite(good, extra, extra=(bkpack._zip_info("surprise.bin"), b"x"))
        with self.assertRaisesRegex(
            bkpack.BkpackError, "(missing/extra|deterministic order)"
        ):
            bkpack.verify(extra)

        compressed = self.root / "compressed.bkpack"
        info = zipfile.ZipInfo("surprise.bin", bkpack.ZIP_TIMESTAMP)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3
        info.external_attr = (stat.S_IFREG | 0o644) << 16
        self._rewrite(good, compressed, extra=(info, b"compressed"))
        with self.assertRaisesRegex(bkpack.BkpackError, "stored regular"):
            bkpack.verify(compressed)

    def test_traversal_symlink_and_size_limit_are_rejected(self) -> None:
        good = self.root / "good.bkpack"
        bkpack.create(self.source, good)

        traversal = self.root / "traversal.bkpack"
        info = bkpack._zip_info("../escape")
        self._rewrite(good, traversal, extra=(info, b"bad"))
        with self.assertRaisesRegex(bkpack.BkpackError, "unsafe"):
            bkpack.verify(traversal)

        symlink = self.root / "symlink.bkpack"
        info = zipfile.ZipInfo("link", bkpack.ZIP_TIMESTAMP)
        info.compress_type = zipfile.ZIP_STORED
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        self._rewrite(good, symlink, extra=(info, b"target"))
        with self.assertRaisesRegex(bkpack.BkpackError, "not regular"):
            bkpack.verify(symlink)

        with mock.patch.object(bkpack, "MAX_MEMBER_SIZE", 1):
            with self.assertRaisesRegex(bkpack.BkpackError, "size is invalid"):
                bkpack.verify(good)


if __name__ == "__main__":
    unittest.main(verbosity=2)
