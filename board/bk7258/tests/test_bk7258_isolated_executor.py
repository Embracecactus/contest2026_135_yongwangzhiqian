#!/usr/bin/env python3
"""Focused host-only tests for the canonical isolated prepare contract."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[3] / "tools" / "bk7258"
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import bk7258_isolated_executor as isolated  # noqa: E402
import bk7258_source_snapshot as snapshot  # noqa: E402


REPOSITORY = Path(__file__).resolve().parents[3]


class IsolatedPrepareTests(unittest.TestCase):
    def _prepare(self, temporary: tempfile.TemporaryDirectory[str], product: str):
        root = Path(temporary) / "fresh-build"
        output = root / "reports" / "execution.json"
        manifest = isolated.prepare(REPOSITORY, product, root, output)
        return root, output, manifest

    def _snapshot_workspace_fixture(self, temporary: tempfile.TemporaryDirectory[str]) -> Path:
        workspace = Path(temporary) / "source-workspace"
        workspace.mkdir()
        for root in snapshot.ROOTS:
            (workspace / root).mkdir()
        board = workspace / "contest2026_135_yongwangzhiqian/board/bk7258"
        for relative in (
                "bootloader/Makefile", "bootloader/bl2/Makefile",
                "chip/include/bk7258_memorymap.h", "include/board.h",
                "partitions/bk7258/auto_partitions.csv",
                "scripts/gen_bk7258_partitions.py", "scripts/bk7258_crc_expand.py"):
            path = board / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture:{relative}\n", encoding="utf-8")
        (workspace / "nuttx/CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\n", encoding="utf-8")
        (workspace / "packages/source.c").write_text("before\n", encoding="utf-8")
        return workspace

    def test_delivery_tool_records_split_board_hooks_and_host_tools(self):
        board = REPOSITORY / "board/bk7258"
        tools = REPOSITORY / "tools/bk7258"
        records = isolated._delivery_tool_records(board, tools)
        for name in ("postbuild", "crc_expand", "bl2_crc"):
            self.assertTrue(
                Path(records[name]["path"]).is_relative_to(board / "scripts")
            )
        for name in ("mcuboot_pair", "dual_image", "bkpack", "trust_chain"):
            self.assertEqual(Path(records[name]["path"]).parent, tools)

    def test_prepare_materializes_runtime_seeds_and_declares_pending_snapshot(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, manifest = self._prepare(temporary, "t5_board_bringup")
            self.assertEqual(manifest["execution_mode"], "prepare-only")
            self.assertTrue(manifest["source_view"]["shared_nuttx_config_detected"])
            self.assertFalse(manifest["source_view"]["shared_source_root_used"])
            self.assertEqual(
                manifest["source_view"]["policy"],
                "required-entity-snapshot-no-command-execution")
            self.assertEqual(manifest["phase"], "prepare")
            self.assertEqual(manifest["boot_policy"]["status"], "RECONCILED")
            self.assertEqual(manifest["boot_policy"]["execution"], "COMPILE_ONLY")
            self.assertEqual(manifest["active_roles"], ["bl1", "bl2", "cp", "ap"])
            self.assertEqual(
                hashlib.sha256(Path(manifest["plan_copy"]).read_bytes()).hexdigest(),
                manifest["plan_copy_sha256"])
            self.assertEqual(json.loads(output.read_text()), manifest)

            roots = []
            for role, row in manifest["roles"].items():
                role_root = Path(row["build_root"])
                roots.append(role_root)
                self.assertTrue(role_root.is_dir())
                self.assertFalse(role_root.is_symlink())
                self.assertTrue(Path(row["artifact_root"]).is_dir())
                self.assertTrue(Path(row["partition_contract_root"]).is_dir())
                source = row["source_view"]
                self.assertFalse(source["materialized"])
                self.assertTrue(source["required"])
                self.assertFalse(Path(source["root"]).is_symlink())
                self.assertFalse(Path(source["nuttx"]).exists())
                requirements = Path(source["snapshot_requirements"])
                self.assertTrue(requirements.is_file())
                self.assertFalse(json.loads(requirements.read_text())["materialized"])
                self.assertEqual(source, manifest["source_view"]["roles"][role])

                for command in row["commands"]:
                    self.assertEqual(command["status"], "NOT_RUN")
                    self.assertEqual(command["precondition"],
                                     "entity-source-snapshot-materialized")
                    self.assertNotIn("build_dual_image.sh", " ".join(command["argv"]))
                    self.assertEqual(command["environment"]["BK7258_SOURCE_VIEW"],
                                     source["root"])
                    if command["stage"] == "partition-contract":
                        self.assertIn(str(source["root"]), " ".join(command["argv"]))
                    if role in ("bl1", "bl2") and command["stage"] != "partition-contract":
                        self.assertIn("bootloader-staging", " ".join(command["argv"]))

                if role in ("cp", "ap"):
                    seed = Path(row["cmake_board_config"])
                    self.assertEqual(seed.parent, Path(row["config_seed_root"]))
                    self.assertTrue((seed / "profile.conf").is_file())
                    self.assertTrue((seed / "defconfig").is_file())
                    self.assertIn(
                        "-DBOARD_CONFIG=" + str(seed),
                        next(c for c in row["commands"]
                             if c["stage"] == "cmake-configure")["argv"])
                    env = row["commands"][-1]["environment"]
                    self.assertNotIn("BK7258_POSTBUILD_MODE", env)
                    self.assertNotIn("BK7258_POSTBUILD_ARTIFACT_ROOT", env)
                    self.assertNotIn("BK7258_BL1_CRC_BIN", env)
                else:
                    self.assertIsNone(row["config_seed_profile"])
                    self.assertIsNone(row["cmake_board_config"])
                if role in ("bl1", "bl2"):
                    self.assertFalse(Path(row["bootloader_staging_root"]).exists())
                else:
                    self.assertIsNone(row["bootloader_staging_root"])
            self.assertEqual(len(set(roots)), 4)
            self.assertEqual(
                len({row["source_view"]["root"] for row in manifest["roles"].values()}), 1)

    def test_prepare_aidk_hash_bound_seed(self):
        with tempfile.TemporaryDirectory() as temporary:
            _, _, manifest = self._prepare(temporary, "aidk_ai_toy_bringup")
            for role in ("cp", "ap"):
                row = manifest["roles"][role]
                seed = Path(row["cmake_board_config"])
                self.assertTrue(seed.name.startswith("aidk_ai_toy_"))
                self.assertEqual(
                    hashlib.sha256((seed / "profile.conf").read_bytes()).hexdigest(),
                    json.loads(Path(manifest["plan_copy"]).read_text())[
                        "legacy_adapter"]["seed_profiles"][role][
                        "materialized_profile_sha256"])

    def test_prepare_raw_marks_bl2_not_applicable(self):
        with tempfile.TemporaryDirectory() as temporary:
            _, _, manifest = self._prepare(temporary, "t5ai_core_bringup")
            self.assertEqual(manifest["active_roles"], ["bl1", "cp", "ap"])
            bl2 = manifest["roles"]["bl2"]
            self.assertEqual(bl2["activation"], "inactive")
            self.assertEqual(bl2["applicability"], "not-applicable")
            self.assertIsNone(bl2["bootloader_staging_root"])
            self.assertEqual(bl2["commands"], [])
            self.assertEqual(bl2["artifacts"], {})

    def test_boot_policy_make_variable_mapping_is_product_bound(self):
        expected = {
            "t5_board_bringup": ("BL1_CONSOLE_UART=3", "BL1_SWD_ENABLE=1"),
            "aidk_ai_toy_bringup": ("BL1_CONSOLE_UART=0", "BL1_SWD_ENABLE=0"),
            "t5ai_core_bringup": ("BL1_CONSOLE_UART=1", "BL1_SWD_ENABLE=0"),
        }
        for product, required in expected.items():
            with self.subTest(product=product):
                plan = isolated.framework.build_plan(REPOSITORY, product)
                resolved = isolated._resolve_policy_for_plan(REPOSITORY, plan)
                variables = isolated._boot_make_variables(plan, resolved, "bl1")
                self.assertTrue(set(required).issubset(set(variables)))

    def test_materialize_uses_one_audited_snapshot_and_role_local_boot_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._snapshot_workspace_fixture(temporary)
            real_tree_digest = isolated._tree_digest
            repository_board = REPOSITORY / "board/bk7258"

            def fixture_tree_digest(path: Path) -> str:
                if path == repository_board:
                    path = workspace / "contest2026_135_yongwangzhiqian/board/bk7258"
                return real_tree_digest(path)

            with mock.patch.object(isolated, "_workspace_root", return_value=workspace), \
                    mock.patch.object(isolated, "_tree_digest", side_effect=fixture_tree_digest):
                root, output, prepared = self._prepare(
                    temporary, "t5_board_bringup")
                prepared_plan = json.loads(Path(prepared["plan_copy"]).read_text())
                real_build_plan = isolated.framework.build_plan

                def fixture_build_plan(repository: Path, *args, **kwargs):
                    if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                        return copy.deepcopy(prepared_plan)
                    return real_build_plan(repository, *args, **kwargs)

                with mock.patch.object(
                        isolated.framework, "build_plan", side_effect=fixture_build_plan):
                    materialized = isolated.materialize_sources(REPOSITORY, output)
            self.assertEqual(materialized["phase"], "materialized")
            source_roots = {
                row["source_view"]["root"] for row in materialized["roles"].values()}
            self.assertEqual(source_roots, {str(root / "source-snapshot")})
            source_root = root / "source-snapshot"
            self.assertEqual(
                materialized["source_view"]["snapshot_identity_sha256"],
                json.loads((source_root / snapshot.MANIFEST_FILENAME).read_text())["identity_sha256"])
            self.assertEqual(
                isolated.audit_snapshot(source_root)["identity_sha256"],
                materialized["source_view"]["snapshot_identity_sha256"])
            self.assertEqual(
                materialized["roles"]["cp"]["cmake_binary_root"],
                str(root / "bk7258/t5_board_bringup/bringup/cp/cmake"))
            self.assertNotEqual(
                materialized["roles"]["cp"]["cmake_binary_root"],
                materialized["roles"]["ap"]["cmake_binary_root"])
            self.assertFalse(source_root.stat().st_mode & 0o222)
            plan = json.loads(Path(materialized["plan_copy"]).read_text())
            partition_source = plan["partition_layout"]["source"]
            self.assertTrue(partition_source.startswith("board/bk7258/"))
            snapshot_csv = Path(materialized["roles"]["bl1"]["source_view"]["board"]) / \
                partition_source.removeprefix("board/bk7258/")
            staging = []
            for role in ("bl1", "bl2"):
                staging_root = Path(materialized["roles"][role]["bootloader_staging_root"])
                staging.append(staging_root)
                self.assertTrue(staging_root.is_dir())
                self.assertTrue(staging_root.stat().st_mode & 0o222)
                staged_makefile = staging_root / "bootloader/Makefile"
                snapshot_makefile = source_root / (
                    "contest2026_135_yongwangzhiqian/board/bk7258/bootloader/Makefile")
                self.assertNotEqual(staged_makefile.stat().st_ino,
                                    snapshot_makefile.stat().st_ino)
                self.assertEqual(staged_makefile.stat().st_nlink, 1)
                adapter_argv = next(
                    command["argv"] for command in materialized["roles"][role]["commands"]
                    if command["stage"] == "make-compile-only")
                self.assertIn(
                    f"PARTITION_CONTRACT_ROOT={materialized['roles'][role]['partition_contract_root']}",
                    adapter_argv)
                self.assertIn(
                    f"PARTITION_CSV={snapshot_csv}",
                    adapter_argv)
                partition_argv = next(
                    command["argv"] for command in materialized["roles"][role]["commands"]
                    if command["stage"] == "partition-contract")
                self.assertEqual(
                    partition_argv[partition_argv.index("--input") + 1], str(snapshot_csv))
                self.assertIn(
                    f"PARTITION_GENERATOR={partition_argv[1]}", adapter_argv)
                self.assertIn(f"PARTITION_LAYOUT_ID={plan['partition_layout']['layout_id']}",
                              adapter_argv)
                self.assertIn(
                    f"PARTITION_LAYOUT_SHA256={plan['partition_layout']['layout_sha256']}",
                    adapter_argv)
            self.assertNotEqual(staging[0], staging[1])
            source_file = workspace / "packages/source.c"
            source_file.write_text("after\n", encoding="utf-8")
            self.assertEqual((source_root / "packages/source.c").read_text(), "before\n")
            for status in ("compile", "sign", "package", "hardware", "network"):
                self.assertEqual(materialized["side_effects"][status], "NOT_RUN")

    def test_materialize_failure_does_not_turn_manifest_green(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._snapshot_workspace_fixture(temporary)
            with mock.patch.object(isolated, "_workspace_root", return_value=workspace):
                _, output, prepared = self._prepare(temporary, "t5_board_bringup")
                with mock.patch.object(
                        isolated, "snapshot_workspace",
                        side_effect=isolated.SnapshotError("fixture failure")):
                    with self.assertRaises(isolated.IsolatedExecutorError):
                        isolated.materialize_sources(REPOSITORY, output)
            unchanged = json.loads(output.read_text())
            self.assertEqual(unchanged["phase"], "prepare")
            self.assertEqual(unchanged["execution_mode"], "prepare-only")
            self.assertFalse(unchanged["source_view"]["materialized"])
            self.assertEqual(unchanged["identity_sha256"], prepared["identity_sha256"])

    def test_materialize_rejects_board_source_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._snapshot_workspace_fixture(temporary)
            real_tree_digest = isolated._tree_digest
            repository_board = REPOSITORY / "board/bk7258"

            def fixture_tree_digest(path: Path) -> str:
                if path == repository_board:
                    path = workspace / "contest2026_135_yongwangzhiqian/board/bk7258"
                return real_tree_digest(path)

            with mock.patch.object(isolated, "_workspace_root", return_value=workspace), \
                    mock.patch.object(isolated, "_tree_digest", side_effect=fixture_tree_digest):
                _, output, prepared = self._prepare(temporary, "t5_board_bringup")
                (workspace / "contest2026_135_yongwangzhiqian/board/bk7258/bootloader/Makefile").write_text(
                    "mutated\n", encoding="utf-8")
                with self.assertRaises(isolated.IsolatedExecutorError):
                    isolated.materialize_sources(REPOSITORY, output)
            unchanged = json.loads(output.read_text())
            self.assertEqual(unchanged["phase"], "prepare")
            self.assertEqual(unchanged["identity_sha256"], prepared["identity_sha256"])

    def test_materialize_rejects_snapshot_build_plan_identity_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = self._snapshot_workspace_fixture(temporary)
            real_tree_digest = isolated._tree_digest
            repository_board = REPOSITORY / "board/bk7258"

            def fixture_tree_digest(path: Path) -> str:
                if path == repository_board:
                    path = workspace / "contest2026_135_yongwangzhiqian/board/bk7258"
                return real_tree_digest(path)

            with mock.patch.object(isolated, "_workspace_root", return_value=workspace), \
                    mock.patch.object(isolated, "_tree_digest", side_effect=fixture_tree_digest):
                root, output, prepared = self._prepare(temporary, "t5_board_bringup")
                prepared_plan = json.loads(Path(prepared["plan_copy"]).read_text())
                real_build_plan = isolated.framework.build_plan

                def drifted_build_plan(repository: Path, *args, **kwargs):
                    if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                        drifted = copy.deepcopy(prepared_plan)
                        drifted["identity_sha256"] = "0" * 64
                        return drifted
                    return real_build_plan(repository, *args, **kwargs)

                with mock.patch.object(
                        isolated.framework, "build_plan", side_effect=drifted_build_plan):
                    with self.assertRaises(isolated.IsolatedExecutorError):
                        isolated.materialize_sources(REPOSITORY, output)
            unchanged = json.loads(output.read_text())
            self.assertEqual(unchanged["phase"], "prepare")
            self.assertEqual(unchanged["identity_sha256"], prepared["identity_sha256"])

    def test_prepare_rejects_unsafe_targets_and_run_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            nonempty = temporary_path / "nonempty"
            nonempty.mkdir()
            (nonempty / "keep").write_text("user-state")
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated.prepare(REPOSITORY, "t5_board_bringup", nonempty,
                                  nonempty / "manifest.json")

            target = temporary_path / "target"
            target.mkdir()
            linked = temporary_path / "linked-build"
            linked.symlink_to(target, target_is_directory=True)
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated.prepare(REPOSITORY, "t5_board_bringup", linked,
                                  target / "manifest.json")

            real_parent = temporary_path / "real-parent"
            real_parent.mkdir()
            alias_parent = temporary_path / "alias-parent"
            alias_parent.symlink_to(real_parent, target_is_directory=True)
            aliased_build = alias_parent / "new-build"
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated.prepare(REPOSITORY, "t5_board_bringup", aliased_build,
                                  aliased_build / "manifest.json")

            fresh = temporary_path / "fresh"
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated.prepare(REPOSITORY, "t5_board_bringup", fresh,
                                  temporary_path / "outside.json")

            root, _, manifest = self._prepare(temporary, "t5_board_bringup")
            tampered = copy.deepcopy(manifest)
            tampered["roles"]["cp"]["commands"][0]["status"] = "RUN"
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated._validate_manifest(tampered)
            self.assertTrue(root.is_dir())

    def _runtime_fixture(self, temporary: tempfile.TemporaryDirectory[str], *, fail_target: str | None = None,
                         include_system_map: bool = False, product: str = "t5_board_bringup"):
        """Build a small source fixture and a fake CMake executable."""
        workspace = self._snapshot_workspace_fixture(temporary)
        generator = workspace / "contest2026_135_yongwangzhiqian/board/bk7258/scripts/gen_bk7258_partitions.py"
        generator.write_text(
            """import pathlib, sys
args = sys.argv[1:]
def value(flag): return pathlib.Path(args[args.index(flag) + 1])
header = value('--header'); output = value('--output-dir')
header.parent.mkdir(parents=True, exist_ok=True); output.mkdir(parents=True, exist_ok=True)
header.write_text('// fixture partition contract\\n')
(output / 'partition.json').write_text('{}\\n')
""",
            encoding="utf-8")
        fake_cmake = Path(temporary) / "fake-cmake.py"
        failure = repr(fail_target)
        system_map = repr(include_system_map)
        fake_cmake.write_text(
            f"""#!/usr/bin/env python3
import pathlib, sys
fail = {failure}
include_system_map = {system_map}
args = sys.argv[1:]
if '--version' in args:
    print('fake-cmake 1.0')
    raise SystemExit(0)
if '-B' in args: build = pathlib.Path(args[args.index('-B') + 1])
else: build = pathlib.Path(args[args.index('--build') + 1])
build.mkdir(parents=True, exist_ok=True)
if '--build' in args:
    target = args[args.index('--target') + 1]
    if target == fail: sys.exit(7)
    (build / '.config').write_text('CONFIG_FIXTURE=y\\n')
    (build / 'nuttx').write_bytes(b'ELF-fixture')
    (build / 'nuttx.map').write_text('map\\n')
    if include_system_map:
        (build / 'System.map').write_text('system map\\n')
    (build / 'nuttx.bin').write_bytes(b'BIN-fixture')
    (build / 'arch').mkdir(exist_ok=True); (build / 'arch/libarch.a').write_bytes(b'arch')
    (build / 'boards').mkdir(exist_ok=True); (build / 'boards/libboard.a').write_bytes(b'board')
""",
            encoding="utf-8")
        fake_cmake.chmod(0o755)
        fake_make = Path(temporary) / "fake-make.py"
        fake_make.write_text(
            """#!/usr/bin/env python3
import pathlib, sys
args = sys.argv[1:]
if '--version' in args:
    print('fake-make 1.0')
    raise SystemExit(0)
root = pathlib.Path(args[args.index('-C') + 1])
target = args[-1]
if target != 'compile-only':
    raise SystemExit(9)
if root.name == 'bl2':
    (root / 'bl2.elf').write_bytes(b'BL2-ELF-fixture')
    (root / 'bl2.bin').write_bytes(b'BL2-BIN-fixture')
    (root / 'bl2.map').write_text('BL2-MAP-fixture\\n')
else:
    (root / 'bl.elf').write_bytes(b'BL1-ELF-fixture')
    (root / 'bl.bin').write_bytes(b'BL1-BIN-fixture')
    (root / 'bl.map').write_text('BL1-MAP-fixture\\n')
""",
            encoding="utf-8")
        fake_make.chmod(0o755)
        fake_olddefconfig = Path(temporary) / "olddefconfig"
        fake_olddefconfig.write_text("#!/usr/bin/env python3\n", encoding="utf-8")
        fake_olddefconfig.chmod(0o755)
        kconfiglib_root = Path(temporary) / "kconfiglib"
        kconfiglib_root.mkdir()
        (kconfiglib_root / "kconfiglib.py").write_text("# fixture kconfiglib\n", encoding="utf-8")
        (kconfiglib_root / "olddefconfig.py").write_text("# fixture olddefconfig\n", encoding="utf-8")
        real_tree_digest = isolated._tree_digest
        repository_board = REPOSITORY / "board/bk7258"

        def fixture_tree_digest(path: Path) -> str:
            if path == repository_board:
                path = workspace / "contest2026_135_yongwangzhiqian/board/bk7258"
            return real_tree_digest(path)

        patches = [
            mock.patch.object(isolated, "_workspace_root", return_value=workspace),
            mock.patch.object(isolated, "_tree_digest", side_effect=fixture_tree_digest),
        ]
        for patch in patches:
            patch.start()
        try:
            root, output, prepared = self._prepare(temporary, product)
            prepared_plan = json.loads(Path(prepared["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                materialized = isolated.materialize_sources(REPOSITORY, output)
            return root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make
        finally:
            for patch in reversed(patches):
                patch.stop()

    def test_compile_runtime_fake_cmake_order_isolation_and_artifact_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(temporary)
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                built = isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            self.assertEqual(built["phase"], "runtime-built")
            self.assertEqual(built["execution_mode"], "compile-runtime")
            self.assertEqual(built["side_effects"]["compile"], "PASS")
            self.assertEqual(built["side_effects"]["sign"], "NOT_RUN")
            self.assertTrue(Path(built["tools"]["make"]["path"]).is_file())
            self.assertTrue(built["tools"]["make"]["version"])
            for tool in ("python", "cmake", "ninja", "arm-none-eabi-gcc"):
                self.assertTrue(Path(built["tools"][tool]["path"]).is_absolute())
                self.assertTrue(Path(built["tools"][tool]["path"]).is_file())
                self.assertTrue(built["tools"][tool]["version"])
            self.assertTrue(Path(built["tools"]["olddefconfig"]["path"]).is_file())
            self.assertTrue(built["tools"]["olddefconfig"]["sha256"])
            self.assertEqual(
                built["tools"]["kconfiglib"]["root"], str(kconfiglib_root))
            self.assertTrue(built["tools"]["kconfiglib"]["kconfiglib_sha256"])
            self.assertTrue(built["tools"]["kconfiglib"]["olddefconfig_sha256"])
            for role in ("cp", "ap"):
                row = built["roles"][role]
                self.assertEqual(
                    [command["stage"] for command in row["commands"]],
                    list(isolated.RUNTIME_COMMAND_STAGES),
                )
                self.assertTrue(all(command["status"] == "PASS" for command in row["commands"]))
                self.assertNotIn("nuttx_post_build", " ".join(
                    item for command in row["commands"] for item in command["argv"]))
                configure_argv = next(
                    command["argv"] for command in row["commands"]
                    if command["stage"] == "cmake-configure")
                self.assertIn(
                    "-DPython3_EXECUTABLE=" + built["tools"]["python"]["path"],
                    configure_argv)
                config_path = Path(row["config_path"])
                self.assertEqual(config_path, Path(row["cmake_binary_root"]) / ".config")
                self.assertTrue(config_path.is_file())
                self.assertTrue(set(isolated.RUNTIME_ARTIFACT_NAMES) <= set(row["artifacts"]))
                self.assertNotIn("System.map", row["artifacts"])
                self.assertEqual(
                    hashlib.sha256(config_path.read_bytes()).hexdigest(),
                    row["artifacts"][".config"]["sha256"])
                for artifact in row["artifacts"].values():
                    path = Path(artifact["path"])
                    self.assertTrue(path.is_file())
                    self.assertGreater(artifact["size"], 0)
                    self.assertEqual(artifact["size"], path.stat().st_size)
                    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), artifact["sha256"])
                    self.assertTrue(str(path).startswith(str(Path(row["artifact_root"]))))
                self.assertNotEqual(
                    Path(built["roles"]["cp"]["cmake_binary_root"]),
                    Path(built["roles"]["ap"]["cmake_binary_root"]),
                )
            expected_boot_args = {
                "bl1": {
                    "BL1_COMPILE_ONLY=1", "BL1_COMPILE_POLICY=mcuboot",
                    "BL1_USE_BL2=1", "BL1_MANIFEST_ENFORCE=1",
                    "BL1_SWD_ENABLE=1", "BL1_SWD_PIN_GROUP=1",
                    "BL1_SWD_TARGET=0", "BL1_SWD_BOOT_HOLD=0",
                    "BL1_CONSOLE_UART=3", "BL1_CONSOLE_BAUD=115200",
                    "BL1_CONSOLE_DATA_BITS=8", "BL1_CONSOLE_PARITY=0",
                    "BL1_CONSOLE_STOP_BITS=1", "BL1_UART2_PIN_GROUP=0",
                    "BL2_LOGICAL_SIZE=0x3000",
                },
                "bl2": {
                    "BL2_COMPILE_ONLY=1", "BL2_COMPILE_POLICY=mcuboot",
                    "BL2_SWD_ENABLE=1", "BL2_SWD_PIN_GROUP=1",
                    "BL2_SWD_TARGET=0", "BL2_SWD_BOOT_HOLD=1",
                    "BL2_CONSOLE_UART=3", "BL2_CONSOLE_BAUD=115200",
                    "BL2_CONSOLE_DATA_BITS=8", "BL2_CONSOLE_PARITY=0",
                    "BL2_CONSOLE_STOP_BITS=1", "BL2_UART2_PIN_GROUP=0",
                    "BL2_LOGICAL_SIZE=0x3000", "BL2_LOGICAL_CAPACITY=0x20000",
                    "BL2_LOGICAL_CAPACITY_BYTES=131072",
                    "BL2_PHYSICAL_SIZE=0x22000", "BL2_PHYSICAL_SIZE_BYTES=139264",
                    "BL2_SECURITY_COUNTER_FLOOR=0", "BL2_KEY_SOURCE=bk7258_bl2_keys.c",
                },
            }
            for role, artifact in (("bl1", "bl.elf"), ("bl2", "bl2.elf")):
                self.assertEqual([command["stage"] for command in built["roles"][role]["commands"]],
                                 list(isolated.BOOT_COMMAND_STAGES))
                self.assertTrue(all(command["status"] == "PASS"
                                    for command in built["roles"][role]["commands"]))
                self.assertIn(artifact, built["roles"][role]["artifacts"])
                make_argv = built["roles"][role]["commands"][1]["argv"]
                self.assertTrue(expected_boot_args[role].issubset(set(make_argv)))
                partition_argv = built["roles"][role]["commands"][0]["argv"]
                partition_csv = partition_argv[partition_argv.index("--input") + 1]
                self.assertIn(f"PARTITION_CSV={partition_csv}", make_argv)
                self.assertIn(f"PARTITION_GENERATOR={partition_argv[1]}", make_argv)
                for name in isolated.BOOT_ARTIFACT_NAMES[role]:
                    record = built["roles"][role]["artifacts"][name]
                    self.assertFalse(record["runnable"])
                    self.assertFalse(record["trusted"])
                    self.assertGreater(record["size"], 0)

    def test_compile_runtime_records_optional_system_map_when_present(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                temporary, include_system_map=True)
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                built = isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            for role in isolated.RUNTIME_ROLES:
                record = built["roles"][role]["artifacts"]["System.map"]
                path = Path(record["path"])
                self.assertTrue(path.is_file())
                self.assertGreater(record["size"], 0)
                self.assertEqual(record["size"], path.stat().st_size)
                self.assertEqual(record["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_compile_runtime_raw_skips_bl2_staging_commands_and_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                temporary, product="t5ai_core_bringup")
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                built = isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            self.assertEqual(built["active_roles"], ["bl1", "cp", "ap"])
            bl2 = built["roles"]["bl2"]
            self.assertEqual(bl2["activation"], "inactive")
            self.assertEqual(bl2["applicability"], "not-applicable")
            self.assertEqual(bl2["commands"], [])
            self.assertEqual(bl2["artifacts"], {})
            bl1_make = built["roles"]["bl1"]["commands"][1]["argv"]
            self.assertIn("BL1_COMPILE_ONLY=1", bl1_make)
            self.assertIn("BL1_COMPILE_POLICY=raw", bl1_make)
            self.assertIn("BL1_USE_BL2=0", bl1_make)
            self.assertIn("BL1_MANIFEST_ENFORCE=0", bl1_make)
            self.assertNotIn("BL2_COMPILE_ONLY=1", bl1_make)
            self.assertEqual(built["side_effects"]["sign"], "NOT_RUN")
            self.assertEqual(built["side_effects"]["package"], "NOT_RUN")

    def test_existing_compile_only_boot_elf_symbols_are_policy_bound(self):
        nm = shutil.which("arm-none-eabi-nm")
        bl1 = REPOSITORY / "board/bk7258/bootloader/bl.elf"
        bl2 = REPOSITORY / "board/bk7258/bootloader/bl2/bl2.elf"
        if nm is None or not bl1.is_file() or not bl2.is_file():
            self.skipTest("existing host-built boot ELF artifacts are unavailable")
        bl1_symbols = subprocess.run(
            [nm, "-g", str(bl1)], capture_output=True, text=True, check=True).stdout
        self.assertIn("bk7258_bl1_handoff_vector_valid", bl1_symbols)
        self.assertIn("bk7258_bl1_manifest_verify_at", bl1_symbols)
        bl2_symbols = subprocess.run(
            [nm, "-g", str(bl2)], capture_output=True, text=True, check=True).stdout
        self.assertIn("bootutil_keys", bl2_symbols)
        self.assertIn("bootutil_key_cnt", bl2_symbols)

    def test_runtime_manifest_rejects_unbound_or_mismatched_config_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                temporary)
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                built = isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)

            def reidentity(manifest):
                body = dict(manifest)
                body.pop("identity_sha256", None)
                manifest["identity_sha256"] = isolated._digest_bytes(
                    isolated._canonical(body))

            bad_path = copy.deepcopy(built)
            bad_path["roles"]["cp"]["config_path"] = str(
                Path(built["roles"]["cp"]["build_root"]) / "missing.config")
            reidentity(bad_path)
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated._validate_manifest(bad_path)

            wrong_hash = copy.deepcopy(built)
            wrong_config = Path(built["roles"]["cp"]["build_root"]) / "wrong.config"
            wrong_config.write_bytes(b"not the cmake config\n")
            wrong_hash["roles"]["cp"]["config_path"] = str(wrong_config)
            reidentity(wrong_hash)
            with self.assertRaises(isolated.IsolatedExecutorError):
                isolated._validate_manifest(wrong_hash)

    def test_manifest_rejects_external_source_and_partition_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                temporary)
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                built = isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)

            def reidentity(manifest):
                body = dict(manifest)
                body.pop("identity_sha256", None)
                manifest["identity_sha256"] = isolated._digest_bytes(
                    isolated._canonical(body))

            for field in ("root", "nuttx", "board", "shared_nuttx", "shared_config"):
                with self.subTest(source_field=field):
                    tampered = copy.deepcopy(built)
                    external = str(Path(temporary) / f"external-{field}")
                    tampered["roles"]["bl1"]["source_view"][field] = external
                    tampered["source_view"]["roles"]["bl1"][field] = external
                    reidentity(tampered)
                    with self.assertRaises(isolated.IsolatedExecutorError):
                        isolated._validate_manifest(tampered)

            for field, marker in (("generator", "PARTITION_GENERATOR="),
                                  ("csv", "PARTITION_CSV=")):
                with self.subTest(partition_field=field):
                    tampered = copy.deepcopy(built)
                    external = str(Path(temporary) / f"external-{field}")
                    partition_argv = tampered["roles"]["bl1"]["commands"][0]["argv"]
                    make_argv = tampered["roles"]["bl1"]["commands"][1]["argv"]
                    if field == "generator":
                        partition_argv[1] = external
                    else:
                        partition_argv[partition_argv.index("--input") + 1] = external
                    make_argv[:] = [
                        f"{marker}{external}" if item.startswith(marker) else item
                        for item in make_argv]
                    reidentity(tampered)
                    with self.assertRaises(isolated.IsolatedExecutorError):
                        isolated._validate_manifest(tampered)

    def test_compile_runtime_rejects_post_command_snapshot_mutation(self):
        for mutation in ("content", "mode"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                    temporary)
                prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
                real_build_plan = isolated.framework.build_plan
                snapshot_file = root / isolated.SOURCE_SNAPSHOT_DIRNAME / "nuttx/CMakeLists.txt"
                changed = False

                def fixture_build_plan(repository: Path, *args, **kwargs):
                    if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                        return copy.deepcopy(prepared_plan)
                    return real_build_plan(repository, *args, **kwargs)

                def tampering_runner(argv, **kwargs):
                    nonlocal changed
                    result = subprocess.run(argv, **kwargs)
                    if not changed:
                        changed = True
                        original_mode = snapshot_file.stat().st_mode & 0o777
                        if mutation == "content":
                            os.chmod(snapshot_file, original_mode | 0o200)
                            snapshot_file.write_bytes(snapshot_file.read_bytes() + b"tampered\n")
                            os.chmod(snapshot_file, original_mode)
                        else:
                            os.chmod(snapshot_file, original_mode | 0o200)
                    return result

                with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                    with self.assertRaises(isolated.IsolatedExecutorError):
                        isolated.compile_runtime(
                            REPOSITORY, output, authorize_compile=True,
                            cmake_executable=fake_cmake,
                            python_executable=Path(sys.executable).resolve(),
                            olddefconfig_executable=fake_olddefconfig,
                            kconfiglib_root=kconfiglib_root, make_executable=fake_make,
                            command_runner=tampering_runner)
                self.assertEqual(json.loads(output.read_text())["phase"], "materialized")

    def test_compile_runtime_accepts_materialized_legacy_manifest(self):
        """The materialize->compile transition accepts its older manifest shape."""
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(temporary)
            marker = json.loads(
                (root / isolated.SOURCE_SNAPSHOT_DIRNAME /
                 isolated.SNAPSHOT_REQUIREMENTS_FILENAME).read_text())
            self.assertTrue(marker["materialized"])
            self.assertEqual(
                marker["snapshot_identity_sha256"],
                materialized["source_view"]["snapshot_identity_sha256"])

            # This is the shape emitted before runtime-built records were
            # added.  Rebind the legacy body identity exactly as that stage
            # did; compile_runtime then migrates it only in memory.
            legacy = json.loads(output.read_text())
            for role, row in legacy["roles"].items():
                row.pop("artifacts", None)
                for command in row["commands"]:
                    command.pop("log", None)
                    command.pop("returncode", None)
                if role in isolated.RUNTIME_ROLES:
                    configure = next(command for command in row["commands"]
                                     if command["stage"] == "cmake-configure")
                    configure["argv"].remove("-G")
                    configure["argv"].remove("Ninja")
                    configure["argv"] = [
                        item for item in configure["argv"]
                        if not item.startswith("-DNUTTX_APPS_DIR=") and
                        not item.startswith("-DPython3_EXECUTABLE=")]
                    build = next(command for command in row["commands"]
                                  if command["stage"] == "cmake-build")
                    build["argv"][build["argv"].index("--target") + 1] = "nuttx_post_build"
                    row["commands"] = [command for command in row["commands"]
                                       if command["stage"] != "cmake-build-bin"]
            legacy["identity_sha256"] = isolated._digest_bytes(isolated._canonical({
                key: value for key, value in legacy.items()
                if key != "identity_sha256"}))
            output.write_bytes(isolated._canonical(legacy))

            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                built = isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            self.assertEqual(built["phase"], "runtime-built")
            self.assertEqual(json.loads(output.read_text())["phase"], "runtime-built")

    def test_compile_runtime_failure_is_not_green_and_requires_authorization(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                temporary, fail_target="nuttx")
            with self.assertRaises(isolated.RuntimeCompileError):
                isolated.compile_runtime(REPOSITORY, output, cmake_executable=fake_cmake)
            unchanged = json.loads(output.read_text())
            self.assertEqual(unchanged["phase"], "materialized")
            self.assertEqual(unchanged["side_effects"]["compile"], "NOT_RUN")
            with self.assertRaises(isolated.RuntimeCompileError):
                prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
                real_build_plan = isolated.framework.build_plan

                def fixture_build_plan(repository: Path, *args, **kwargs):
                    if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                        return copy.deepcopy(prepared_plan)
                    return real_build_plan(repository, *args, **kwargs)

                with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                    isolated.compile_runtime(
                        REPOSITORY, output, authorize_compile=True,
                        cmake_executable=fake_cmake,
                        python_executable=Path(sys.executable).resolve(),
                        olddefconfig_executable=fake_olddefconfig,
                        kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            self.assertEqual(json.loads(output.read_text())["phase"], "materialized")

    def test_compile_runtime_rejects_missing_or_wrong_boot_policy_argument(self):
        cases = (
            ("bl1", "BL1_USE_BL2=1", "remove"),
            ("bl1", "BL1_USE_BL2=1", "BL1_USE_BL2=0"),
            ("bl2", "BL2_LOGICAL_SIZE=0x3000", "BL2_LOGICAL_SIZE=0x20000"),
            ("bl1", None, "CC=/tmp/x"),
            ("bl1", None, "BL1_MANIFEST_KEY_SOURCE=/secret"),
            ("bl1", None, "verify"),
        )
        for role, original, replacement in cases:
            with self.subTest(replacement=replacement), tempfile.TemporaryDirectory() as temporary:
                root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                    temporary)
                tampered = json.loads(output.read_text())
                make_argv = tampered["roles"][role]["commands"][1]["argv"]
                if original is None:
                    make_argv.append(replacement)
                elif replacement == "remove":
                    make_argv.remove(original)
                else:
                    make_argv[make_argv.index(original)] = replacement
                body = dict(tampered)
                body.pop("identity_sha256", None)
                tampered["identity_sha256"] = isolated._digest_bytes(isolated._canonical(body))
                output.write_bytes(isolated._canonical(tampered))
                prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
                real_build_plan = isolated.framework.build_plan

                def fixture_build_plan(repository: Path, *args, **kwargs):
                    if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                        return copy.deepcopy(prepared_plan)
                    return real_build_plan(repository, *args, **kwargs)

                with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                    with self.assertRaises(isolated.RuntimeCompileError):
                        isolated.compile_runtime(
                            REPOSITORY, output, authorize_compile=True,
                            cmake_executable=fake_cmake,
                            python_executable=Path(sys.executable).resolve(),
                            olddefconfig_executable=fake_olddefconfig,
                            kconfiglib_root=kconfiglib_root, make_executable=fake_make)
                self.assertEqual(json.loads(output.read_text())["phase"], "materialized")

    def test_compile_runtime_rejects_noncanonical_command_environment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = self._runtime_fixture(
                temporary)
            tampered = json.loads(output.read_text())
            tampered["roles"]["bl1"]["commands"][0]["environment"]["HOME"] = "/tmp/shared-home"
            body = dict(tampered)
            body.pop("identity_sha256", None)
            tampered["identity_sha256"] = isolated._digest_bytes(isolated._canonical(body))
            output.write_bytes(isolated._canonical(tampered))
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                with self.assertRaises(isolated.RuntimeCompileError):
                    isolated.compile_runtime(
                        REPOSITORY, output, authorize_compile=True,
                        cmake_executable=fake_cmake,
                        python_executable=Path(sys.executable).resolve(),
                        olddefconfig_executable=fake_olddefconfig,
                        kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            self.assertEqual(json.loads(output.read_text())["phase"], "materialized")

    def test_delivery_log_redacts_key_path_on_success_and_runner_failure(self):
        """Child diagnostics must not persist an authorized key pathname."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            secret = root / "private-root.pem"
            secret.write_text("fake-private-key\n", encoding="utf-8")
            cwd = root / "cwd"
            cwd.mkdir()
            env = {"PATH": os.defpath, "LANG": "C", "LC_ALL": "C"}
            actual = ["python3", "tool.py", "--key", str(secret)]
            command = isolated._make_delivery_command(
                "fake-sign", "python3", actual, cwd, env, "sign")
            log = root / "logs" / "success.log"

            def echo_runner(argv, **kwargs):
                return subprocess.CompletedProcess(
                    argv, 0, stdout=f"argv={secret}\n", stderr=f"failed={secret}\n")

            isolated._run_delivery_command(
                command, actual, env, cwd, log, runner=echo_runner)
            text = log.read_text(encoding="utf-8")
            self.assertNotIn(str(secret), text)
            self.assertIn(isolated.PRIVATE_KEY_TOKEN, text)
            self.assertNotIn(str(secret), json.dumps(command))

            failure_log = root / "logs" / "failure.log"

            def failing_runner(argv, **kwargs):
                raise RuntimeError(f"tool echoed {secret}")

            with self.assertRaises(isolated.RuntimeDeliveryError):
                isolated._run_delivery_command(
                    isolated._make_delivery_command(
                        "fake-fail", "python3", actual, cwd, env, "sign"),
                    actual, env, cwd, failure_log, runner=failing_runner)
            self.assertNotIn(str(secret), failure_log.read_text(encoding="utf-8"))

    def test_delivery_rejects_key_inside_build_root_before_signer(self):
        """A delivery key must never become part of the indexed build tree."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repository"
            build_root = root / "build-root"
            delivery_root = build_root / "delivery"
            repository.mkdir()
            delivery_root.mkdir(parents=True)
            manifest = delivery_root / "execution.json"
            manifest.write_text("delivery-prepared\n", encoding="utf-8")
            before = manifest.read_bytes()
            key = delivery_root / "mcuboot-signing.pem"
            key.write_bytes(b"not-a-real-key")

            with self.assertRaises(isolated.RuntimeDeliveryError):
                isolated._authorized_private_key(
                    key, "mcuboot_signing_key", repository, build_root)

            self.assertEqual(manifest.read_bytes(), before)

    def test_delivery_post_command_snapshot_audit_rejects_fake_runner_mutation(self):
        """A successful child return cannot publish after changing inputs."""
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, *_ = self._runtime_fixture(temporary)
            source_root = root / isolated.SOURCE_SNAPSHOT_DIRNAME
            source_file = source_root / "packages/source.c"
            original_mode = source_file.stat().st_mode & 0o777
            command_root = root / "fake-delivery-command"
            command_root.mkdir()
            log = command_root / "command.log"
            env = {"PATH": os.defpath, "LANG": "C", "LC_ALL": "C"}
            actual = ["python3", "fake-tool"]
            command = isolated._make_delivery_command(
                "fake-postbuild", "python3", actual, command_root, env, "none")

            def tampering_runner(argv, **kwargs):
                os.chmod(source_file, original_mode | 0o200)
                source_file.write_bytes(source_file.read_bytes() + b"tampered\n")
                os.chmod(source_file, original_mode)
                return subprocess.CompletedProcess(argv, 0, stdout="ok\n", stderr="")

            isolated._run_delivery_command(
                command, actual, env, command_root, log, runner=tampering_runner)
            with self.assertRaises(isolated.RuntimeDeliveryError):
                isolated._assert_delivery_snapshot_unchanged(
                    source_root,
                    materialized["source_view"]["snapshot_identity_sha256"],
                    "fake-delivery")
            self.assertEqual(json.loads(output.read_text())["phase"], "materialized")

    def test_delivery_redaction_rejects_secret_marker_in_nonkey_argument(self):
        with self.assertRaises(isolated.IsolatedExecutorError):
            isolated._redacted_delivery_argv(
                ["python3", "tool.py", "--key", isolated.PRIVATE_KEY_TOKEN,
                 "--output", "/tmp/forged.pem"], "forged")

    def test_delivery_canonical_contract_rejects_argv_and_environment_forgery(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cwd = root / "cwd"
            logs = root / "logs"
            cwd.mkdir()
            logs.mkdir()
            expected = isolated._make_delivery_command(
                "fake-stage", "python3", ["python3", "tool.py", "--out", "out.bin"],
                cwd, {"PATH": os.defpath, "LANG": "C", "LC_ALL": "C"}, "none")
            expected.update({
                "status": "PASS", "returncode": 0,
                "log": str(logs / "00-fake-stage.log"),
            })
            observed = copy.deepcopy(expected)
            with self.assertRaises(isolated.IsolatedExecutorError):
                forged = copy.deepcopy(observed)
                forged["argv"][-1] = "forged.bin"
                isolated._assert_delivery_command_canonical(forged, expected, root)
            with self.assertRaises(isolated.IsolatedExecutorError):
                forged = copy.deepcopy(observed)
                forged["environment"]["BK7258_PARTITION_LAYOUT_ID"] = "forged"
                isolated._assert_delivery_command_canonical(forged, expected, root)

    def test_prepare_delivery_requires_runtime_built_manifest_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, prepared = self._prepare(temporary, "t5ai_core_bringup")
            with self.assertRaises(isolated.RuntimeDeliveryError):
                isolated.prepare_delivery(REPOSITORY, output)
            self.assertFalse((root / "delivery").exists())

    def test_prepare_delivery_rejects_raw_runtime_product(self):
        with tempfile.TemporaryDirectory() as temporary:
            root, output, materialized, fake_cmake, fake_olddefconfig, kconfiglib_root, fake_make = \
                self._runtime_fixture(temporary, product="t5ai_core_bringup")
            prepared_plan = json.loads(Path(materialized["plan_copy"]).read_text())
            real_build_plan = isolated.framework.build_plan

            def fixture_build_plan(repository: Path, *args, **kwargs):
                if repository.resolve() == (root / "source-snapshot/contest2026_135_yongwangzhiqian").resolve():
                    return copy.deepcopy(prepared_plan)
                return real_build_plan(repository, *args, **kwargs)

            with mock.patch.object(isolated.framework, "build_plan", side_effect=fixture_build_plan):
                isolated.compile_runtime(
                    REPOSITORY, output, authorize_compile=True,
                    cmake_executable=fake_cmake,
                    python_executable=Path(sys.executable).resolve(),
                    olddefconfig_executable=fake_olddefconfig,
                    kconfiglib_root=kconfiglib_root, make_executable=fake_make)
            with self.assertRaises(isolated.RuntimeDeliveryError):
                isolated.prepare_delivery(REPOSITORY, output)
            self.assertFalse((root / "delivery").exists())


if __name__ == "__main__":
    unittest.main()
