#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit Windows DPAPI + JVM codec + actual TLS host integration, no device."""
import contextlib
import hashlib
import importlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch
import test_workbench_client as peer

pairing = importlib.import_module("_lib.workbench_pairing")
profile = importlib.import_module("_lib.workbench_profile")
ROOT = Path(__file__).resolve().parents[3]


class PairingInteropTest(unittest.TestCase):
    def test_jvm_envelope_dpapi_profile_and_real_tls(self):
        out = Path(os.environ["SHANIU_PAIR_INTEROP_OUT"]).resolve()
        out.mkdir(parents=True, exist_ok=True)
        peer.WorkbenchClientTest.setUpClass()
        self.addCleanup(peer.WorkbenchClientTest.tearDownClass)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            request, pending, response, saved = [
                root / x
                for x in ("request.spq", "pending.spp", "response.spr", "device.spc")
            ]
            now = 1800000000000
            pairing.start(request, pending, 3, now_ms=now)
            env = dict(
                os.environ,
                SHANIU_PC_PAIR_REQUEST=str(request),
                SHANIU_PC_PAIR_RESPONSE=str(response),
                SHANIU_PC_PAIR_CERT=str(peer.WorkbenchClientTest.cert),
                SHANIU_PC_PAIR_NOW=str(now),
            )
            app = ROOT / "android/shaniu-companion"
            report = (
                app
                / "app/build/test-results/testDebugUnitTest/TEST-com.shaniu.companion.provision.PcPairingExchangeTest.xml"
            )
            report.unlink(missing_ok=True)
            with (out / "jvm.log").open("w") as log:
                result = subprocess.run(
                    [
                        "./gradlew",
                        ":app:testDebugUnitTest",
                        "--offline",
                        "--rerun-tasks",
                        "--tests",
                        "com.shaniu.companion.provision.PcPairingExchangeTest.hostInteropUsesProductionCodecWhenExplicitlySelected",
                    ],
                    cwd=app,
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=180,
                )
            self.assertEqual(result.returncode, 0)
            xml = report.read_bytes()
            (out / report.name).write_bytes(xml)
            cases = ET.fromstring(xml).findall("testcase")
            self.assertEqual(len(cases), 1)
            self.assertEqual(
                cases[0].get("name"),
                "hostInteropUsesProductionCodecWhenExplicitlySelected",
            )
            self.assertFalse(
                any(
                    cases[0].find(x) is not None
                    for x in ("failure", "error", "skipped")
                )
            )
            imported = pairing.finish(
                pending,
                response.read_bytes(),
                saved,
                peer.WorkbenchClientTest.pin,
                now_ms=now + 1,
            )
            self.assertFalse(imported["device_authorization_verified"])
            self.assertNotIn(peer.PC_KEY, saved.read_bytes())
            tls = peer.TlsPeer(
                peer.WorkbenchClientTest.cert, peer.WorkbenchClientTest.key
            )
            output = io.StringIO()
            with patch.object(
                peer.workbench, "SerialChannel", return_value=tls
            ), contextlib.redirect_stdout(output):
                code = importlib.import_module("bk7258").main(
                    ["workbench", "status", "--port", "NATIVE", "--profile", str(saved)]
                )
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(output.getvalue())["volume"], 37)
            self.assertEqual([r[0] for r in tls.requests], [1, 2])
            (out / "public-evidence.json").write_text(
                json.dumps(
                    {
                        "request_sha256": hashlib.sha256(
                            request.read_bytes()
                        ).hexdigest(),
                        "response_sha256": hashlib.sha256(
                            response.read_bytes()
                        ).hexdigest(),
                        "protection": "Windows CurrentUser DPAPI",
                        "peer": "synthetic external TLS peer",
                        "device_operations": 0,
                        "result": "PASS",
                    },
                    indent=2,
                )
                + "\n"
            )


if __name__ == "__main__":
    unittest.main()
