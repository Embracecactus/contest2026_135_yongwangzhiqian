# SPDX-License-Identifier: Apache-2.0

"""Host regression for board-declared BK7258 product delivery ZIPs."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))

from _lib import build as build_domain  # noqa: E402
from _lib import image as image_domain  # noqa: E402
from _lib import layout as layout_domain  # noqa: E402
from _lib import package as package_domain  # noqa: E402
from _lib import product as product_domain  # noqa: E402


class ProductDeliveryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="bk7258-product-test-"
        )
        self.root = Path(self.temporary.name)
        layout_path = self.root / "layout.csv"
        layout_path.write_text(
            "# LAYOUT_NAME=bk7258-product-test\n"
            "# STORAGE_TOPOLOGY=fixed-block\n"
            "# ERASE_SIZE=4\n"
            "# CRC_DATA_SIZE=32\n"
            "# CRC_TOTAL_SIZE=32\n"
            "# XIP_BASE=0x02000000\n"
            "# Name,Offset,Size,Type,Read,Write,Artifact,Policy\n"
            "FLASH_CAPACITY=40\n"
            "boot,0,4,data,TRUE,FALSE,boot,image\n"
            "cp,,4,data,TRUE,FALSE,cp,image\n"
            "ap,,4,data,TRUE,FALSE,ap,image\n"
            "s_app,,8,data,TRUE,FALSE,pair,image\n"
            "usr_config,,4,data,TRUE,TRUE,,preserve\n"
            "reset_marker,,4,data,TRUE,TRUE,,preserve\n"
            "manifest,,4,data,TRUE,FALSE,manifest_a,external\n"
            "persistent_data,,4,data,TRUE,TRUE,,preserve\n"
            "sys_rf,,4,data,TRUE,TRUE,,immutable\n",
            encoding="utf-8",
        )
        policy_path = self.root / "release.csv"
        policy_path.write_text(
            "# SPDX-License-Identifier: Apache-2.0\n"
            "# FORMAT=bk7258.release-policy/1\n"
            "# FACTORY_MODE=provision-required\n"
            "# Partition,ReleasePolicy\n"
            "boot,replace\n"
            "cp,replace\n"
            "ap,replace\n"
            "s_app,replace\n"
            "usr_config,factory-init\n"
            "reset_marker,transactional\n"
            "manifest,replace\n"
            "persistent_data,factory-init\n"
            "sys_rf,device-unique\n",
            encoding="utf-8",
        )
        self.layout = layout_domain.load(layout_path)
        self.policy = product_domain.load_policy(policy_path, self.layout)
        artifacts = {
            "boot": b"BOOT",
            "cp": b"CP00",
            "ap": b"AP00",
            "pair": b"CP00AP00",
        }
        self.images = image_domain.finalized(
            self.layout, artifacts, preserved_external=("manifest_a",)
        )
        self.base = self.root / "device-base.bin"
        self.base.write_bytes(bytes(range(self.layout.flash_size)))
        self.base_sha256 = hashlib.sha256(self.base.read_bytes()).hexdigest()
        self.base_evidence_path = self.root / "accepted-base.json"
        product_domain.create_base_evidence(
            physical_board="test_board",
            layout=self.layout,
            base=self.base,
            device_id="test-unit:0001",
            capture_method="fixture-readback",
            output=self.base_evidence_path,
        )
        self.base_evidence = product_domain.load_base_evidence(
            self.base_evidence_path,
            {"board_family": "bk7258", "physical_board": "test_board"},
            self.layout,
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _inputs(self, stem: str) -> tuple[Path, Path]:
        package = self.root / f"{stem}.bkpack"
        package_domain.create(
            image_set=self.images,
            member_names={
                name: f"{name}.bin" for name in ("boot", "cp", "ap", "pair")
            },
            sdk_evidence={},
            trust_evidence={"mode": "unsigned"},
            physical_board="test_board",
            output=package,
        )
        finalized = {
            row.artifact: {
                "kind": "finalized-flash",
                "path": f"images/{row.artifact}.bin",
                "sha256": hashlib.sha256(row.data).hexdigest(),
                "size": len(row.data),
            }
            for row in self.images.writes
        }
        manifest = self.root / f"{stem}-build-manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "boot": "direct",
                    "finalized_flash": finalized,
                    "format": "bk7258.build-manifest/2",
                    "layout": {
                        "identity": self.layout.identity,
                        "sha256": self.layout.sha256,
                    },
                    "target": {
                        "board_family": "bk7258",
                        "physical_board": "test_board",
                    },
                },
                sort_keys=True,
                separators=(",", ":"),
            ) + "\n",
            encoding="utf-8",
        )
        return package, manifest

    def _delivery(self, stem: str) -> Path:
        package, manifest = self._inputs(stem)
        recovery = product_domain.materialize_recovery(
            package, self.policy, self.base, self.base_evidence
        )
        output = self.root / f"{stem}.zip"
        product_domain.create_delivery(
            package=package,
            build_manifest=manifest,
            policy=self.policy,
            recovery=recovery,
            base_evidence=self.base_evidence,
            version="1.2.3+4",
            output=output,
        )
        return output

    def test_delivery_is_deterministic_and_complete_flash(self) -> None:
        first = self._delivery("first")
        second = self._delivery("second")
        self.assertEqual(first.read_bytes(), second.read_bytes())
        report = product_domain.verify_delivery(first)
        self.assertEqual(report["physical_board"], "test_board")
        self.assertEqual(report["operator_size"], self.layout.flash_size)
        self.assertEqual(report["factory"], "requires-provisioning")
        self.assertEqual(report["ota"], "not-included")
        with zipfile.ZipFile(first) as archive:
            release = json.loads(archive.read("release.json"))
            self.assertIsNone(
                release["components"]["recovery"]["installed_root"]
            )
            self.assertEqual(
                release["components"]["recovery"]["accepted_base"]["device_id"],
                "test-unit:0001",
            )
            operator = archive.read(
                release["components"]["recovery"]["operator"]["path"]
            )
        expected = bytearray(self.base.read_bytes())
        expected[0:20] = b"BOOTCP00AP00CP00AP00"
        expected[24:28] = b"\xff" * 4
        self.assertEqual(operator, bytes(expected))

    def test_copied_build_manifest_evidence_is_path_independent(self) -> None:
        package, manifest = self._inputs("portable-evidence")
        evidence = self.root / "detached-release/evidence/build-manifest.json"
        evidence.parent.mkdir(parents=True)
        evidence.write_bytes(manifest.read_bytes())

        document = product_domain.validate_build_manifest_evidence(
            evidence, package
        )

        self.assertEqual(document["boot"], "direct")
        self.assertEqual(
            document["target"],
            {"board_family": "bk7258", "physical_board": "test_board"},
        )

    def test_delivery_rejects_changed_operator(self) -> None:
        valid = self._delivery("valid")
        corrupt = self.root / "corrupt.zip"
        with zipfile.ZipFile(valid, "r") as source, \
                zipfile.ZipFile(corrupt, "w", allowZip64=False) as target:
            for info in source.infolist():
                data = source.read(info)
                if info.filename.endswith("full-flash.bin"):
                    data = data[:-1] + bytes([data[-1] ^ 0xff])
                target.writestr(info, data)
        with self.assertRaises(product_domain.ProductError):
            product_domain.verify_delivery(corrupt)

    def test_delivery_never_overwrites_existing_output(self) -> None:
        package, manifest = self._inputs("source")
        recovery = product_domain.materialize_recovery(
            package, self.policy, self.base, self.base_evidence
        )
        output = self.root / "delivery.zip"
        product_domain.create_delivery(
            package=package,
            build_manifest=manifest,
            policy=self.policy,
            recovery=recovery,
            base_evidence=self.base_evidence,
            version="1.2.3+4",
            output=output,
        )
        accepted = output.read_bytes()
        with self.assertRaises(product_domain.ProductError):
            product_domain.create_delivery(
                package=package,
                build_manifest=manifest,
                policy=self.policy,
                recovery=recovery,
                base_evidence=self.base_evidence,
                version="1.2.3+4",
                output=output,
            )
        self.assertEqual(output.read_bytes(), accepted)

    def test_release_directory_publish_never_replaces_existing_output(self) -> None:
        staging = self.root / "staging-release"
        staging.mkdir()
        (staging / "release.json").write_text("candidate\n", encoding="utf-8")
        output = self.root / "published-release"
        output.mkdir()
        (output / "owner.txt").write_text("existing\n", encoding="utf-8")

        with self.assertRaises(package_domain.PackageError):
            package_domain.publish_directory_no_replace(
                staging, output, "test release"
            )

        self.assertEqual(
            (output / "owner.txt").read_text(encoding="utf-8"),
            "existing\n",
        )
        self.assertTrue(staging.is_dir())

        fresh_output = self.root / "fresh-release"
        package_domain.publish_directory_no_replace(
            staging, fresh_output, "test release"
        )
        self.assertFalse(staging.exists())
        self.assertEqual(
            (fresh_output / "release.json").read_text(encoding="utf-8"),
            "candidate\n",
        )

    def test_recovery_requires_exact_complete_device_base(self) -> None:
        package, _ = self._inputs("source")
        changed = self.root / "changed.bin"
        changed.write_bytes(self.base.read_bytes()[:-1] + b"\x00")
        with self.assertRaises(product_domain.ProductError):
            product_domain.materialize_recovery(
                package, self.policy, changed, self.base_evidence
            )
        short = self.root / "short.bin"
        short.write_bytes(self.base.read_bytes()[:-1])
        with self.assertRaises(product_domain.ProductError):
            product_domain.materialize_recovery(
                package,
                self.policy,
                short,
                self.base_evidence,
            )

    def test_base_evidence_rejects_another_physical_board(self) -> None:
        with self.assertRaises(product_domain.ProductError):
            product_domain.load_base_evidence(
                self.base_evidence_path,
                {"board_family": "bk7258", "physical_board": "other_board"},
                self.layout,
            )

    def test_all_board_presets_resolve_complete_release_policy(self) -> None:
        for board in ("aidk_ai_toy", "t5_board", "t5ai_core"):
            with self.subTest(board=board):
                preset = build_domain.board_preset(REPOSITORY, board)
                layout = layout_domain.load(preset.partition)
                policy = product_domain.load_policy(
                    preset.release_policy, layout
                )
                self.assertEqual(
                    set(policy.by_partition),
                    {row.name for row in layout.partitions},
                )
                self.assertEqual(layout.flash_size, 8 * 1024 * 1024)

    def test_fourth_board_and_non_eight_mib_layout_are_descriptor_only(self) -> None:
        repository = self.root / "synthetic_team"
        board = "future_board"
        version = "v-test"
        for role in ("cp", "ap"):
            (repository / "boards/bk7258" / board / "configs" /
             f"openvela_{role}").mkdir(parents=True)
        (repository / "chips/bk7258/bk_idk/sdk-profiles" / version).mkdir(
            parents=True
        )
        (repository / f"{repository.name}.xml").write_text(
            "<manifest><project path=\"sdk\" name=\"sdk\" "
            "groups=\"bk7258-sdk\" revision=\"" + "0" * 40 + "\" "
            "upstream=\"refs/tags/" + version + "\"/></manifest>\n",
            encoding="utf-8",
        )
        profile_hash = "0" * 64
        for role in ("cp", "ap"):
            config = repository / "boards/bk7258" / board / "configs" / \
                f"openvela_{role}"
            (config / "defconfig").write_text(
                "CONFIG_ARCH_CHIP_BK7258=y\n", encoding="utf-8"
            )
            (config / "profile.conf").write_text(
                "BK7258_PROFILE_SCHEMA=1\n"
                f"BK7258_PROFILE_BOARD={board}\n"
                f"BK7258_PROFILE_ROLE={role}\n"
                "BK7258_PROFILE_CLASS=runnable\n"
                "BK7258_PROFILE_COMPAT=future_board_v1\n"
                f"BK7258_PROFILE_SDK={role}\n",
                encoding="utf-8",
            )
            (repository / "chips/bk7258/bk_idk/sdk-profiles" / version /
             f"{role}.config").write_text(
                f"# BK7258_BUNDLE_TREE_SHA256={profile_hash}\n",
                encoding="utf-8",
            )

        layout_path = repository / "boards/bk7258" / board / "layout.csv"
        layout_path.write_text(
            "# LAYOUT_NAME=bk7258-future-board\n"
            "# STORAGE_TOPOLOGY=fixed-block\n"
            "# ERASE_SIZE=4K\n"
            "# CRC_DATA_SIZE=32\n"
            "# CRC_TOTAL_SIZE=32\n"
            "# XIP_BASE=0x02000000\n"
            "# Name,Offset,Size,Type,Read,Write,Artifact,Policy\n"
            "FLASH_CAPACITY=16M\n"
            "primary_bootloader,0,1M,code,TRUE,FALSE,boot,image\n"
            "primary_cp_app,,2M,code,TRUE,FALSE,cp,image\n"
            "primary_ap_app,,2M,code,TRUE,FALSE,ap,image\n"
            "s_app,,4M,data,TRUE,FALSE,pair,image\n"
            "usr_config,,1M,data,TRUE,TRUE,,preserve\n"
            "reset_marker,,4K,data,TRUE,TRUE,,clear\n"
            "primary_manifest,,4K,data,TRUE,FALSE,manifest_a,external\n"
            "secondary_manifest,,4K,data,TRUE,FALSE,manifest_b,external\n"
            "primary_bl2,,1M,code,TRUE,FALSE,bl2_a,external\n"
            "secondary_bl2,,1M,code,TRUE,FALSE,bl2_b,external\n"
            "persistent_data,0xe00000,1M,data,TRUE,TRUE,,preserve\n"
            "sys_rf,0xf00000,1M,data,TRUE,TRUE,,immutable\n",
            encoding="utf-8",
        )
        policy_path = repository / "boards/bk7258" / board / "release.csv"
        policy_path.write_text(
            "# FORMAT=bk7258.release-policy/1\n"
            "# FACTORY_MODE=provision-required\n"
            "primary_bootloader,replace\n"
            "primary_cp_app,replace\n"
            "primary_ap_app,replace\n"
            "s_app,replace\n"
            "usr_config,factory-init\n"
            "reset_marker,transactional\n"
            "primary_manifest,replace\n"
            "secondary_manifest,replace\n"
            "primary_bl2,replace\n"
            "secondary_bl2,replace\n"
            "persistent_data,factory-init\n"
            "sys_rf,device-unique\n",
            encoding="utf-8",
        )
        (repository / "boards/bk7258" / board / "openvela.conf").write_text(
            "BK7258_BOARD_SCHEMA=1\n"
            f"BK7258_BOARD_NAME={board}\n"
            f"BK7258_BOARD_CP_CONFIG=boards/bk7258/{board}/configs/openvela_cp\n"
            f"BK7258_BOARD_AP_CONFIG=boards/bk7258/{board}/configs/openvela_ap\n"
            f"BK7258_BOARD_PARTITION=boards/bk7258/{board}/layout.csv\n"
            f"BK7258_BOARD_RELEASE_POLICY=boards/bk7258/{board}/release.csv\n",
            encoding="utf-8",
        )

        preset = build_domain.board_preset(repository, board)
        layout = layout_domain.load(preset.partition)
        policy = product_domain.load_policy(preset.release_policy, layout)
        self.assertEqual(preset.board, board)
        self.assertEqual(layout.flash_size, 16 * 1024 * 1024)
        self.assertEqual(
            set(policy.by_partition),
            {row.name for row in layout.partitions},
        )

    def test_aidk_recovery_is_eight_megabytes_and_preserves_device_tail(self) -> None:
        preset = build_domain.board_preset(REPOSITORY, "aidk_ai_toy")
        layout = layout_domain.load(preset.partition)
        policy = product_domain.load_policy(preset.release_policy, layout)
        cp = image_domain.crc_encode(b"C" * 32)
        ap = image_domain.crc_encode(b"A" * 32)
        boot = image_domain.crc_encode(b"B" * 32)
        pair = (
            cp.ljust(layout.artifact("cp").size, b"\xff")
            + ap.ljust(layout.artifact("ap").size, b"\xff")
        )
        images = image_domain.finalized(
            layout,
            {"boot": boot, "cp": cp, "ap": ap, "pair": pair},
            preserved_external=("bl2_a", "bl2_b", "manifest_a", "manifest_b"),
        )
        package = self.root / "aidk.bkpack"
        package_domain.create(
            image_set=images,
            member_names={
                name: f"{name}.bin" for name in ("boot", "cp", "ap", "pair")
            },
            sdk_evidence={},
            trust_evidence={"mode": "unsigned"},
            physical_board="aidk_ai_toy",
            output=package,
        )
        base = self.root / "aidk-base.bin"
        base.write_bytes(bytes([0x5a]) * layout.flash_size)
        evidence_path = self.root / "aidk-accepted-base.json"
        product_domain.create_base_evidence(
            physical_board="aidk_ai_toy",
            layout=layout,
            base=base,
            device_id="aidk-test-unit:0001",
            capture_method="fixture-readback",
            output=evidence_path,
        )
        evidence = product_domain.load_base_evidence(
            evidence_path,
            {"board_family": "bk7258", "physical_board": "aidk_ai_toy"},
            layout,
        )
        recovery = product_domain.materialize_recovery(
            package,
            policy,
            base,
            evidence,
        )
        base_data = base.read_bytes()
        self.assertEqual(len(recovery.data), 0x800000)
        self.assertEqual(recovery.data[0x7FA000:], base_data[0x7FA000:])
        self.assertEqual(recovery.data[0x50A000:0x50B000], b"\xff" * 0x1000)


if __name__ == "__main__":
    unittest.main()
