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

BASELINE = json.loads((runner.HERE / "acceptance/baseline-20260924.json").read_text())
JVM_IDS = [r["id"] for r in BASELINE["collected"] if r["id"].startswith(("ota.", "provision."))]


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

    def test_empty_selection_cannot_pass(self):
        self.assertTrue(runner.collection_errors([], []))


if __name__ == "__main__":
    unittest.main()
