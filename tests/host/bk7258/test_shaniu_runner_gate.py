#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""S0: Gradle success is not a collected, passing JUnit result.

The external Gradle command and report files are fixtures. The real collector
and gate must reject bad evidence without running Gradle or product logic.
"""
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

import test_shaniu_contracts as runner

JVM_IDS = [ident for ident in runner.REQUIRED if ident.startswith(("ota.", "provision."))]


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
            elif fault in ("zero", "skip", "missing_case", "duplicate", "error"):
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

    def test_empty_selection_cannot_pass(self):
        self.assertTrue(runner.collection_errors([], []))


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
