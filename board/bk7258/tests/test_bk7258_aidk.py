#!/usr/bin/env python3
"""Focused host-only checks for the schematic-only AIDK AI Toy binding."""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


TOOLS_ROOT = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
sys.path.insert(0, str(TOOLS_ROOT))
SCRIPT_ROOT = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_ROOT))

from bk7258_framework import (  # noqa: E402
    build_plan,
    load_catalog,
    load_json,
    validate_sdk_lock,
    validate_sdk_registry,
    validate_sdk_set,
)
from bk7258_resource_graph import (  # noqa: E402
    resolve_resource_graph,
    validate_resource_graph,
)
from materialize_product_profiles import materialize_plan  # noqa: E402
from gen_bk7258_partitions import load_layout  # noqa: E402

BOOTLOADER_ROOT = TOOLS_ROOT.parent.parent / "board" / "bk7258" / "bootloader"
sys.path.insert(0, str(BOOTLOADER_ROOT))
from bk7258_bl1_pack import bl2_contract_from_layout  # noqa: E402


REPOSITORY = Path(__file__).resolve().parents[3]


class AidkBoardTest(unittest.TestCase):
    def test_partition_headers_are_product_and_role_private(self) -> None:
        board = REPOSITORY / "board/bk7258"
        generator = board / "scripts/gen_bk7258_partitions.py"
        verifier = TOOLS_ROOT / "verify_bk7258_partitions.py"
        source = board / "partitions/bk7258/auto_partitions.csv"
        tracked_header = board / "include/bk7258_partition_layout.h"
        tracked_before = tracked_header.read_bytes()

        with tempfile.TemporaryDirectory(
                prefix="bk7258-private-partitions-") as directory:
            root = Path(directory)
            alternate = root / "alternate.csv"
            alternate.write_text(
                source.read_text(encoding="utf-8").replace(
                    "primary_cp_app,,1360K", "primary_cp_app,,1292K", 1
                ).replace("s_app,,2516K", "s_app,,2448K", 1),
                encoding="utf-8",
            )
            layouts = {
                "product-a": (source, load_layout(source)),
                "product-b": (alternate, load_layout(alternate)),
            }
            self.assertNotEqual(
                layouts["product-a"][1].layout_id,
                layouts["product-b"][1].layout_id,
            )

            def generate(product: str, role: str) -> tuple[Path, Path]:
                input_path, layout = layouts[product]
                contract = root / product / role
                header = (
                    contract /
                    "include/arch/board/bk7258_partition_layout.h"
                )
                output_dir = contract / "generated"
                common = [
                    sys.executable, str(generator),
                    "--input", str(input_path),
                    "--expect-layout-id", layout.layout_id,
                    "--expect-layout-sha256", layout.layout_sha256,
                    "--header", str(header),
                    "--output-dir", str(output_dir),
                ]
                generated = subprocess.run(
                    common, cwd=REPOSITORY, text=True,
                    capture_output=True, check=False,
                )
                self.assertEqual(generated.returncode, 0, generated.stderr)
                checked = subprocess.run(
                    common + ["--check"], cwd=REPOSITORY, text=True,
                    capture_output=True, check=False,
                )
                self.assertEqual(checked.returncode, 0, checked.stderr)
                return header, output_dir

            requests = [
                (product, role)
                for product in layouts
                for role in ("cp", "ap")
            ]
            with ThreadPoolExecutor(max_workers=4) as executor:
                generated = dict(zip(
                    requests,
                    executor.map(lambda request: generate(*request), requests),
                ))

            for (product, role), (header, output_dir) in generated.items():
                layout = layouts[product][1]
                header_text = header.read_text(encoding="utf-8")
                self.assertIn(layout.layout_id, header_text)
                report = json.loads((
                    output_dir / "bk7258_partition_layout.json"
                ).read_text(encoding="utf-8"))
                self.assertEqual(report["layout_id"], layout.layout_id)
                self.assertEqual(
                    Path(report["source"]).resolve(), layouts[product][0].resolve()
                )
                self.assertIn(role, str(header))

                preprocessed = subprocess.run(
                    [
                        "cc", "-E", "-P", "-x", "c",
                        "-DBK7258_FLASH_XIP_BASE=0x02000000u",
                        "-DBK7258_FLASH_CRC_DATA_SIZE=32u",
                        "-DBK7258_FLASH_CRC_TOTAL_SIZE=34u",
                        "-I", str(header.parents[2]),
                        "-I", str(REPOSITORY.parent / "nuttx/include"),
                        "-",
                    ],
                    input=(
                        "#include <arch/board/bk7258_partition_layout.h>\n"
                        "const char *layout_id = BK7258_PARTITION_LAYOUT_ID;\n"
                    ),
                    text=True, capture_output=True, check=False,
                )
                self.assertEqual(
                    preprocessed.returncode, 0, preprocessed.stderr)
                self.assertIn(f'"{layout.layout_id}"', preprocessed.stdout)

            selected_header, selected_output = generated[("product-a", "cp")]
            baseline = layouts["product-a"][1]
            verified = subprocess.run(
                [
                    sys.executable, str(verifier),
                    "--input", str(source),
                    "--expect-layout-id", baseline.layout_id,
                    "--expect-layout-sha256", baseline.layout_sha256,
                    "--header", str(selected_header),
                    "--output-dir", str(selected_output),
                ],
                cwd=REPOSITORY, text=True, capture_output=True, check=False,
            )
            self.assertEqual(verified.returncode, 0, verified.stdout)

        self.assertEqual(tracked_header.read_bytes(), tracked_before)

    def test_partition_contract_is_required_atomically_by_build_consumers(
            self) -> None:
        board = REPOSITORY / "board/bk7258"
        source = board / "partitions/bk7258/auto_partitions.csv"
        layout = load_layout(source)
        base_env = dict(os.environ)
        for name in (
            "BK7258_PARTITION_CONTRACT_ROOT",
            "BK7258_PARTITION_LAYOUT_SOURCE",
            "BK7258_PARTITION_LAYOUT_ID",
            "BK7258_PARTITION_LAYOUT_SHA256",
        ):
            base_env.pop(name, None)

        with tempfile.TemporaryDirectory(
                prefix="bk7258-postbuild-contract-") as directory:
            missing_root = subprocess.run(
                [
                    str(board / "scripts/postbuild.sh"),
                    directory, str(board), "cp",
                ],
                env={
                    **base_env,
                    "BK7258_PARTITION_LAYOUT_SOURCE": str(source),
                    "BK7258_PARTITION_LAYOUT_ID": layout.layout_id,
                    "BK7258_PARTITION_LAYOUT_SHA256": layout.layout_sha256,
                },
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(missing_root.returncode, 3)
            self.assertIn(
                "contract root, source, ID and SHA-256 must be supplied together",
                missing_root.stderr,
            )

            incomplete_root = subprocess.run(
                [
                    str(board / "scripts/postbuild.sh"),
                    directory, str(board), "cp",
                ],
                env={
                    **base_env,
                    "BK7258_PARTITION_CONTRACT_ROOT": str(
                        Path(directory) / "missing-contract"),
                    "BK7258_PARTITION_LAYOUT_SOURCE": str(source),
                    "BK7258_PARTITION_LAYOUT_ID": layout.layout_id,
                    "BK7258_PARTITION_LAYOUT_SHA256": layout.layout_sha256,
                },
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(incomplete_root.returncode, 3)
            self.assertIn("incomplete private partition contract",
                          incomplete_root.stderr)

        build_dual = (TOOLS_ROOT / "build_dual_image.sh").read_text()
        make_defs = (board / "scripts/Make.defs").read_text()
        cmake = (board / "CMakeLists.txt").read_text()
        self.assertIn("materialize_partition_contract", build_dual)
        self.assertIn('build_config "${CP_CONFIG}" cp', build_dual)
        self.assertIn('build_config "${AP_CONFIG}" ap', build_dual)
        self.assertIn("PARTITION_CONTRACT_ROOT=$(partition_contract_root bl1)",
                      build_dual)
        self.assertIn("PARTITION_CONTRACT_ROOT=$(partition_contract_root bl2)",
                      build_dual)
        self.assertIn("BK7258_PARTITION_MATERIALIZED", make_defs)
        self.assertIn("legacy-$(BK7258_PARTITION_LAYOUT_SHA256)", make_defs)
        self.assertIn("NUTTX_INCLUDE_DIRECTORIES", cmake)
        self.assertIn("BK7258_PARTITION_ENV_COUNT EQUAL 0", cmake)
        self.assertIn("bk7258-${BK7258_PARTITION_ROLE}-link.ld", cmake)
        self.assertIn("BK7258_PARTITION_CONTRACT_ROOT=${BK7258_PARTITION_CONTRACT_ROOT}",
                      cmake)

    def test_isolated_postbuild_requires_private_contract_and_artifacts(self) -> None:
        board = REPOSITORY / "board/bk7258"
        script = board / "scripts/postbuild.sh"
        generator = board / "scripts/gen_bk7258_partitions.py"
        source = board / "partitions/bk7258/auto_partitions.csv"
        layout = load_layout(source)
        base_env = dict(os.environ)
        for name in (
            "BK7258_POSTBUILD_MODE",
            "BK7258_POSTBUILD_ARTIFACT_ROOT",
            "BK7258_POSTBUILD_DUAL_ROLE",
            "BK7258_BL1_CRC_BIN",
            "BK7258_PARTITION_CONTRACT_ROOT",
            "BK7258_PARTITION_LAYOUT_SOURCE",
            "BK7258_PARTITION_LAYOUT_ID",
            "BK7258_PARTITION_LAYOUT_SHA256",
        ):
            base_env.pop(name, None)

        with tempfile.TemporaryDirectory(prefix="bk7258-isolated-postbuild-") as directory:
            root = Path(directory)
            topdir = root / "cp"
            artifact_root = topdir / "artifacts"
            contract = topdir / "partition-contract"
            artifact_root.mkdir(parents=True)
            contract.mkdir(parents=True)
            config = topdir / ".config"
            config.write_text("# isolated CP role\n", encoding="utf-8")

            generated = subprocess.run(
                [
                    sys.executable, str(generator),
                    "--input", str(source),
                    "--expect-layout-id", layout.layout_id,
                    "--expect-layout-sha256", layout.layout_sha256,
                    "--header", str(
                        contract / "include/arch/board/bk7258_partition_layout.h"
                    ),
                    "--output-dir", str(contract / "generated"),
                ],
                cwd=REPOSITORY, text=True, capture_output=True, check=False,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)

            xip = int(subprocess.check_output(
                [
                    sys.executable, str(generator),
                    "--input", str(source),
                    "--get", "slot_a_cp.xip_start",
                ],
                cwd=REPOSITORY, text=True,
            ).strip(), 0)
            image = bytearray(0x108)
            struct.pack_into("<II", image, 0, 0x28030000, xip + 0x40 | 1)
            image[0x100:0x108] = b"BK7236\0\0"
            (topdir / "nuttx.bin").write_bytes(image)

            bl1 = root / "bl1" / "artifacts" / "bl_crc.bin"
            bl1.parent.mkdir(parents=True)
            # The isolated contract owns this input; do not consume the
            # source-tree compatibility bootloader artifact in a host test.
            bl1.write_bytes(b"fake-bl1-crc-artifact\n")

            isolated_env = {
                **base_env,
                "BK7258_POSTBUILD_MODE": "isolated",
                "BK7258_POSTBUILD_ARTIFACT_ROOT": str(artifact_root),
                "BK7258_BL1_CRC_BIN": str(bl1),
                "BK7258_PARTITION_CONTRACT_ROOT": str(contract),
                "BK7258_PARTITION_LAYOUT_SOURCE": str(source),
                "BK7258_PARTITION_LAYOUT_ID": layout.layout_id,
                "BK7258_PARTITION_LAYOUT_SHA256": layout.layout_sha256,
            }
            complete = subprocess.run(
                [str(script), str(topdir), str(board), "cp"],
                cwd=REPOSITORY, env=isolated_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(complete.returncode, 0, complete.stderr)
            self.assertIn("mode=isolated", complete.stdout)
            self.assertIn("artifact_root=", complete.stdout)
            for name in (
                "app.bin", "app_crc.bin", "app_crc.bin.json",
                "nuttx_crc.bin", "all-app.bin",
            ):
                self.assertTrue((artifact_root / name).is_file(), name)
            self.assertEqual(
                (artifact_root / "vela_nuttx.bin").read_bytes(),
                (artifact_root / "app.bin").read_bytes(),
            )
            self.assertEqual(
                (artifact_root / "vela_nuttx_cp.bin").read_bytes(),
                (artifact_root / "app.bin").read_bytes(),
            )
            cp_standard = json.loads(
                (artifact_root / "vela_nuttx_cp.json").read_text(encoding="utf-8")
            )
            self.assertEqual(cp_standard["role"], "cp")
            self.assertEqual(cp_standard["source_file"], "app.bin")
            self.assertEqual(cp_standard["generic_alias"], "vela_nuttx.bin")
            self.assertFalse((topdir / "app.bin").exists())
            self.assertTrue(
                (artifact_root / "all-app.bin").read_bytes().startswith(bl1.read_bytes())
            )

            missing_bl1_env = dict(isolated_env)
            missing_bl1_env.pop("BK7258_BL1_CRC_BIN")
            missing_bl1 = subprocess.run(
                [str(script), str(topdir), str(board), "cp"],
                cwd=REPOSITORY, env=missing_bl1_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(missing_bl1.returncode, 6)
            self.assertIn("requires explicit BK7258_BL1_CRC_BIN",
                          missing_bl1.stderr)

            linked_source = root / "linked-partitions.csv"
            linked_source.symlink_to(source)
            symlink_env = dict(isolated_env)
            symlink_env["BK7258_PARTITION_LAYOUT_SOURCE"] = str(linked_source)
            symlink_source = subprocess.run(
                [str(script), str(topdir), str(board), "cp"],
                cwd=REPOSITORY, env=symlink_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(symlink_source.returncode, 3)
            self.assertIn("partition source must not be a symlink",
                          symlink_source.stderr)

            source_tree_env = dict(isolated_env)
            source_tree_env["BK7258_POSTBUILD_ARTIFACT_ROOT"] = str(
                board / "partitions"
            )
            source_tree_output = subprocess.run(
                [str(script), str(topdir), str(board), "cp"],
                cwd=REPOSITORY, env=source_tree_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(source_tree_output.returncode, 3)
            self.assertIn("artifact root must be outside BOARD_DIR",
                          source_tree_output.stderr)

            fallback_env = dict(isolated_env)
            fallback_env["BK7258_BL1_CRC_BIN"] = str(
                board / "bootloader/bl_crc.bin"
            )
            fallback = subprocess.run(
                [str(script), str(topdir), str(board), "cp"],
                cwd=REPOSITORY, env=fallback_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(fallback.returncode, 6)
            self.assertIn("cannot use the legacy board bootloader", fallback.stderr)

            ap_topdir = root / "ap"
            ap_artifact_root = ap_topdir / "artifacts"
            ap_contract = ap_topdir / "partition-contract"
            ap_artifact_root.mkdir(parents=True)
            ap_contract.mkdir(parents=True)
            ap_generated = subprocess.run(
                [
                    sys.executable, str(generator),
                    "--input", str(source),
                    "--expect-layout-id", layout.layout_id,
                    "--expect-layout-sha256", layout.layout_sha256,
                    "--header", str(
                        ap_contract / "include/arch/board/bk7258_partition_layout.h"
                    ),
                    "--output-dir", str(ap_contract / "generated"),
                ],
                cwd=REPOSITORY, text=True, capture_output=True, check=False,
            )
            self.assertEqual(ap_generated.returncode, 0, ap_generated.stderr)
            ap_xip = int(subprocess.check_output(
                [
                    sys.executable, str(generator),
                    "--input", str(source),
                    "--get", "slot_a_ap.xip_start",
                ],
                cwd=REPOSITORY, text=True,
            ).strip(), 0)
            ap_image = bytearray(8)
            struct.pack_into("<II", ap_image, 0, 0x28030000, ap_xip + 0x4 | 1)
            (ap_topdir / ".config").write_text("# isolated AP role\n", encoding="utf-8")
            (ap_topdir / "nuttx.bin").write_bytes(ap_image)
            not_read = root / "bl1-not-read"
            not_read.symlink_to(root / "missing-bl1")
            ap_env = dict(isolated_env)
            ap_env.update({
                "BK7258_POSTBUILD_ARTIFACT_ROOT": str(ap_artifact_root),
                "BK7258_BL1_CRC_BIN": str(not_read),
                "BK7258_PARTITION_CONTRACT_ROOT": str(ap_contract),
            })
            ap_result = subprocess.run(
                [str(script), str(ap_topdir), str(board), "ap"],
                cwd=REPOSITORY, env=ap_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(ap_result.returncode, 0, ap_result.stderr)
            self.assertTrue((ap_artifact_root / "app1.bin").is_file())
            self.assertTrue((ap_artifact_root / "app1_crc.bin").is_file())
            self.assertEqual(
                (ap_artifact_root / "vela_nuttx.bin").read_bytes(),
                (ap_artifact_root / "app1.bin").read_bytes(),
            )
            self.assertEqual(
                (ap_artifact_root / "vela_nuttx_ap.bin").read_bytes(),
                (ap_artifact_root / "app1.bin").read_bytes(),
            )
            self.assertFalse((ap_artifact_root / "all-app.bin").exists())
            self.assertFalse((ap_artifact_root / "nuttx_crc.bin").exists())

            dual_ap_env = dict(ap_env)
            dual_ap_env["BK7258_POSTBUILD_DUAL_ROLE"] = "1"
            dual_ap_result = subprocess.run(
                [str(script), str(ap_topdir), str(board), "ap"],
                cwd=REPOSITORY, env=dual_ap_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(dual_ap_result.returncode, 0, dual_ap_result.stderr)
            self.assertFalse((ap_artifact_root / "vela_nuttx.bin").exists())
            dual_ap_standard = json.loads(
                (ap_artifact_root / "vela_nuttx_ap.json").read_text(encoding="utf-8")
            )
            self.assertTrue(dual_ap_standard["dual_core"])
            self.assertIsNone(dual_ap_standard["generic_alias"])

            bl2_topdir = root / "bl2" / "cp"
            bl2_artifact_root = bl2_topdir / "artifacts"
            bl2_contract = bl2_topdir / "partition-contract"
            bl2_artifact_root.mkdir(parents=True)
            bl2_contract.mkdir(parents=True)
            bl2_generated = subprocess.run(
                [
                    sys.executable, str(generator),
                    "--input", str(source),
                    "--expect-layout-id", layout.layout_id,
                    "--expect-layout-sha256", layout.layout_sha256,
                    "--header", str(
                        bl2_contract / "include/arch/board/bk7258_partition_layout.h"
                    ),
                    "--output-dir", str(bl2_contract / "generated"),
                ],
                cwd=REPOSITORY, text=True, capture_output=True, check=False,
            )
            self.assertEqual(bl2_generated.returncode, 0, bl2_generated.stderr)
            bl2_image = bytearray(0x108)
            struct.pack_into(
                "<II", bl2_image, 0, 0x28030000, 0x28020000 + 0x4 | 1
            )
            bl2_image[0x100:0x108] = b"BK7236\0\0"
            (bl2_topdir / ".config").write_text(
                "CONFIG_BK7258_BL2_IMAGE=y\n", encoding="utf-8"
            )
            (bl2_topdir / "nuttx.bin").write_bytes(bl2_image)
            bl2_env = dict(isolated_env)
            bl2_env.update({
                "BK7258_POSTBUILD_ARTIFACT_ROOT": str(bl2_artifact_root),
                "BK7258_BL1_CRC_BIN": str(not_read),
                "BK7258_PARTITION_CONTRACT_ROOT": str(bl2_contract),
            })
            bl2_result = subprocess.run(
                [str(script), str(bl2_topdir), str(board), "cp"],
                cwd=REPOSITORY, env=bl2_env,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(bl2_result.returncode, 0, bl2_result.stderr)
            self.assertTrue((bl2_artifact_root / "app.bin").is_file())
            self.assertTrue((bl2_artifact_root / "app_crc.bin").is_file())
            self.assertFalse((bl2_artifact_root / "all-app.bin").exists())
            self.assertFalse((bl2_artifact_root / "nuttx_crc.bin").exists())

        postbuild = script.read_text(encoding="utf-8")
        self.assertIn(
            'BL_CRC_BIN="${BOARD_DIR}/bootloader/bl_crc.bin"', postbuild
        )
        self.assertIn("postbuild.sh: mode=legacy (compatibility)", postbuild)

    def test_bl1_manifest_rechecks_resolved_partition_identity(self) -> None:
        source = REPOSITORY / "board/bk7258/partitions/bk7258/auto_partitions.csv"
        layout = load_layout(source)
        script = REPOSITORY / "board/bk7258/bootloader/bk7258_bl1_pack.py"
        primary = bl2_contract_from_layout(
            source, "primary", layout.layout_id, layout.layout_sha256)
        secondary = bl2_contract_from_layout(
            source, "secondary", layout.layout_id, layout.layout_sha256)
        self.assertEqual(primary, (0x024D0000, 0x20000))
        self.assertEqual(secondary, (0x024F0000, 0x20000))
        staging_source = (
            REPOSITORY /
            "board/bk7258/partitions/bk7258/secureboot_xip_cp_ap.csv"
        )
        staging = load_layout(staging_source)
        self.assertEqual(
            bl2_contract_from_layout(
                staging_source, "primary", staging.layout_id,
                staging.layout_sha256),
            (0x02004C00, 0x1FF40),
        )
        self.assertEqual(
            bl2_contract_from_layout(
                staging_source, "secondary", staging.layout_id,
                staging.layout_sha256),
            (0x02024C00, 0x1FF40),
        )
        common = [
            sys.executable,
            str(script),
            "manifest",
            "--bl2", "missing-bl2.bin",
            "--private-key", "missing-key.pem",
            "--out", "unused-manifest.bin",
            "--partition-csv", str(source),
        ]

        wrong_sha = subprocess.run(
            common + [
                "--expect-layout-id", layout.layout_id,
                "--expect-layout-sha256", "0" * 64,
            ],
            cwd=REPOSITORY,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(wrong_sha.returncode, 0)
        self.assertIn("partition layout identity mismatch", wrong_sha.stderr)

        incomplete = subprocess.run(
            common + ["--expect-layout-id", layout.layout_id],
            cwd=REPOSITORY,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(incomplete.returncode, 0)
        self.assertIn("must be supplied together", incomplete.stderr)

        wrong_xip = subprocess.run(
            common + [
                "--expect-layout-id", layout.layout_id,
                "--expect-layout-sha256", layout.layout_sha256,
                "--bl2-xip", "0x024d0200",
            ],
            cwd=REPOSITORY,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(wrong_xip.returncode, 0)
        self.assertIn("disagrees with primary layout address", wrong_xip.stderr)

        wrong_capacity = subprocess.run(
            common + [
                "--expect-layout-id", layout.layout_id,
                "--expect-layout-sha256", layout.layout_sha256,
                "--bl2-capacity", "0x10000",
            ],
            cwd=REPOSITORY,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(wrong_capacity.returncode, 0)
        self.assertIn("disagrees with primary layout capacity",
                      wrong_capacity.stderr)

        secureboot_packer = (
            REPOSITORY / "tools/bk7258/pack_bk7258_secureboot.py"
        )
        staging_mismatch = subprocess.run(
            [
                sys.executable, str(secureboot_packer),
                "--cp-raw", "missing-cp.bin",
                "--ap-raw", "missing-ap.bin",
                "--key", "missing-key.pem",
                "--imgtool", "missing-imgtool.py",
                "--output", "unused-secureboot-output",
                "--version", "0.0.0",
                "--partition-csv", str(staging_source),
                "--expect-layout-id", staging.layout_id,
                "--expect-layout-sha256", "0" * 64,
            ],
            cwd=REPOSITORY,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(staging_mismatch.returncode, 0)
        self.assertIn("secureboot staging layout identity mismatch",
                      staging_mismatch.stderr)

    def test_partition_contract_is_atomic_and_source_report_is_dynamic(self) -> None:
        source = REPOSITORY / "board/bk7258/partitions/bk7258/auto_partitions.csv"
        with tempfile.TemporaryDirectory(prefix="bk7258-aidk-part-") as directory:
            config_root = Path(directory) / "final-configs"
            config_root.mkdir()
            (config_root / "cp.config").write_text(
                "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y\n"
                "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
                "# CONFIG_BK7258_AP_CORE is not set\n",
                encoding="utf-8")
            (config_root / "ap.config").write_text(
                "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y\n"
                "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
                "CONFIG_BK7258_AP_CORE=y\n",
                encoding="utf-8")
            plan = build_plan(REPOSITORY, "aidk_ai_toy_bringup",
                              config_root=config_root)
        expected = plan["partition_layout"]
        base_env = dict(os.environ)
        for name in (
            "BK7258_PARTITION_LAYOUT_SOURCE",
            "BK7258_PARTITION_LAYOUT_ID",
            "BK7258_PARTITION_LAYOUT_SHA256",
        ):
            base_env.pop(name, None)

        incomplete = subprocess.run(
            [sys.executable, "-c", "import bk7258_ab_layout"],
            cwd=TOOLS_ROOT,
            env={**base_env, "BK7258_PARTITION_LAYOUT_SOURCE": str(source)},
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(incomplete.returncode, 0)
        self.assertIn("must be exported together", incomplete.stderr)

        complete_env = {
            **base_env,
            "BK7258_PARTITION_LAYOUT_SOURCE": str(source),
            "BK7258_PARTITION_LAYOUT_ID": expected["layout_id"],
            "BK7258_PARTITION_LAYOUT_SHA256": expected["layout_sha256"],
        }
        accepted = subprocess.run(
            [sys.executable, "-c",
             "import bk7258_ab_layout as layout; print(layout.LAYOUT_ID)"],
            cwd=TOOLS_ROOT,
            env=complete_env,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertEqual(accepted.stdout.strip(), expected["layout_id"])

        rejected = subprocess.run(
            [sys.executable, "-c", "import bk7258_ab_layout"],
            cwd=TOOLS_ROOT,
            env={**complete_env, "BK7258_PARTITION_LAYOUT_SHA256": "0" * 64},
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn("identity mismatch", rejected.stderr)

        with tempfile.TemporaryDirectory(prefix="bk7258-layout-source-") as directory:
            alternate = Path(directory) / "selected.csv"
            alternate.write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
            self.assertEqual(load_layout(alternate).report()["source"], str(alternate))

    def test_product_selects_shared_mcuboot_ab_and_temp_profiles_have_no_devices(
            self) -> None:
        product = load_catalog(REPOSITORY)["products"]["aidk_ai_toy_bringup"]
        self.assertEqual(product["boot"], "mcuboot")
        self.assertEqual(product["roles"]["cp"], {"legacy_profile": None})
        self.assertEqual(product["roles"]["ap"], {"legacy_profile": None})
        self.assertFalse(list(TOOLS_ROOT.glob("bk7258_fragment_catalog_*.json")),
                         "fragment catalogs are retired")
        script = (TOOLS_ROOT / "build_dual_image.sh").read_text(encoding="utf-8")
        self.assertIn("materialize_product_profiles.py", script)
        self.assertIn("product plan/profile boot mismatch", script)
        self.assertIn("bk7258-product:${PRODUCT_ID}:${role}", script)
        self.assertIn("build-plan", script)
        self.assertIn("PARTITION_LAYOUT_SHA256=${PARTITION_LAYOUT_SHA256}", script)
        self.assertIn("SECUREBOOT_STAGING_LAYOUT_ACTIVE=false", script)
        self.assertIn("product package metadata contains temporary profile paths",
                      script)

        with tempfile.TemporaryDirectory(prefix="bk7258-aidk-profiles-") as directory:
            work_root = Path(directory)
            final_configs = work_root / "final-configs"
            final_configs.mkdir()
            (final_configs / "cp.config").write_text(
                "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y\n"
                "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
                "# CONFIG_BK7258_AP_CORE is not set\n",
                encoding="utf-8")
            (final_configs / "ap.config").write_text(
                "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y\n"
                "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
                "CONFIG_BK7258_AP_CORE=y\n",
                encoding="utf-8")
            result = subprocess.run(
                [str(TOOLS_ROOT / "build_dual_image.sh")],
                env={**os.environ,
                     "BK7258_PRODUCT": "aidk_ai_toy_bringup",
                     "BK7258_PROFILE_CHECK_ONLY": "YES",
                     "BK7258_CONFIG_ROOT": str(final_configs)},
                text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("boot=mcuboot", result.stdout)

            config_root = work_root / "configs"
            make_defs = REPOSITORY / "board/bk7258/scripts/Make.defs"
            plan = build_plan(REPOSITORY, "aidk_ai_toy_bringup",
                              config_root=final_configs)
            materialize_plan(
                plan, REPOSITORY / "board/bk7258/configs", config_root, make_defs
            )
            generated_make_defs = work_root / "scripts/Make.defs"
            self.assertTrue(generated_make_defs.is_symlink())
            self.assertEqual(generated_make_defs.resolve(), make_defs.resolve())
            for role in ("cp", "ap"):
                profile = config_root / f"aidk_ai_toy_{role}_mcuboot"
                metadata = (profile / "profile.conf").read_text(encoding="utf-8")
                defconfig = (profile / "defconfig").read_text(encoding="utf-8")
                self.assertIn("BK7258_PROFILE_BOOT=mcuboot", metadata)
                self.assertIn("BK7258_PROFILE_BOARD=aidk_ai_toy", metadata)
                self.assertIn("BK7258_PROFILE_COMPAT=seed-config-v1", metadata)
                self.assertIn("CONFIG_BK7258_BOARD_AIDK_AI_TOY=y", defconfig)
                self.assertIn("CONFIG_BK7258_MCUBOOT_IMAGE=y", defconfig)
                self.assertNotRegex(defconfig, r"CONFIG_BK7258_(MIC|AUD|LCD|DVP)=y")
                self.assertNotIn("legacy-fal", defconfig.lower())
                if role == "cp":
                    self.assertIn("# CONFIG_BK7258_AP_CORE is not set", defconfig)

    def test_classic_selector_allows_only_unresolved_configure_goals(self) -> None:
        root = REPOSITORY / "board/bk7258"
        with tempfile.TemporaryDirectory(prefix="bk7258-selector-") as directory:
            topdir = Path(directory) / "nuttx"
            (topdir / "tools").mkdir(parents=True)
            (topdir / "arch/arm/src/armv8-m").mkdir(parents=True)
            (topdir / ".config").write_text("", encoding="utf-8")
            (topdir / "tools/Config.mk").write_text("", encoding="utf-8")
            (topdir / "arch/arm/src/armv8-m/Toolchain.defs").write_text(
                "", encoding="utf-8")
            harness = Path(directory) / "selector.mk"
            harness.write_text(
                f"TOPDIR := {topdir}\n"
                f"BOARD_DIR := {root}\n"
                "DELIM := /\n"
                f"include {root / 'scripts/Make.defs'}\n"
                ".PHONY: all olddefconfig print-contract\n"
                "all olddefconfig:\n\t@:\n"
                "print-contract:\n"
                "\t@printf '%s\\n' "
                "'$(BK7258_PARTITION_CONTRACT_INCLUDE)' "
                "'$(BK7258_PARTITION_CONTRACT_HEADER_DIR)' "
                "'$(BK7258_PARTITION_CONTRACT_HEADER)'\n",
                encoding="utf-8")

            contract = Path(directory) / "partition-contract"
            private_header = (
                contract / "include/arch/board/bk7258_partition_layout.h"
            )
            private_header.parent.mkdir(parents=True)
            source = root / "partitions/bk7258/auto_partitions.csv"
            layout = load_layout(source)
            generated = subprocess.run(
                [
                    sys.executable,
                    str(root / "scripts/gen_bk7258_partitions.py"),
                    "--input", str(source),
                    "--expect-layout-id", layout.layout_id,
                    "--expect-layout-sha256", layout.layout_sha256,
                    "--header", str(private_header),
                    "--output-dir", str(contract / "generated"),
                ],
                text=True, capture_output=True, check=False)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            contract_vars = (
                f"BK7258_PARTITION_CONTRACT_ROOT={contract}",
                f"BK7258_PARTITION_LAYOUT_SOURCE={source}",
                f"BK7258_PARTITION_LAYOUT_ID={layout.layout_id}",
                f"BK7258_PARTITION_LAYOUT_SHA256={layout.layout_sha256}",
            )

            def run(goal: str, *selectors: str) -> subprocess.CompletedProcess[str]:
                return subprocess.run(
                    ["make", "-s", "-f", str(harness), goal, *selectors],
                    text=True, capture_output=True, check=False)

            self.assertEqual(run("olddefconfig").returncode, 0)
            self.assertNotEqual(run("all").returncode, 0)
            self.assertEqual(
                run("all", "CONFIG_BK7258_BOARD_T5AI_CORE=y",
                    *contract_vars).returncode, 0)
            self.assertNotEqual(
                run("olddefconfig", "CONFIG_BK7258_BOARD_T5AI_CORE=y",
                    "CONFIG_BK7258_BOARD_T5_BOARD=y").returncode,
                0)

            override_attempt = run(
                "print-contract",
                "CONFIG_BK7258_BOARD_T5AI_CORE=y",
                *contract_vars,
                f"BK7258_PARTITION_CONTRACT_INCLUDE={root / 'include'}",
                f"BK7258_PARTITION_CONTRACT_HEADER_DIR={root / 'include'}",
                "BK7258_PARTITION_CONTRACT_HEADER="
                f"{root / 'include/bk7258_partition_layout.h'}",
            )
            self.assertEqual(
                override_attempt.returncode, 0, override_attempt.stderr)
            self.assertEqual(
                override_attempt.stdout.splitlines(),
                [
                    str(contract / "include"),
                    str(contract / "include/arch/board"),
                    str(private_header),
                ],
            )

            private_header.write_text(
                private_header.read_text(encoding="utf-8").replace(
                    layout.layout_id, "stale-layout", 1
                ),
                encoding="utf-8",
            )
            stale_header = run(
                "all", "CONFIG_BK7258_BOARD_T5AI_CORE=y", *contract_vars)
            self.assertNotEqual(stale_header.returncode, 0)
            self.assertIn(
                "Explicit BK7258 partition contract validation failed",
                stale_header.stderr,
            )

    def test_catalog_and_sdk_lock_are_board_product_mode_specific(self) -> None:
        catalog = load_catalog(REPOSITORY)
        board = catalog["boards"]["aidk_ai_toy"]
        product = catalog["products"]["aidk_ai_toy_bringup"]
        self.assertNotIn("bindings", board,
                         "console/debug Kconfig facts are owned by .config")
        self.assertEqual(product["board"], "aidk_ai_toy")
        self.assertEqual(product["mode"], "bringup")
        registry_path = REPOSITORY / "tools/bk7258/bk7258_sdk_registry.json"
        registry = load_json(registry_path)
        sdk_set_path = REPOSITORY / product["sdk_set"]
        sdk_lock_path = REPOSITORY / product["sdk_lock"]
        sdk_set = load_json(sdk_set_path)
        sdk_lock = load_json(sdk_lock_path)
        self.assertIs(validate_sdk_registry(REPOSITORY, registry), registry)
        self.assertIs(validate_sdk_set(sdk_set, registry), sdk_set)
        self.assertIs(validate_sdk_lock(
            REPOSITORY, registry_path, sdk_set_path, sdk_lock, registry, sdk_set), sdk_lock)

    def test_resource_graph_resolves_exactly_one_aidk_plan(self) -> None:
        graph_path = TOOLS_ROOT / "bk7258_resource_graph_aidk_ai_toy.json"
        graph = load_json(graph_path)
        self.assertIs(validate_resource_graph(REPOSITORY, graph), graph)
        with tempfile.TemporaryDirectory(prefix="bk7258-aidk-graph-") as directory:
            config_root = Path(directory) / "configs"
            config_root.mkdir()
            (config_root / "cp.config").write_text(
                "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y\n"
                "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
                "# CONFIG_BK7258_AP_CORE is not set\n",
                encoding="utf-8")
            (config_root / "ap.config").write_text(
                "CONFIG_BK7258_BOARD_AIDK_AI_TOY=y\n"
                "CONFIG_BK7258_MCUBOOT_IMAGE=y\n"
                "CONFIG_BK7258_AP_CORE=y\n",
                encoding="utf-8")
            resolved = resolve_resource_graph(
                REPOSITORY, graph, config_root=config_root)
            self.assertEqual(
                build_plan(REPOSITORY, "aidk_ai_toy_bringup",
                           config_root=config_root)["board"]["id"],
                "aidk_ai_toy")
        self.assertTrue(resolved["resolved"])
        self.assertEqual(resolved["board_selection"]["candidates"], ["aidk_ai_toy"])
        self.assertEqual(resolved["board_constraints"]["port_identity"]["port"],
                         "dynamic-usb-serial")
    def test_board_layer_is_minimal_and_wired_without_unverified_devices(self) -> None:
        root = REPOSITORY / "board/bk7258"
        header = (root / "boards/aidk_ai_toy/include/bk7258_board_config.h").read_text()
        source = (root / "boards/aidk_ai_toy/src/bk7258_board_bringup.c").read_text()
        self.assertIn('#define BK7258_BOARD_HARDWARE_VERIFIED           0', header)
        self.assertIn('#define BK7258_BOARD_CONSOLE_UART_ID             0', header)
        self.assertIn('#define BK7258_BOARD_CONSOLE_BAUD                115200u', header)
        self.assertIn('#define BK7258_BOARD_CONSOLE_FLOW_CONTROL        0', header)
        self.assertIn('#define BK7258_BOARD_CONFLICT_P20_P21_SC7A20_SWD 1', header)
        self.assertIn('#define BK7258_BOARD_CONFLICT_P0_P1_MFRC522_CN1  1', header)
        self.assertIn('#define BK7258_BOARD_CONFLICT_P8_P9_32K_KEY3_MOTOR 1', header)
        self.assertIn('int bk7258_board_devices_initialize(void)', source)
        self.assertIn('return OK;', source)
        self.assertIn('config BK7258_BOARD_AIDK_AI_TOY',
                      (root / "Kconfig").read_text())
        self.assertIn('boards/aidk_ai_toy', (root / "CMakeLists.txt").read_text())
        self.assertIn('boards$(DELIM)aidk_ai_toy',
                      (root / "scripts/Make.defs").read_text())

    def test_board_selection_and_audio_binding_fail_closed(self) -> None:
        root = REPOSITORY / "board/bk7258"
        make_defs = (root / "scripts/Make.defs").read_text()
        cmake = (root / "CMakeLists.txt").read_text()
        chip_make = (root / "chip/Make.defs").read_text()
        chip_cmake = (root / "chip/CMakeLists.txt").read_text()
        board_make = (root / "src/Makefile").read_text()
        board_cmake = (root / "src/CMakeLists.txt").read_text()
        board_kconfig = (root / "Kconfig").read_text()
        chip_kconfig = (root / "chip/Kconfig").read_text()
        self.assertIn("Select exactly one BK7258 physical board", make_defs)
        self.assertIn("Select exactly one BK7258 physical board", cmake)
        self.assertIn(
            "BK7258_BOARD_VARIANT_COUNT := "
            "$(words $(BK7258_BOARD_VARIANT_SELECTIONS))",
            make_defs)
        unresolved_goals = make_defs.split(
            "BK7258_UNRESOLVED_BOARD_GOALS :=", 1)[1].split("\n\nifeq", 1)[0]
        for goal in ("apps_preconfig", "clean", "clean_context", "dirlinks",
                     "distclean", "incdir", "olddefconfig", "preconfig"):
            self.assertIn(goal, unresolved_goals)
        self.assertIn(
            "$(filter-out $(BK7258_UNRESOLVED_BOARD_GOALS),$(MAKECMDGOALS))",
            make_defs)
        self.assertIn("BK7258_BOARD_VARIANT_COUNT EQUAL 1", cmake)
        self.assertIn("else ifeq ($(CONFIG_BK7258_BOARD_T5AI_CORE),y)",
                      make_defs)
        self.assertIn("elseif(CONFIG_BK7258_BOARD_T5AI_CORE)", cmake)
        self.assertNotIn("boards$(DELIM)", chip_make)
        self.assertNotIn("/boards/", chip_cmake)
        self.assertNotIn("CONFIG_BK7258_BOARD_T5", chip_make)
        self.assertNotIn("CONFIG_BK7258_BOARD_T5", chip_cmake)
        self.assertIn("BK7258_AUD requires a physical-board audio binding",
                      board_make)
        self.assertIn("BK7258_AUD requires a physical-board audio binding",
                      board_cmake)
        self.assertIn("$(BK7258_BOARD_SDK_DIR)$(DELIM)include", board_make)
        self.assertIn("${BK7258_SDK_ROLE_DIR}/include", board_cmake)
        self.assertIn("CONFIG_FREERTOS=0", board_make)
        self.assertIn("CONFIG_FREERTOS=0", board_cmake)
        self.assertIn("config BK7258_BOARD_HAS_MIC_BINDING", board_kconfig)
        self.assertIn("config BK7258_BOARD_HAS_USER_GPIO_BINDING",
                      board_kconfig)
        self.assertIn("config BK7258_BOARD_HAS_SDIO_BINDING",
                      board_kconfig)
        self.assertIn("depends on BK7258_BOARD_HAS_AUDIO_BINDING",
                      chip_kconfig)
        self.assertIn("BK7258_BOARD_HAS_MIC_BINDING", chip_kconfig)
        self.assertIn("BK7258_BOARD_HAS_USER_GPIO_BINDING", chip_kconfig)
        self.assertIn("BK7258_BOARD_HAS_SDIO_BINDING", chip_kconfig)
        self.assertNotIn("BK7258_T5_BOARD", chip_kconfig)
    def test_chip_sources_consume_only_typed_board_bindings(self) -> None:
        root = REPOSITORY / "board/bk7258"
        chip = root / "chip"

        for path in chip.rglob("*"):
            if path.suffix not in {".c", ".h"}:
                continue

            text = path.read_text(encoding="utf-8")
            self.assertNotIn("#include <arch/board/board.h>", text,
                             str(path))
            self.assertNotRegex(text,
                                r"\bBK7258_BOARD_(?!BINDING)[A-Z0-9_]+\b",
                                str(path))

        dvp = (chip / "ap/bk7258_dvp.c").read_text(encoding="utf-8")
        self.assertNotIn("GC2145", dvp)
        self.assertNotIn("0x78u >> 1", dvp)
        self.assertNotIn("0xf2u", dvp)

        board_make = (root / "src/Makefile").read_text(encoding="utf-8")
        board_cmake = (root / "src/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("bk7258_platform.c bk7258_board_bringup.c", board_make)
        self.assertIn("${BK7258_BOARD_VARIANT_DIR}/src/bk7258_board_bringup.c",
                      board_cmake)


if __name__ == "__main__":
    unittest.main(verbosity=2)
