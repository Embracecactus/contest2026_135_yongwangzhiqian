#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise isolated BK7258 build workspace ownership checks."""

import os
import contextlib
import io
import json
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from types import SimpleNamespace


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))

from _lib import build as build_domain  # noqa: E402
from _lib import trust as trust_domain
from _lib import layout as layout_domain
import bk7258 as cli


class BuildWorkspaceTest(unittest.TestCase):
    def test_cp_memory_report_does_not_claim_runtime_budget(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / ".config"
            config.write_text("CONFIG_IDLETHREAD_STACKSIZE=2048\n")
            elf = root / "nuttx"
            elf.write_bytes(b"fixture")
            symbols = {"_sdata": 0x28010000, "_edata": 0x28011000,
                       "_sbss": 0x28011000, "_ebss": 0x28040000,
                       "_eheap": 0x2804fffc, "g_intstackalloc": 0x28010000,
                       "g_intstacktop": 0x28010800}
            role = SimpleNamespace(role="cp", dotconfig=config, elf=elf,
                                   binary_root=root, resolved_config_sha256="fixture")
            with mock.patch.object(trust_domain, "elf_symbol",
                                   side_effect=lambda elf, nm, name: symbols[name]):
                build_domain._cp_memory_report(role, SimpleNamespace(binary_dir=root))
                report = json.loads((root / "cp-memory-report.json").read_text())
                self.assertEqual(report["initial_heap_gross_bytes"], 63484)
                self.assertEqual(report["boot_status"], "not-verified")
                self.assertIn("requires-pre-PSRAM", report["runtime_budget_status"])
                symbols["_ebss"] = symbols["_eheap"] - 1024
                with self.assertRaises(build_domain.BuildError):
                    build_domain._cp_memory_report(role, SimpleNamespace(binary_dir=root))

    def test_cp_memory_report_requires_soft_off_wfi_wrapper_in_copy(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / ".config"
            config.write_text(
                "CONFIG_IDLETHREAD_STACKSIZE=2048\n"
                "CONFIG_BK7258_PM_SOFT_OFF=y\n"
            )
            elf = root / "nuttx"
            elf.write_bytes(b"fixture")
            symbols = {
                "_sdata": 0x28010000,
                "_edata": 0x28011000,
                "_sbss": 0x28011000,
                "_ebss": 0x28040000,
                "_eheap": 0x2804FFFC,
                "g_intstackalloc": 0x28010000,
                "g_intstacktop": 0x28010800,
            }
            copied = (
                "sys_hal_enter_deep_sleep",
                "arch_deep_sleep",
                "sys_set_ana_reg_bit",
                "sys_ll_set_ana_reg5_en_cb",
                "sys_ll_set_ana_reg8_valoldosel",
                "sys_ll_set_ana_reg9_spi_latch1v",
                "sys_ll_set_ana_reg10_vbspbuflp1v",
                "sys_ll_set_ana_reg11_aldosel",
                "sys_ll_set_ana_reg12_dldosel",
                "sys_hal_enable_spi_latch",
                "sys_hal_disable_spi_latch",
                "sys_hal_power_on_and_select_rosc",
                "sys_hal_disable_hf_clock",
                "sys_hal_gpio_state_switch",
                "__wrap_arch_deep_sleep",
                "bk7258_pm_soft_off_wfi_reset",
            )
            nm_rows = "\n".join(
                f"{name} T {0x28010000 + 4 * index:08x} 4"
                for index, name in enumerate(copied)
            )
            role = SimpleNamespace(
                role="cp",
                dotconfig=config,
                elf=elf,
                binary_root=root,
                resolved_config_sha256="fixture",
            )
            with mock.patch.object(
                trust_domain,
                "elf_symbol",
                side_effect=lambda elf, nm, name: symbols[name],
            ), mock.patch.object(
                build_domain.subprocess,
                "run",
                return_value=SimpleNamespace(stdout=nm_rows),
            ):
                build_domain._cp_memory_report(role, SimpleNamespace(binary_dir=root))
                report = json.loads((root / "cp-memory-report.json").read_text())
                self.assertIn("__wrap_arch_deep_sleep", report["symbols"])
                self.assertIn("bk7258_pm_soft_off_wfi_reset", report["symbols"])
                with mock.patch.object(
                    build_domain.subprocess,
                    "run",
                    return_value=SimpleNamespace(
                        stdout="\n".join(nm_rows.splitlines()[:-1])
                    ),
                ):
                    with self.assertRaisesRegex(
                        build_domain.BuildError,
                        "bk7258_pm_soft_off_wfi_reset",
                    ):
                        build_domain._cp_memory_report(
                            role, SimpleNamespace(binary_dir=root)
                        )

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bk7258-workspace-")
        self.root = Path(self.temporary.name)
        self.workspace = self.root / "workspace"
        self.repository = self.workspace / "contest"
        for relative in ("boards/bk7258", "chips/bk7258", "nuttx", "prebuilt"):
            (self.repository / relative).mkdir(parents=True, exist_ok=True)
        self._wire(self.workspace)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _wire(self, workspace: Path) -> None:
        workspace.mkdir(parents=True, exist_ok=True)
        entry = workspace / "build.sh"
        entry.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        entry.chmod(entry.stat().st_mode | stat.S_IXUSR)
        for relative in ("boards/bk7258", "chips/bk7258", "nuttx", "prebuilt"):
            target = workspace / "vendor/beken" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(self.repository / relative, target_is_directory=True)

    def test_generated_inputs_keep_mtime_and_reject_symlinks(self) -> None:
        for domain in (build_domain, trust_domain, layout_domain):
            for name in ("_atomic_text", "_atomic_bytes"):
                writer = getattr(domain, name, None)
                if writer is None:
                    continue
                with self.subTest(domain=domain.__name__, writer=name):
                    target = self.root / "generated"
                    value = "same\n" if name.endswith("text") else b"same\n"
                    writer(target, value)
                    os.utime(target, ns=(1000000000, 1000000000))
                    writer(target, value)
                    self.assertEqual(target.stat().st_mtime_ns, 1000000000)
                    writer(target, value + value)
                    self.assertNotEqual(target.stat().st_mtime_ns, 1000000000)
                    link = self.root / "link"
                    link.symlink_to(target)
                    with self.assertRaises(
                        (
                            build_domain.BuildError,
                            trust_domain.TrustError,
                            layout_domain.LayoutError,
                        )
                    ):
                        writer(link, value)
                    link.unlink()

    def test_development_signer_is_reused_and_never_silently_rotated(self) -> None:
        executable = shutil.which("openssl")
        if executable is None:
            self.skipTest("OpenSSL is unavailable")
        with tempfile.TemporaryDirectory(
            prefix="bk7258-signing-test-", dir=Path.home()
        ) as directory:
            store = Path(directory) / "identity"
            signer, created = trust_domain.init_development_identity(
                self.repository, store, Path(executable)
            )
            self.assertTrue(created)
            self.assertEqual(stat.S_IMODE(store.stat().st_mode), 0o700)
            self.assertEqual(
                stat.S_IMODE(signer.bl1_private_key.stat().st_mode), 0o600
            )
            again, created = trust_domain.init_development_identity(
                self.repository, store, Path(executable)
            )
            self.assertFalse(created)
            self.assertEqual(again.identity, signer.identity)
            self.assertEqual(again.bl1_fingerprint, signer.bl1_fingerprint)
            missing = store / "bl1-key-temporarily-missing"
            signer.bl1_private_key.rename(missing)
            try:
                with self.assertRaisesRegex(trust_domain.TrustError, "incomplete"):
                    trust_domain.init_development_identity(
                        self.repository, store, Path(executable)
                    )
            finally:
                missing.rename(signer.bl1_private_key)
            self.assertEqual(
                trust_domain.load_development_identity(
                    self.repository, store, Path(executable)
                ).identity,
                signer.identity,
            )

    def test_default_and_explicit_workspace_are_compatible(self) -> None:
        self.assertEqual(
            build_domain._build_workspace(self.repository, None),
            self.workspace.resolve(),
        )
        isolated = self.root / "isolated"
        self._wire(isolated)
        self.assertEqual(
            build_domain._build_workspace(self.repository, isolated), isolated.resolve()
        )

    def test_provenance_is_bounded_and_records_actual_profiles(self) -> None:
        cp = build_domain.ConfigProfile(
            self.repository / "boards/bk7258/aidk_ai_toy/configs/app",
            "aidk_ai_toy",
            "cp",
            "pair",
            "cp",
        )
        ap = build_domain.ConfigProfile(
            self.repository / "boards/bk7258/aidk_ai_toy/configs/openvela_ap",
            "aidk_ai_toy",
            "ap",
            "pair",
            "ap",
        )
        source = self.repository / "chips/bk7258/input.c"
        source.write_text("int value = 1;\n")
        subprocess.run(["git", "init", "-q", str(self.repository)], check=True)
        subprocess.run(
            ["git", "-C", str(self.repository), "add", "chips/bk7258/input.c"],
            check=True,
        )
        subprocess.run(
            [
                "git",
                "-C",
                str(self.repository),
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "commit",
                "-qm",
                "fixture",
            ],
            check=True,
        )
        for name in ("nuttx", "apps"):
            (self.workspace / name).symlink_to(
                self.repository, target_is_directory=True
            )
        original = build_domain._source_provenance(self.repository, cp, ap, "shaniu")
        self.assertFalse(original["dirty"])
        copied = self.root / "copied-source"
        copied_source = copied / "chips/bk7258/input.c"
        copied_source.parent.mkdir(parents=True)
        copied_source.write_bytes(source.read_bytes())
        index = self.repository / ".git/index"
        index_before = (index.stat().st_mtime_ns, index.read_bytes())
        clean_copy = build_domain._source_tree_state(
            copied, ["chips/bk7258"], changed_only=True, git_repository=self.repository
        )
        self.assertFalse(clean_copy["dirty"])
        copied_source.write_text("int copied_value = 3;\n")
        dirty_copy = build_domain._source_tree_state(
            copied, ["chips/bk7258"], changed_only=True, git_repository=self.repository
        )
        self.assertTrue(dirty_copy["dirty"])
        self.assertNotEqual(
            clean_copy["input_tree_sha256"], dirty_copy["input_tree_sha256"]
        )
        self.assertEqual(index_before, (index.stat().st_mtime_ns, index.read_bytes()))
        self.assertEqual(
            original["profiles"]["cp"], "boards/bk7258/aidk_ai_toy/configs/app"
        )
        for path in (
            "chips/bk7258/logs/log.json",
            "chips/bk7258/secrets/key.json",
            "out/result.c",
        ):
            excluded = self.repository / path
            excluded.parent.mkdir(parents=True, exist_ok=True)
            excluded.write_text("excluded fixture")
        self.assertEqual(
            original, build_domain._source_provenance(self.repository, cp, ap, "shaniu")
        )
        source.write_text("int value = 2;\n")
        changed = build_domain._source_provenance(self.repository, cp, ap, "shaniu")
        self.assertTrue(changed["dirty"])
        self.assertNotEqual(original["input_tree_sha256"], changed["input_tree_sha256"])
        self.assertNotEqual(
            original["dependencies"]["nuttx"], changed["dependencies"]["nuttx"]
        )
        self.assertEqual(build_domain.validate_provenance(changed), changed)

    def test_missing_signing_identity_never_starts_a_tool(self) -> None:
        with mock.patch.object(trust_domain, "_run") as runner:
            with self.assertRaises(trust_domain.TrustError):
                trust_domain.public_fingerprint(
                    self.root / "absent-signing-identity.pem", Path("/usr/bin/openssl")
                )
            runner.assert_not_called()
        with contextlib.redirect_stderr(io.StringIO()), mock.patch.object(
            trust_domain, "_run"
        ) as runner:
            self.assertEqual(
                cli.main(
                    [
                        "release",
                        "ota",
                        "--build-manifest",
                        "build.json",
                        "--version",
                        "18.6.351+419",
                        "--openssl",
                        "/usr/bin/openssl",
                        "--output-dir",
                        str(self.root / "release"),
                    ]
                ),
                1,
            )
            runner.assert_not_called()
        self.assertFalse((self.root / "release").exists())
        for mode in ("full", "ota"):
            args = [
                "release",
                mode,
                "--build-manifest",
                "unused.json",
                "--version",
                "18.6.351+419",
                "--openssl",
                "/usr/bin/openssl",
                "--mcuboot-key",
                str(self.root / "absent-mcuboot.pem"),
                "--output-dir",
                str(self.root / "release"),
            ]
            if mode == "full":
                args += [
                    "--bl1-key",
                    str(self.root / "absent-bl1.pem"),
                    "--base",
                    "unused.bin",
                    "--base-evidence",
                    "unused.json",
                ]
            with contextlib.redirect_stderr(io.StringIO()), mock.patch.object(
                trust_domain, "_run"
            ) as runner:
                self.assertEqual(cli.main(args), 1)
                runner.assert_not_called()
            self.assertFalse((self.root / "release").exists())

    def test_workspace_rejects_foreign_vendor_source(self) -> None:
        isolated = self.root / "isolated"
        self._wire(isolated)
        link = isolated / "vendor/beken/nuttx"
        link.unlink()
        foreign = self.root / "foreign-nuttx"
        foreign.mkdir()
        link.symlink_to(foreign, target_is_directory=True)
        with self.assertRaisesRegex(build_domain.BuildError, "does not resolve"):
            build_domain._build_workspace(self.repository, isolated)

    def test_absolute_manifest_infers_only_a_valid_isolated_root(self) -> None:
        isolated = self.root / "isolated"
        self._wire(isolated)
        manifest = (
            isolated
            / "out/bk7258/aidk_ai_toy/cp__ap/layout"
            / "releases/direct/build-manifest.json"
        )
        manifest.parent.mkdir(parents=True)
        manifest.write_text("{}\n", encoding="utf-8")
        self.assertEqual(
            build_domain._manifest_workspace(self.repository, manifest, manifest),
            isolated.resolve(),
        )
        self.assertEqual(
            build_domain._manifest_workspace(
                self.repository, Path("out/bk7258/manifest"), manifest
            ),
            self.workspace.resolve(),
        )
        malformed = isolated / "out/bk7258/aidk_ai_toy/build-manifest.json"
        malformed.parent.mkdir(parents=True, exist_ok=True)
        malformed.write_text("{}\n", encoding="utf-8")
        with self.assertRaisesRegex(
            build_domain.BuildError, "no OpenVela workspace|does not identify"
        ):
            build_domain._manifest_workspace(self.repository, malformed, malformed)


if __name__ == "__main__":
    unittest.main()
