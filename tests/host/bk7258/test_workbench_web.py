#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local HTTP boundary and single-owner jobs; no physical port is opened."""
import argparse
import importlib
import json
import http.client
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))


class WebTest(unittest.TestCase):
    def setUp(self):
        self.m = importlib.import_module("_lib.workbench_web")
        self.temp = tempfile.TemporaryDirectory()
        self.service = self.m.Service(
            "fixture-native", Path("fixture.profile"), Path(self.temp.name) / "jobs", 10
        )
        self.server = self.m.Server(self.service, 0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.service.close()
        self.thread.join(3)
        self.temp.cleanup()

    def request(self, method, path, body=None, **headers):
        connection = http.client.HTTPConnection(
            "127.0.0.1", self.server.server_port, timeout=3
        )
        data = None if body is None else json.dumps(body)
        defaults = {
            "Authorization": "Bearer " + self.server.token,
            "Origin": self.server.origin,
            "Content-Type": "application/json",
        }
        defaults.update(headers)
        connection.request(method, path, data, defaults)
        response = connection.getresponse()
        self.response_headers = dict(response.getheaders())
        result = response.status, response.read()
        connection.close()
        return result

    def wait_result(self):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            code, raw = self.request("GET", "/api/state")
            self.assertEqual(code, 200)
            value = json.loads(raw)
            if value["job"] and value["job"]["phase"] != "running":
                return value["job"]
            time.sleep(0.005)
        self.fail("job did not terminate")

    def test_http_authority_rejects_before_operation(self):
        with patch.object(self.m.workbench, "run") as run:
            for headers in [
                {"Authorization": ""},
                {"Origin": "https://other.invalid"},
                {"Host": "other.invalid"},
                {"Sec-Fetch-Site": "cross-site"},
            ]:
                self.assertEqual(
                    self.request(
                        "POST",
                        "/api/start",
                        {"id": "01" * 16, "operation": "status", "params": {}},
                        **headers
                    )[0],
                    403,
                )
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": "02" * 16, "operation": "shell", "params": {}},
                )[0],
                400,
            )
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {
                        "id": "02" * 16,
                        "operation": "status",
                        "params": {"port": "evil"},
                    },
                )[0],
                400,
            )
            run.assert_not_called()

    def test_polling_is_local_and_duplicate_is_not_replayed(self):
        entered, release = threading.Event(), threading.Event()

        def run(args, **hooks):
            entered.set()
            release.wait(2)
            return {"ready": True}

        with patch.object(self.m.workbench, "run", side_effect=run) as operation:
            body = {"id": "03" * 16, "operation": "status", "params": {}}
            self.assertEqual(self.request("POST", "/api/start", body)[0], 202)
            self.assertTrue(entered.wait(1))
            for unused in range(20):
                self.assertEqual(self.request("GET", "/api/state")[0], 200)
            self.assertEqual(self.request("POST", "/api/start", body)[0], 202)
            self.assertEqual(
                self.request("POST", "/api/start", dict(body, id="04" * 16))[0], 409
            )
            self.assertEqual(
                self.request("POST", "/api/start", dict(body, operation="info"))[0], 409
            )
            release.set()
            self.assertEqual(self.wait_result()["result"], {"ready": True})
            self.assertEqual(self.request("POST", "/api/start", body)[0], 202)
            self.assertEqual(operation.call_count, 1)

    def test_cancel_is_intent_not_remote_completion(self):
        entered, release = threading.Event(), threading.Event()

        def run(args, **hooks):
            hooks["observe"]({"state": "receiving", "written": 4, "total": 128})
            entered.set()
            release.wait(2)
            self.assertTrue(hooks["cancel_requested"]())
            raise ValueError("PRIVATE detail must never reach HTTP")

        import base64

        body = {
            "id": "05" * 16,
            "operation": "resource-upload",
            "params": {"data": base64.b64encode(bytes(128)).decode(), "ttl_ms": 5000},
        }
        with patch.object(self.m.workbench, "run", side_effect=run):
            self.assertEqual(self.request("POST", "/api/start", body)[0], 202)
            self.assertTrue(entered.wait(1))
            self.assertEqual(
                self.request("POST", "/api/cancel", {"id": "05" * 16})[0], 202
            )
            value = json.loads(self.request("GET", "/api/state")[1])["job"]
            self.assertEqual(value["phase"], "running")
            self.assertEqual(value["snapshot"]["state"], "receiving")
            self.assertTrue(value["cancel_requested"])
            release.set()
            result = self.wait_result()
            self.assertEqual(result["phase"], "unconfirmed")
            self.assertNotIn("PRIVATE", json.dumps(result))

    def test_page_headers_and_no_credential_paths(self):
        status, page = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertEqual(self.response_headers["Cache-Control"], "no-store")
        self.assertEqual(self.response_headers["X-Content-Type-Options"], "nosniff")
        self.assertIn(
            "frame-ancestors 'none'", self.response_headers["Content-Security-Policy"]
        )
        self.assertNotIn("Access-Control-Allow-Origin", self.response_headers)
        self.assertNotIn(self.server.token.encode(), page)
        self.assertNotIn(b"fixture.profile", page)
        self.assertEqual(self.request("GET", "/../../fixture.profile")[0], 404)
        self.assertEqual(self.request("GET", "/api/state", Authorization="")[0], 403)

    def test_invalid_upload_does_not_open_device(self):
        with patch.object(self.m.workbench, "run") as run:
            for data in ["!", "YQ==", "A" * 200000]:
                body = {
                    "id": "06" * 16,
                    "operation": "resource-upload",
                    "params": {"data": data, "ttl_ms": 5000},
                }
                self.assertIn(self.request("POST", "/api/start", body)[0], (400, 413))
            run.assert_not_called()

    def test_http_upload_reaches_tls_and_native_installer(self):
        import base64
        import contextlib
        import ssl
        import struct
        from test_workbench_client import TlsPeer, WorkbenchClientTest
        from test_workbench_resource_flow import Pipe, PACK

        class NativePeer(TlsPeer):
            def __init__(self, certificate, key):
                super().__init__(certificate, key)
                self.native = Pipe()

            def step(self):
                if not self.handshaken:
                    try:
                        self.tls.do_handshake()
                        self.handshaken = True
                    except ssl.SSLWantReadError:
                        return
                try:
                    self.plain.extend(self.tls.read(4096))
                except ssl.SSLWantReadError:
                    return
                while len(self.plain) >= 16:
                    size = struct.unpack_from(">I", self.plain, 12)[0]
                    if len(self.plain) < 16 + size:
                        return
                    frame = bytes(self.plain[: 16 + size])
                    del self.plain[: 16 + size]
                    self.native.write(frame)
                    reply = self.native.read(40)
                    if len(reply) != 40:
                        raise ValueError("Native peer closed")
                    self.tls.write(reply)

            def close(self):
                if not self.closed:
                    self.native.close()
                super().close()

        WorkbenchClientTest.setUpClass()
        peer = NativePeer(WorkbenchClientTest.cert, WorkbenchClientTest.key)

        @contextlib.contextmanager
        def material(args):
            yield WorkbenchClientTest.pem, WorkbenchClientTest.pin, bytearray(
                b"\x2a" + bytes(31)
            )

        try:
            with patch.object(
                self.m.workbench, "SerialChannel", return_value=peer
            ), patch.object(self.m.workbench, "_credentials", material):
                body = {
                    "id": "08" * 16,
                    "operation": "resource-upload",
                    "params": {
                        "data": base64.b64encode(PACK.read_bytes()).decode(),
                        "ttl_ms": 5000,
                    },
                }
                self.assertEqual(self.request("POST", "/api/start", body)[0], 202)
                value = self.wait_result()
                self.assertEqual(value["phase"], "returned")
                self.assertTrue(value["result"]["installed"])
                self.assertEqual(value["result"]["written"], PACK.stat().st_size)
                self.assertNotIn("activated", value["result"])
                self.assertTrue(peer.closed)
                self.assertTrue(
                    (self.service.directory / ("08" * 16 + ".json")).is_file()
                )
        finally:
            peer.close()
            WorkbenchClientTest.tearDownClass()

    def test_large_counters_preserve_exact_value(self):
        with patch.object(
            self.m.workbench, "run", return_value={"revision": 2**64 - 1}
        ):
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": "07" * 16, "operation": "default-status", "params": {}},
                )[0],
                202,
            )
            self.assertEqual(self.wait_result()["result"]["revision"], str(2**64 - 1))


if __name__ == "__main__":
    unittest.main()
