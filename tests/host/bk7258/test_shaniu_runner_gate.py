#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""S0: Gradle success is not a collected, passing JUnit result.

The external Gradle command and report files are fixtures. The real collector
and gate must reject bad evidence without running Gradle or product logic.
"""
import json
import hashlib
import os
import re
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

import test_shaniu_contracts as runner
import test_pack_trial as pack_trial

JVM_IDS = [ident for ident in runner.REQUIRED if ident.startswith(("ota.", "provision."))]


class CiContractGateTest(unittest.TestCase):
    def workflow(self):
        import yaml
        path = runner.ROOT / ".github/workflows/shaniu-source-checks.yml"
        return yaml.load(path.read_text(), Loader=yaml.BaseLoader)

    def test_contract_inputs_trigger_formal_workflow(self):
        import fnmatch
        paths = self.workflow()["on"]["push"]["paths"]
        for relative in (
            "tests/host/bk7258/acceptance/contracts.md",
            "tests/host/bk7258/acceptance/required-units.v1.json",
            "tests/host/bk7258/test_shaniu_contracts.py",
            "tests/host/bk7258/test_shaniu_runner_gate.py",
            "tests/host/bk7258/mocks/nuttx/config.h",
        ):
            self.assertTrue(any(fnmatch.fnmatchcase(relative, p) for p in paths), relative)

    def test_contract_artifact_keeps_junit_evidence(self):
        import fnmatch
        uploads = [step for job in self.workflow()["jobs"].values()
                   for step in job["steps"] if "upload-artifact@" in step.get("uses", "")
                   and "contract" in step.get("with", {}).get("name", "")]
        self.assertTrue(uploads)
        for upload in uploads:
            paths = upload["with"]["path"].splitlines()
            self.assertTrue(any(fnmatch.fnmatchcase(
                "TEST-com.shaniu.companion.ota.OtaControlUploadTest.xml",
                path.rsplit("/", 1)[-1]) for path in paths), paths)

    def test_native_serial_dependency_precedes_contract_collection(self):
        jobs = self.workflow()["jobs"]
        checked = 0
        for job in jobs.values():
            commands = "\n".join(step.get("run", "") for step in job["steps"])
            if "run-shaniu-contracts" not in commands:
                continue
            before = commands.split("run-shaniu-contracts", 1)[0]
            # The native adapter is a required selected case, even though
            # serial hardware itself is an external peer in the host suite.
            self.assertRegex(before, r"pip install[^\n]*(?:\\\n[^\n]*)*pyserial==[0-9.]+")
            checked += 1
        self.assertGreater(checked, 0)

    def test_contracts_and_collector_selftest_are_enforced_with_evidence(self):
        jobs = self.workflow()["jobs"]
        for command in ("run-shaniu-contracts", "test_shaniu_runner_gate.py"):
            matches = [(job, step) for job in jobs.values() for step in job["steps"]
                       if command in step.get("run", "")]
            self.assertTrue(matches, command)
            for job, step in matches:
                self.assertNotEqual(job.get("continue-on-error"), "true")
                self.assertNotEqual(step.get("continue-on-error"), "true")
                self.assertNotIn("|| true", step["run"])
                self.assertIn("set -e", step["run"])
                self.assertTrue(any(s.get("if") == "always()" and
                    "upload-artifact@" in s.get("uses", "") and
                    "contract" in s.get("with", {}).get("name", "")
                    for s in job["steps"]))


class JunitGateTest(unittest.TestCase):
    def collect(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="shaniu-gate-") as directory:
            root = Path(directory)
            reports = root / "android/shaniu-companion/app/build/test-results/testDebugUnitTest"
            reports.mkdir(parents=True)
            out = root / "out"
            out.mkdir()
            by_class = {}
            for ident in JVM_IDS:
                name, method = ident.rsplit(".", 1)
                by_class.setdefault(name, []).append(method)
            for name, methods in by_class.items():
                suite = ET.Element("testsuite", name="com.shaniu.companion." + name,
                                   tests=str(len(methods)), failures="0", errors="0", skipped="0")
                for method in methods:
                    ET.SubElement(suite, "testcase", name=method,
                                  classname="com.shaniu.companion." + name, time="0.001")
                path = reports / ("TEST-com.shaniu.companion." + name + ".xml")
                path.write_bytes(ET.tostring(suite))
                # Files represent a report produced during the mocked command.
                os.utime(path, (time.time() + 10, time.time() + 10))
            selected = reports / "TEST-com.shaniu.companion.ota.OtaControlUploadTest.xml"
            if fault == "missing":
                selected.unlink()
            elif fault == "stale":
                os.utime(selected, (1, 1))
            elif fault == "corrupt":
                selected.write_text("<testsuite><")
                os.utime(selected, (time.time() + 10, time.time() + 10))
            elif fault in ("zero", "skip", "missing_case", "duplicate", "unexpected", "error"):
                tree = ET.parse(selected)
                suite = tree.getroot()
                if fault == "zero":
                    for node in list(suite):
                        suite.remove(node)
                    suite.set("tests", "0")
                elif fault == "skip":
                    ET.SubElement(suite.find("testcase"), "skipped")
                    suite.set("skipped", "1")
                elif fault == "missing_case":
                    suite.remove(suite.find("testcase"))
                    suite.set("tests", str(len(suite.findall("testcase"))))
                elif fault == "unexpected":
                    ET.SubElement(suite, "testcase", name="unregisteredRegression",
                                  classname=suite.attrib["name"], time="0.001")
                    suite.set("tests", str(len(suite.findall("testcase"))))
                elif fault == "duplicate":
                    suite.append(ET.fromstring(ET.tostring(suite.find("testcase"))))
                    suite.set("tests", str(len(suite.findall("testcase"))))
                else:
                    ET.SubElement(suite.find("testcase"), "error", type="RuntimeError")
                    suite.set("errors", "1")
                tree.write(selected)
                os.utime(selected, (time.time() + 10, time.time() + 10))
            with patch.object(runner, "ROOT", root), patch.object(runner, "OUT", out), \
                 patch.object(runner, "RESULTS", []), patch.object(runner, "BUILDS", []), \
                 patch.object(runner, "command", return_value=(0, 0.1)):
                code = runner.run_jvm()
                return code, list(runner.RESULTS)

    def test_complete_collection_can_pass(self):
        code, results = self.collect()
        self.assertEqual(code, 0)
        self.assertEqual({r["id"] for r in results}, set(JVM_IDS))
        self.assertTrue(all(r["status"] == "PASS" for r in results))

    def test_gradle_zero_missing_xml_fails(self):
        self.assertNotEqual(self.collect("missing")[0], 0)

    def test_gradle_zero_stale_xml_fails(self):
        self.assertNotEqual(self.collect("stale")[0], 0)

    def test_gradle_zero_corrupt_xml_is_setup_error(self):
        code, results = self.collect("corrupt")
        self.assertNotEqual(code, 0)
        self.assertTrue(any(r["status"] == "SETUP_ERROR" for r in results))

    def test_gradle_zero_empty_suite_fails(self):
        self.assertNotEqual(self.collect("zero")[0], 0)

    def test_required_skipped_case_fails(self):
        self.assertNotEqual(self.collect("skip")[0], 0)

    def test_missing_required_method_fails(self):
        self.assertNotEqual(self.collect("missing_case")[0], 0)

    def test_duplicate_method_is_not_complete_collection(self):
        self.assertNotEqual(self.collect("duplicate")[0], 0)

    def test_extra_passing_method_is_rejected(self):
        code, results = self.collect("unexpected")
        self.assertNotEqual(code, 0)
        self.assertTrue(all(r["status"] == "PASS" for r in results))
        self.assertIn(
            "unexpected: ota.OtaControlUploadTest.unregisteredRegression",
            runner.collection_errors(results, JVM_IDS),
        )

    def test_junit_error_is_not_pass(self):
        code, results = self.collect("error")
        self.assertNotEqual(code, 0)
        self.assertTrue(any(r["status"] == "SETUP_ERROR" for r in results))


class SelectedCollectionGateTest(unittest.TestCase):
    def test_complete_selected_set_ignores_future_specifications(self):
        self.assertEqual(runner.collection_errors([dict(id="selected", status="PASS")], ["selected"]), [])

    def test_missing_skipped_error_and_duplicate_are_failures(self):
        for results in ([], [dict(id="selected", status="NOT_RUN")],
                        [dict(id="selected", status="SETUP_ERROR")],
                        [dict(id="selected", status="FAIL_ASSERTION")],
                        [dict(id="selected", status="PASS")] * 2):
            with self.subTest(results=results):
                self.assertTrue(runner.collection_errors(results, ["selected"]))

    def test_report_groups_partition_selected_units(self):
        baseline = runner.SELECTION["baseline_ids"]
        added = runner.SELECTION["added_ids"]
        self.assertEqual(len(baseline), 63)
        self.assertFalse(set(baseline) & set(added))
        self.assertCountEqual(baseline + added, runner.REQUIRED)

    def test_selected_lifecycle_kotlin_methods_match_registration(self):
        source_root = runner.ROOT / "android/shaniu-companion/app/src/test/java/com/shaniu/companion"
        for name in ("provision.DeviceControlSessionTest", "ota.OtaSourceLeaseTest"):
            with self.subTest(name=name):
                source = (source_root / (name.replace(".", "/") + ".kt")).read_text()
                methods = re.findall(r"@Test(?:\([^)]*\))?\s+fun\s+(\w+)\s*\(", source)
                self.assertTrue(methods)
                # Fail closed if a new annotation shape needs parser support.
                self.assertEqual(len(methods), len(re.findall(r"(?m)^\s*@Test\b", source)))
                self.assertCountEqual(
                    [name + "." + method for method in methods],
                    [ident for ident in runner.REQUIRED if ident.startswith(name + ".")],
                )

    def test_lifecycle_host_methods_are_collected_and_registered(self):
        import importlib
        with patch.object(runner, "add") as add:
            runner.add_lifecycle_regressions(unittest.TestSuite())
        cases = [call.args for call in add.call_args_list]
        self.assertEqual(len(cases), 21)
        ids = [case[1] for case in cases]
        prefixes = {ident.rsplit(".", 1)[0] + "." for ident in ids}
        self.assertCountEqual(ids, [ident for ident in runner.REQUIRED
                                   if any(ident.startswith(prefix) for prefix in prefixes)])
        by_class = {}
        for _, _, _, _, command in cases:
            module = Path(command[1]).stem
            cls, method = command[2].split(".")
            by_class.setdefault((module, cls), []).append(method)
        for (module, cls), methods in by_class.items():
            with self.subTest(module=module, cls=cls):
                test_class = getattr(importlib.import_module(module), cls)
                self.assertCountEqual(methods, unittest.defaultTestLoader.getTestCaseNames(test_class))

    def test_empty_selection_cannot_pass(self):
        self.assertTrue(runner.collection_errors([], []))


class ColdFixtureGateTest(unittest.TestCase):
    def test_pack_trial_tls_build_is_shared_by_selected_cases(self):
        with tempfile.TemporaryDirectory(prefix="shaniu-pack-tls-") as directory, \
             patch.object(runner, "OUT", Path(directory)), \
             patch.object(runner, "build", return_value=True) as build, \
             patch.dict(os.environ, {}, clear=False):
            os.environ.pop("SHANIU_PACK_TRIAL_TLS_BUILD", None)
            self.assertTrue(runner.prepare_pack_trial_tls())
            build.assert_called_once()
            command = [str(value) for value in build.call_args.args[0]]
            self.assertEqual(command[0], sys.executable)
            self.assertEqual(command[1], str(runner.HERE / "test_pack_trial.py"))
            self.assertEqual(command[2], "build-tls")
            shared = Path(os.environ["SHANIU_PACK_TRIAL_TLS_BUILD"])
            self.assertEqual(shared, Path(directory) / "pack-trial-tls")

    def test_pack_trial_uses_shared_tls_build_and_incremental_gradle(self):
        shared = Path("/tmp/shaniu-pack-trial-shared")
        with patch.dict(os.environ, {"SHANIU_PACK_TRIAL_TLS_BUILD": str(shared)}):
            self.assertEqual(pack_trial.tls_build_path(Path("/tmp/per-case")), shared)
        command = pack_trial.android_gradle_command("example.Test", "method")
        self.assertIn("--tests", command)
        self.assertIn("example.Test.method", command)
        self.assertIn("--no-build-cache", command)
        self.assertNotIn("--rerun-tasks", command)

    def test_pack_trial_invalidates_only_selected_junit_evidence(self):
        with tempfile.TemporaryDirectory(prefix="shaniu-junit-refresh-") as directory:
            root = Path(directory)
            report = root / "reports" / "TEST-example.Test.xml"
            compiled = root / "classes" / "Example.class"
            report.parent.mkdir()
            compiled.parent.mkdir()
            report.write_text("old report")
            compiled.write_bytes(b"compiled input")
            pack_trial.invalidate_junit_report(report)
            self.assertFalse(report.exists())
            self.assertEqual(compiled.read_bytes(), b"compiled input")

    def test_timeout_quiesces_descendants_and_freezes_evidence(self):
        with tempfile.TemporaryDirectory(prefix="shaniu-command-tree-") as directory:
            out = Path(directory)
            child = out / "child.py"
            child.write_text(
                "import subprocess,sys,time\n"
                "subprocess.Popen([sys.executable, '-c', "
                "'import time; time.sleep(0.25); print(\\\"LATE_SENTINEL\\\", flush=True)'])\n"
                "time.sleep(5)\n"
            )
            with patch.object(runner, "OUT", out):
                code, _ = runner.command(
                    [sys.executable, child], "tree.log", timeout=0.05
                )
            self.assertEqual(code, 124)
            before = hashlib.sha256((out / "tree.log").read_bytes()).hexdigest()
            time.sleep(0.4)
            data = (out / "tree.log").read_bytes()
            self.assertNotIn(b"LATE_SENTINEL", data)
            self.assertEqual(before, hashlib.sha256(data).hexdigest())


class NestedTlsGateTest(unittest.TestCase):
    def test_compiler_failure_is_error_not_assertion(self):
        import subprocess
        import test_provision_tls as tls

        with tempfile.TemporaryDirectory(prefix="tls-gate-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("")

            def external(args, **kwargs):
                failed = args[0] == "cc"
                if failed:
                    kwargs["stdout"].write("compiler error: Assertion symbol missing\n")
                return subprocess.CompletedProcess(args, 1 if failed else 0)

            with patch.dict(os.environ, {
                "MBEDTLS_SOURCE": str(root),
                "SHANIU_TLS_FAILURE_DIR": str(root / "failures"),
            }), patch.object(tls.subprocess, "run", side_effect=external):
                result = unittest.TestResult()
                tls.ProvisionTlsTest(
                    "test_real_tls_fragmentation_and_teardown"
                ).run(result)
            self.assertEqual(len(result.errors), 1)
            self.assertEqual(len(result.failures), 0)

    def test_invalid_positive_certificate_stops_before_peer(self):
        import subprocess
        import test_provision_tls as tls
        with tempfile.TemporaryDirectory(prefix="tls-time-gate-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("")
            verify_calls, peer_calls = [], []
            def external(args, **kwargs):
                if str(args[0]) == "openssl" and str(args[1]) == "verify":
                    verify_calls.append(args)
                    kwargs["stdout"].write("certificate is not yet valid\n")
                    return subprocess.CompletedProcess(args, 1)
                if len(args) > 1 and str(args[1]).endswith("test_workbench_native_tls.py"):
                    peer_calls.append(args)
                return subprocess.CompletedProcess(args, 0)
            with patch.dict(os.environ, {"MBEDTLS_SOURCE": str(root),
                "SHANIU_TLS_FAILURE_DIR": str(root / "failures")}), \
                patch.object(tls, "RESOURCE_CASE", "upload"), \
                patch.object(tls.subprocess, "run", side_effect=external):
                result = unittest.TestResult()
                tls.ProvisionTlsTest("test_real_tls_fragmentation_and_teardown").run(result)
            self.assertEqual(len(verify_calls), 1)
            self.assertEqual(peer_calls, [])
            self.assertEqual(len(result.errors), 1)
            self.assertEqual(len(result.failures), 0)

    def test_native_peer_setup_error_remains_setup_error(self):
        import subprocess
        import test_provision_tls as tls

        with tempfile.TemporaryDirectory(prefix="native-tls-gate-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("")

            def external(args, **kwargs):
                failed = len(args) > 1 and str(args[1]).endswith("test_workbench_native_tls.py")
                if failed:
                    kwargs["stdout"].write("fixture setup error: Assertion in diagnostic\n")
                return subprocess.CompletedProcess(args, 2 if failed else 0)

            with patch.dict(os.environ, {"MBEDTLS_SOURCE": str(root),
                "SHANIU_TLS_FAILURE_DIR": str(root / "failures")}), \
                patch.object(tls, "RESOURCE_CASE", "upload"), \
                patch.object(tls.subprocess, "run", side_effect=external):
                result = unittest.TestResult()
                tls.ProvisionTlsTest("test_real_tls_fragmentation_and_teardown").run(result)
            self.assertEqual(len(result.errors), 1)
            self.assertEqual(len(result.failures), 0)

    def test_nested_setup_exit_overrides_assertion_text(self):
        with tempfile.TemporaryDirectory(prefix="tls-gate-") as directory:
            out = Path(directory)
            (out / "nested.log").write_text("compiler: Assertion symbol missing\n")
            with patch.object(runner, "OUT", out), patch.object(runner, "RESULTS", []), \
                 patch.object(runner, "command", return_value=(2, 0.1)):
                with self.assertRaises(RuntimeError):
                    runner.case("nested", "NET-03", "L2", [], marker=False,
                                setup_exit_code=2)
                self.assertEqual(runner.RESULTS[0]["status"], "SETUP_ERROR")


    def test_missing_tls_source_process_returns_setup_code(self):
        import subprocess
        import sys
        with tempfile.TemporaryDirectory(prefix="tls-missing-") as directory:
            result = subprocess.run(
                [sys.executable, str(Path(__file__).with_name("test_provision_tls.py"))],
                env={**os.environ, "MBEDTLS_SOURCE": directory},
                capture_output=True, text=True,
            )
        self.assertEqual(result.returncode, 2)
        self.assertIn("mbedTLS source is unavailable", result.stderr)

    def test_nested_assertion_and_success_keep_their_status(self):
        for code, output, expected in (
            (1, "AssertionError: product invariant", "FAIL_ASSERTION"),
            (0, "OK", "PASS"),
        ):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as directory:
                out = Path(directory)
                (out / "nested.log").write_text(output)
                with patch.object(runner, "OUT", out), patch.object(runner, "RESULTS", []), \
                     patch.object(runner, "command", return_value=(code, 0.1)):
                    if code:
                        with self.assertRaises(AssertionError):
                            runner.case("nested", "NET-03", "L2", [], marker=False,
                                        setup_exit_code=2)
                    else:
                        runner.case("nested", "NET-03", "L2", [], marker=False,
                                    setup_exit_code=2)
                    self.assertEqual(runner.RESULTS[0]["status"], expected)


if __name__ == "__main__":
    unittest.main()
