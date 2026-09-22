# SPDX-License-Identifier: Apache-2.0

"""Host regression for board-declared BK7258 product delivery ZIPs."""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import stat
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))
sys.path.insert(0, str(REPOSITORY / "gateway/shaniu"))

from _lib import build as build_domain  # noqa: E402
from _lib import image as image_domain  # noqa: E402
from _lib import layout as layout_domain  # noqa: E402
from _lib import package as package_domain  # noqa: E402
from _lib import product as product_domain  # noqa: E402
import bk7258 as bk7258_cli  # noqa: E402
from shaniu_gateway.firmware import load_firmware_releases  # noqa: E402


class ProductDeliveryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bk7258-product-test-")
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

    def test_clean_prunes_only_stale_role_identities(self) -> None:
        workspace = self.root / "workspace"
        role_root = (
            workspace / "out/bk7258/test_board/cp__ap/layout" / "roles/mcuboot/ap"
        )
        current = role_root / "bk7258-role-1111111111111111"
        stale = role_root / "bk7258-role-2222222222222222"
        current.mkdir(parents=True)
        stale.mkdir()
        (current / "keep").write_text("current", encoding="utf-8")
        (stale / "drop").write_text("stale", encoding="utf-8")

        build_domain._prune_stale_role_outputs(current, workspace)

        self.assertEqual((current / "keep").read_text(encoding="utf-8"), "current")
        self.assertFalse(stale.exists())

    def _inputs(self, stem: str) -> tuple[Path, Path]:
        package = self.root / f"{stem}.bkpack"
        package_domain.create(
            image_set=self.images,
            member_names={name: f"{name}.bin" for name in ("boot", "cp", "ap", "pair")},
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
            )
            + "\n",
            encoding="utf-8",
        )
        return package, manifest

    def _release_input_fixture(
        self,
        stem: str,
        *,
        identity: dict[str, object] | None = None,
        generation: int | None = None,
        package_name: str | None = None,
    ) -> Path:
        package, manifest = self._inputs(stem)
        build = json.loads(manifest.read_text(encoding="utf-8"))
        provenance = {
            "dependencies": {
                name: {
                    "source_commit": "a" * 40,
                    "dirty": False,
                    "input_tree_sha256": hashlib.sha256(b"").hexdigest(),
                    "input_count": 0,
                }
                for name in ("nuttx", "apps")
            },
            "source_commit": "a" * 40,
            "dirty": False,
            "input_tree_sha256": "b" * 64,
            "input_count": 2,
            "scope": ["app/bk7258", "chips/bk7258"],
            "product": "shaniu",
            "profiles": {
                "cp": "configs/cp-aidk",
                "ap": "configs/ap-aidk",
            },
        }
        build["format"] = build_domain.BUILD_MANIFEST_FORMAT
        build["provenance"] = provenance

        version = "18.6.351+415"
        expected_identity = product_domain.artifact_identity(
            "test_board", provenance, version, "shaniu", "A4"
        )
        assert expected_identity is not None
        selected_identity = identity or expected_identity
        release = self.root / f"{stem}-release"
        evidence = release / "evidence"
        package_dir = release / "package"
        evidence.mkdir(parents=True)
        package_dir.mkdir()
        build_path = evidence / "build-manifest.json"
        build_path.write_text(
            json.dumps(build, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        member_name = package_name or (
            product_domain.artifact_stem(expected_identity, "ota") + ".bkpack"
        )
        release_package = package_dir / member_name
        release_package.write_bytes(package.read_bytes())
        summary = {
            "build_manifest": {
                "path": "evidence/build-manifest.json",
                "sha256": hashlib.sha256(build_path.read_bytes()).hexdigest(),
            },
            "format": "bk7258.release/2",
            "generation": (
                expected_identity["security_counter"]
                if generation is None
                else generation
            ),
            "identity": selected_identity,
            "layout": build["layout"],
            "mode": "ota",
            "package": {
                "path": f"package/{member_name}",
                "sha256": hashlib.sha256(release_package.read_bytes()).hexdigest(),
            },
            "target": build["target"],
            "version": version,
        }
        bk7258_cli._release_summary(release, summary)
        return release

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

    def _ota_delivery(self, stem: str) -> Path:
        package, manifest = self._inputs(stem)
        recovery = product_domain.materialize_recovery(
            package, self.policy, self.base, self.base_evidence
        )
        ota_images = image_domain.ImageSet(
            self.layout,
            tuple(row for row in self.images.writes if row.artifact in {"cp", "ap"}),
            (),
            (),
        )
        public = bytearray(range(91))
        public[-65] = 0x04
        public_bytes = bytes(public)
        evidence = {
            "mode": "signed-ota",
            "algorithm": "ecdsa-p256-sha256",
            "mcuboot_public_fingerprint": hashlib.sha256(public_bytes).hexdigest(),
            "mcuboot_public_der": public_bytes.hex(),
            "rollback": "otp-readonly-plus-explicit-software-floor",
            "trailer": "pending-v1",
            "images": [
                {
                    "artifact": row.artifact,
                    "signed_sha256": hashlib.sha256(row.data).hexdigest(),
                    "version": "1.2.4+5",
                    "security_counter": 5,
                }
                for row in ota_images.writes
            ],
        }
        ota = self.root / f"{stem}-ota.bkpack"
        package_domain.create(
            image_set=ota_images,
            member_names={"cp": "cp.bin", "ap": "ap.bin"},
            sdk_evidence={},
            trust_evidence=evidence,
            physical_board="test_board",
            output=ota,
            catalog_signer=lambda _: b"\x30\x06\x02\x01\x01\x02\x01\x01",
        )

        def fixture_verifier(candidate: Path) -> object:
            report = package_domain.verify(candidate)
            self.assertEqual(report["security"], "signed-ota")
            return report

        output = self.root / f"{stem}-delivery.zip"
        product_domain.create_delivery(
            package=package,
            build_manifest=manifest,
            policy=self.policy,
            recovery=recovery,
            base_evidence=self.base_evidence,
            version="1.2.4+5",
            output=output,
            ota_package=ota,
            ota_required_source_version="1.2.3+4",
            package_verifier=fixture_verifier,
        )
        return output

    def test_release_product_preserves_verification_and_output_guards(self) -> None:
        # Exercise the moved orchestration using existing unsigned fixtures;
        # the callback is deliberately not evidence of cryptographic acceptance.
        package, manifest = self._inputs("product-entry")
        release = self.root / "product-entry-release"
        release.mkdir()
        package = package.rename(release / package.name)
        manifest = manifest.rename(release / manifest.name)
        recovery = product_domain.materialize_recovery(
            package, self.policy, self.base, self.base_evidence
        )
        operator = release / "operator.bin"
        operator.write_bytes(recovery.data)
        evidence = release / "accepted-base.json"
        evidence.write_bytes(self.base_evidence_path.read_bytes())

        def member(path: Path) -> dict[str, object]:
            return {
                "path": path.name,
                "size": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }

        build = json.loads(manifest.read_text())
        base_row = member(evidence)
        base_row["device_id"] = self.base_evidence.device_id
        summary = {
            "format": "bk7258.release/2",
            "mode": "full",
            "version": "1.2.3+4",
            "generation": 4,
            "target": build["target"],
            "layout": build["layout"],
            "package": member(package),
            "build_manifest": member(manifest),
            "operator": member(operator),
            "materialization": {
                "accepted_base": base_row,
                "flash_offset": 0,
                "flash_end": self.layout.flash_size,
                "flash_size": self.layout.flash_size,
            },
        }
        bk7258_cli._release_summary(release, summary)
        preset = mock.Mock(
            partition=self.layout.source, release_policy=self.policy.source
        )
        output = self.root / "product-entry.zip"
        verifier = mock.Mock(side_effect=package_domain.verify)
        kwargs = dict(
            full_release=release,
            base=self.base,
            output=output,
            ota_release=None,
            ota_required_source_version=None,
            package_verifier=verifier,
        )
        with mock.patch.object(build_domain, "board_preset", return_value=preset):
            report = product_domain.release_product(REPOSITORY, **kwargs)
            self.assertEqual(report["physical_board"], "test_board")
            verifier.assert_any_call(package)
            original = output.read_bytes()
            with self.assertRaises(ValueError):
                product_domain.release_product(REPOSITORY, **kwargs)
            self.assertEqual(output.read_bytes(), original)
            kwargs["output"] = self.root / "rejected.zip"
            verifier.side_effect = ValueError("untrusted fixture")
            with self.assertRaisesRegex(ValueError, "untrusted fixture"):
                product_domain.release_product(REPOSITORY, **kwargs)
            self.assertFalse(kwargs["output"].exists())
            verifier.side_effect = package_domain.verify
            with mock.patch.object(
                bk7258_cli,
                "_verify_package_trust",
                side_effect=lambda candidate, _: verifier(candidate),
            ), contextlib.redirect_stdout(io.StringIO()):
                status = bk7258_cli.main(
                    [
                        "release",
                        "product",
                        "--full-release",
                        str(release),
                        "--base",
                        str(self.base),
                        "--openssl",
                        "unused-fixture-tool",
                        "--output",
                        str(self.root / "cli-product.zip"),
                    ]
                )
            self.assertEqual(status, 0)
            self.assertEqual((self.root / "cli-product.zip").read_bytes(), original)

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
            self.assertIsNone(release["components"]["recovery"]["installed_root"])
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

    def test_operation_impact_distinguishes_full_flash_and_ota(self) -> None:
        package, _ = self._inputs("impact-full")

        full = product_domain.operation_impact(
            package, self.policy, transport="full-bin"
        )
        self.assertEqual(full["transport"], "full-bin")
        self.assertEqual(full["layout"]["flash_size"], self.layout.flash_size)
        self.assertEqual(full["full_bin"]["scope"], "complete-flash")
        self.assertEqual(full["full_bin"]["flash_offset"], 0)
        self.assertEqual(
            full["full_bin"]["erases"], [{"offset": 0, "size": self.layout.flash_size}]
        )
        self.assertEqual(full["full_bin"]["writes"], full["full_bin"]["erases"])
        self.assertEqual(
            full["full_bin"]["device_unique_data"],
            "trusted-same-device-base-required",
        )
        self.assertEqual(
            full["startup_migration"],
            {
                "status": "unknown",
                "unconditional_start_allowed": False,
            },
        )

        delivery = self._ota_delivery("impact-ota")
        with zipfile.ZipFile(delivery) as archive:
            ota_member = next(
                name for name in archive.namelist() if name.startswith("ota/")
            )
            ota = self.root / "impact-ota.bkpack"
            ota.write_bytes(archive.read(ota_member))

        impact = product_domain.operation_impact(ota, self.policy, transport="ota")
        self.assertEqual(impact["transport"], "ota")
        self.assertEqual(impact["ota"]["target"], "inactive")
        self.assertEqual(
            {row["artifact"] for row in impact["ota"]["payloads"]},
            {"cp", "ap"},
        )
        self.assertEqual(impact["ota"]["package_erase_operations"], [])
        self.assertEqual(
            impact["ota"]["target_device_erase_granularity"],
            "device-managed-unknown",
        )
        self.assertEqual(impact["ota"]["full_flash_base"], "not-required")

    def test_operation_impact_rejects_wrong_transport_and_large_generation(
        self,
    ) -> None:
        package, _ = self._inputs("impact-reject")
        with self.assertRaises(product_domain.ProductError):
            product_domain.operation_impact(package, self.policy, transport="ota")
        with self.assertRaises(product_domain.ProductError):
            product_domain.operation_impact(
                package, self.policy, transport="unexpected"
            )
        self.assertEqual(
            product_domain.version_generation("1.2.3+4294967295"),
            4294967295,
        )
        with self.assertRaises(product_domain.ProductError):
            product_domain.version_generation("1.2.3+4294967296")

    def test_copied_build_manifest_evidence_is_path_independent(self) -> None:
        package, manifest = self._inputs("portable-evidence")
        evidence = self.root / "detached-release/evidence/build-manifest.json"
        evidence.parent.mkdir(parents=True)
        evidence.write_bytes(manifest.read_bytes())

        document = product_domain.validate_build_manifest_evidence(evidence, package)

        self.assertEqual(document["boot"], "direct")
        self.assertEqual(
            document["target"],
            {"board_family": "bk7258", "physical_board": "test_board"},
        )

    def test_build_manifest_evidence_accepts_v2_and_v3_provenance(self) -> None:
        package, legacy_manifest = self._inputs("manifest-compat")
        package_document, _, report = product_domain._package_target_layout(package)

        legacy = product_domain._validate_build_manifest(
            legacy_manifest.read_bytes(), package_document, report["security"]
        )
        self.assertEqual(legacy["format"], build_domain.BUILD_MANIFEST_FORMAT_V2)

        current = dict(legacy)
        current["format"] = build_domain.BUILD_MANIFEST_FORMAT
        current["provenance"] = {
            "dependencies": {
                name: {
                    "source_commit": "a" * 40,
                    "dirty": False,
                    "input_tree_sha256": hashlib.sha256(b"").hexdigest(),
                    "input_count": 0,
                }
                for name in ("nuttx", "apps")
            },
            "source_commit": "a" * 40,
            "dirty": False,
            "input_tree_sha256": "b" * 64,
            "input_count": 2,
            "scope": ["app/bk7258", "chips/bk7258"],
            "product": "shaniu",
            "profiles": {
                "cp": "configs/cp-aidk",
                "ap": "configs/ap-aidk",
            },
        }

        accepted = product_domain._validate_build_manifest(
            json.dumps(current, separators=(",", ":")).encode("utf-8"),
            package_document,
            report["security"],
        )
        self.assertEqual(accepted["provenance"], current["provenance"])

    def test_release_identity_uses_explicit_product_and_artifact_id(self) -> None:
        manifest = mock.Mock()
        manifest.physical_board = "aidk_ai_toy"
        manifest.provenance = {
            "product": "shaniu",
            "profiles": {
                "cp": "configs/cp-aidk",
                "ap": "configs/ap-aidk",
            },
        }

        identity = product_domain.release_identity(
            manifest, "18.6.351+415", "shaniu", "A4"
        )

        self.assertEqual(identity["product"], "shaniu")
        self.assertEqual(identity["artifact_id"], "A4")
        self.assertEqual(identity["security_counter"], 415)
        self.assertEqual(
            product_domain.artifact_stem(identity, "ota"),
            "shaniu-bk7258-aidk_ai_toy-cp-aidk__ap-aidk-" "v18.6.351+415-bA4-ota",
        )

    def test_release_identity_rejects_conflicts_and_unsafe_names(self) -> None:
        manifest = mock.Mock()
        manifest.physical_board = "aidk_ai_toy"
        manifest.provenance = {
            "product": "shaniu",
            "profiles": {
                "cp": "configs/cp-aidk",
                "ap": "configs/ap-aidk",
            },
        }

        with self.assertRaisesRegex(ValueError, "differs from build provenance"):
            product_domain.release_identity(manifest, "18.6.351+415", "other", "A4")
        manifest.provenance["product"] = None
        with self.assertRaisesRegex(ValueError, "requires an explicit --product"):
            product_domain.release_identity(manifest, "18.6.351+415", "Shaniu!", "A4")
        with self.assertRaisesRegex(ValueError, "artifact-id"):
            product_domain.release_identity(
                manifest, "18.6.351+415", "shaniu", "A4/bad"
            )

    def test_release_input_accepts_bound_v3_identity(self) -> None:
        release = self._release_input_fixture("release-input-valid")

        summary, package, build_manifest, operator, base = product_domain.load_release(
            release, "ota"
        )

        self.assertEqual(summary["identity"]["product"], "shaniu")
        self.assertEqual(summary["generation"], 415)
        self.assertEqual(
            package.name,
            "shaniu-bk7258-test_board-cp-aidk__ap-aidk-" "v18.6.351+415-bA4-ota.bkpack",
        )
        self.assertEqual(build_manifest.parent.name, "evidence")
        self.assertIsNone(operator)
        self.assertIsNone(base)

    def test_release_input_rejects_identity_counter_and_package_name_mismatch(
        self,
    ) -> None:
        identity_release = self._release_input_fixture(
            "release-input-identity",
            identity={
                "product": "shaniu",
                "chip": "unexpected-chip",
                "board": "test_board",
                "profile": "cp-aidk__ap-aidk",
                "version": "18.6.351+415",
                "artifact_id": "A4",
                "security_counter": 415,
                "counter_policy": "legacy-version-build-equals-security-counter",
            },
        )
        with self.assertRaisesRegex(ValueError, "identity differs from build evidence"):
            product_domain.load_release(identity_release, "ota")

        counter_release = self._release_input_fixture(
            "release-input-counter", generation=414
        )
        with self.assertRaisesRegex(ValueError, "identity differs from build evidence"):
            product_domain.load_release(counter_release, "ota")

        name_release = self._release_input_fixture(
            "release-input-name", package_name="renamed.bkpack"
        )
        with self.assertRaisesRegex(
            ValueError, "package name differs from artifact identity"
        ):
            product_domain.load_release(name_release, "ota")

    def test_delivery_rejects_changed_operator(self) -> None:
        valid = self._delivery("valid")
        corrupt = self.root / "corrupt.zip"
        with zipfile.ZipFile(valid, "r") as source, zipfile.ZipFile(
            corrupt, "w", allowZip64=False
        ) as target:
            for info in source.infolist():
                data = source.read(info)
                if info.filename.endswith("full-flash.bin"):
                    data = data[:-1] + bytes([data[-1] ^ 0xFF])
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

    def test_gateway_registry_projects_only_verified_ota_metadata(self) -> None:
        delivery = self._ota_delivery("gateway")
        output = self.root / "firmware-releases.json"
        calls = []

        def fixture_verifier(candidate: Path) -> object:
            calls.append(hashlib.sha256(candidate.read_bytes()).hexdigest())
            return package_domain.verify(candidate)

        report = product_domain.create_gateway_release_registry(
            (delivery,),
            output,
            package_verifier=fixture_verifier,
        )
        self.assertEqual(report["releases"], 1)
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
        self.assertTrue(calls)
        registry = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(set(registry), {"format", "releases"})
        self.assertEqual(registry["format"], "shaniu.firmware-release-registry/1")
        self.assertEqual(len(registry["releases"]), 1)
        release = registry["releases"][0]
        self.assertEqual(release["device_id"], "test-unit:0001")
        self.assertEqual(release["target_version"], "1.2.4+5")
        self.assertEqual(release["required_source_version"], "1.2.3+4")
        self.assertEqual(release["physical_board"], "test_board")
        self.assertNotIn("path", release)
        self.assertNotIn("uri", release)

        with zipfile.ZipFile(delivery) as outer:
            delivery_manifest = json.loads(outer.read("release.json"))
            ota_row = delivery_manifest["components"]["ota"]["package"]
            ota_bytes = outer.read(ota_row["path"])
        with zipfile.ZipFile(io.BytesIO(ota_bytes)) as ota_archive:
            expected_manifest = hashlib.sha256(
                ota_archive.read("catalog.json")
            ).hexdigest()
        self.assertEqual(release["manifest_sha256"], expected_manifest)
        self.assertEqual(
            release["package_sha256"], hashlib.sha256(ota_bytes).hexdigest()
        )
        self.assertEqual(release["package_size_bytes"], len(ota_bytes))
        loaded = load_firmware_releases(output)
        self.assertEqual(
            loaded.compatible(
                "test-unit:0001",
                "1.2.3+4",
                release["required_source_root_sha256"],
            )[0].projection(),
            {key: value for key, value in release.items() if key != "device_id"},
        )

        with self.assertRaises(product_domain.ProductError):
            product_domain.create_gateway_release_registry(
                (delivery,),
                output,
                package_verifier=fixture_verifier,
            )

    def test_gateway_registry_rejects_missing_ota_and_duplicates(self) -> None:
        without_ota = self._delivery("without-ota")
        with self.assertRaises(product_domain.ProductError):
            product_domain.create_gateway_release_registry(
                (without_ota,),
                self.root / "missing.json",
                package_verifier=lambda candidate: package_domain.verify(candidate),
            )
        delivery = self._ota_delivery("duplicate")
        with self.assertRaises(product_domain.ProductError):
            product_domain.create_gateway_release_registry(
                (delivery, delivery),
                self.root / "duplicate.json",
                package_verifier=lambda candidate: package_domain.verify(candidate),
            )
        with self.assertRaises(product_domain.ProductError):
            product_domain.create_gateway_release_registry(
                (delivery,),
                self.root / "unverified.json",
                package_verifier=None,
            )

        verified = product_domain.verify_delivery(
            delivery,
            package_verifier=lambda candidate: package_domain.verify(candidate),
        )
        oversized = dict(verified)
        oversized["firmware_release"] = dict(verified["firmware_release"])
        oversized["firmware_release"]["package_size_bytes"] = (
            product_domain.MAX_GATEWAY_PACKAGE_SIZE + 1
        )
        with mock.patch.object(
            product_domain, "verify_delivery", return_value=oversized
        ):
            with self.assertRaises(product_domain.ProductError):
                product_domain.create_gateway_release_registry(
                    (delivery,),
                    self.root / "oversized.json",
                    package_verifier=lambda candidate: package_domain.verify(candidate),
                )

    def test_release_directory_publish_never_replaces_existing_output(self) -> None:
        staging = self.root / "staging-release"
        staging.mkdir()
        (staging / "release.json").write_text("candidate\n", encoding="utf-8")
        output = self.root / "published-release"
        output.mkdir()
        (output / "owner.txt").write_text("existing\n", encoding="utf-8")

        with self.assertRaises(package_domain.PackageError):
            package_domain.publish_directory_no_replace(staging, output, "test release")

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
                policy = product_domain.load_policy(preset.release_policy, layout)
                self.assertEqual(
                    set(policy.by_partition),
                    {row.name for row in layout.partitions},
                )
                self.assertEqual(layout.flash_size, 8 * 1024 * 1024)

    def test_aidk_factory_software_declares_target_bound_ranges(self) -> None:
        preset = build_domain.board_preset(REPOSITORY, "aidk_ai_toy")
        layout = layout_domain.load(preset.partition)
        policy = product_domain.load_policy(preset.release_policy, layout)
        payload = package_domain.factory_initial_persistent_payload(layout)
        persistent = next(
            row for row in layout.partitions if row.name == "persistent_data"
        )
        self.assertEqual(payload, b"\xff" * persistent.size)
        plan = product_domain.factory_software_plan(layout, policy)
        self.assertFalse(plan["materialized"])
        sources = {row["name"]: row["source"] for row in plan["partitions"]}
        self.assertEqual(sources["primary_bootloader"], "signed-build")
        self.assertEqual(sources["persistent_data"], "erased-user-state")
        self.assertEqual(sources["reset_marker"], "reset-transaction-state")
        self.assertEqual(sources["factory_state"], "factory-transaction")
        self.assertEqual(sources["sys_rf"], "same-device-snapshot")
        self.assertIn(
            "sys_rf", {row["name"] for row in plan["target_snapshot_required"]}
        )

    def test_aidk_factory_state_relocation_preserves_old_unallocated_bytes(self) -> None:
        preset = build_domain.board_preset(REPOSITORY, "aidk_ai_toy")
        layout = layout_domain.load(preset.partition)
        factory = next(row for row in layout.partitions if row.name == "factory_state")
        factory_row = "factory_state,0x7f8000,8K,data,TRUE,TRUE,,preserve\n"
        current_csv = preset.partition.read_text(encoding="utf-8")
        self.assertEqual(current_csv.count(factory_row), 1)
        old_csv = self.root / "aidk-before-factory.csv"
        old_csv.write_text(current_csv.replace(factory_row, ""), encoding="utf-8")
        old_layout = layout_domain.load(old_csv)
        base = self.root / "aidk-before-factory.bin"
        original = bytearray(b"\x5a" * layout.flash_size)
        original[factory.offset:factory.end] = bytes(range(256)) * 32
        base.write_bytes(original)
        relocated = self.root / "aidk-relocated.bin"

        report = product_domain.relocate_base(
            source_layout=old_layout, layout=layout, base=base, output=relocated
        )
        self.assertEqual(relocated.read_bytes(), original)
        self.assertEqual(
            report["carried_forward_gaps"],
            [{
                "partition": "factory_state",
                "offset": factory.offset,
                "size": factory.size,
                "sha256": hashlib.sha256(original[factory.offset:factory.end]).hexdigest(),
                "action": "preserved-unallocated",
            }],
        )

        occupied_csv = self.root / "aidk-occupied-gap.csv"
        occupied_csv.write_text(
            current_csv.replace(factory_row, factory_row.replace(
                "factory_state", "legacy_data").replace(",preserve\n", ",clear\n"
            )),
            encoding="utf-8",
        )
        with self.assertRaises(product_domain.ProductError):
            product_domain.relocate_base(
                source_layout=layout_domain.load(occupied_csv),
                layout=layout,
                base=base,
                output=self.root / "aidk-unsafe-relocated.bin",
            )

        immutable_csv = self.root / "aidk-immutable-gap.csv"
        immutable_csv.write_text(
            current_csv.replace(factory_row, factory_row.replace(
                ",preserve\n", ",immutable\n"
            )),
            encoding="utf-8",
        )
        with self.assertRaises(product_domain.ProductError):
            product_domain.relocate_base(
                source_layout=old_layout,
                layout=layout_domain.load(immutable_csv),
                base=base,
                output=self.root / "aidk-immutable-relocated.bin",
            )

    def test_fourth_board_and_non_eight_mib_layout_are_descriptor_only(self) -> None:
        repository = self.root / "synthetic_team"
        board = "future_board"
        version = "v-test"
        for role in ("cp", "ap"):
            (
                repository / "boards/bk7258" / board / "configs" / f"openvela_{role}"
            ).mkdir(parents=True)
        (repository / "chips/bk7258/bk_idk/sdk-profiles" / version).mkdir(parents=True)
        (repository / f"{repository.name}.xml").write_text(
            '<manifest><project path="sdk" name="sdk" '
            'groups="bk7258-sdk" revision="' + "0" * 40 + '" '
            'upstream="refs/tags/' + version + '"/></manifest>\n',
            encoding="utf-8",
        )
        profile_hash = "0" * 64
        for role in ("cp", "ap"):
            config = (
                repository / "boards/bk7258" / board / "configs" / f"openvela_{role}"
            )
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
            (
                repository
                / "chips/bk7258/bk_idk/sdk-profiles"
                / version
                / f"{role}.config"
            ).write_text(
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
        pair = cp.ljust(layout.artifact("cp").size, b"\xff") + ap.ljust(
            layout.artifact("ap").size, b"\xff"
        )
        images = image_domain.finalized(
            layout,
            {"boot": boot, "cp": cp, "ap": ap, "pair": pair},
            preserved_external=("bl2_a", "bl2_b", "manifest_a", "manifest_b"),
        )
        package = self.root / "aidk.bkpack"
        package_domain.create(
            image_set=images,
            member_names={name: f"{name}.bin" for name in ("boot", "cp", "ap", "pair")},
            sdk_evidence={},
            trust_evidence={"mode": "unsigned"},
            physical_board="aidk_ai_toy",
            output=package,
        )
        base = self.root / "aidk-base.bin"
        base.write_bytes(bytes([0x5A]) * layout.flash_size)
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
