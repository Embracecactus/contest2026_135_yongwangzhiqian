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
from types import SimpleNamespace
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
                        **headers,
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

    def test_camera_state_is_metadata_only(self):
        def run(args, **hooks):
            if args.operation == "camera-capture":
                self.assertTrue(args.camera_preview)
                return {
                    "state": "ready",
                    "image_base64": "synthetic-fixture",
                    "valid": True,
                    "release_confirmed": True,
                }
            return {"state": "canceled"}

        with patch.object(self.m.workbench, "run", side_effect=run):
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": "c1" * 16, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            for unused in range(3):
                result = json.loads(self.request("GET", "/api/state")[1])["job"][
                    "result"
                ]
                self.assertNotIn("image_base64", result)
                self.assertTrue(result["preview_available"])
            self.assertEqual(list(self.service.directory.iterdir()), [])

    def test_camera_preview_rejects_bad_or_unauthorized_reads_without_operation(self):
        capture_id = "c1" * 16
        with patch.object(self.m.workbench, "run") as run:
            for body, expected in (
                ({}, 400),
                ({"id": "not-a-capture-id"}, 400),
                ({"id": capture_id}, 410),
            ):
                with self.subTest(body=body):
                    self.assertEqual(
                        self.request("POST", "/api/camera-preview", body)[0], expected
                    )
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/camera-preview",
                    {"id": capture_id},
                    Authorization="",
                )[0],
                403,
            )
            run.assert_not_called()

    def test_camera_preview_is_read_through_and_survives_status_and_cancel(self):
        first, second = "c2" * 16, "c3" * 16
        captures = 0

        def run(args, **hooks):
            nonlocal captures
            if args.operation == "camera-capture":
                captures += 1
                return {
                    "state": "ready",
                    "image_base64": "first" if captures == 1 else "second",
                    "valid": True,
                    "release_confirmed": True,
                }
            return {"state": "canceled", "release_confirmed": True}

        with patch.object(self.m.workbench, "run", side_effect=run) as operation:
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": first, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            self.wait_result()
            for unused in range(2):
                code, raw = self.request("POST", "/api/camera-preview", {"id": first})
                self.assertEqual(code, 200)
                self.assertEqual(json.loads(raw)["image_base64"], "first")
            self.assertEqual(operation.call_count, 1)
            for operation_name in ("camera-status", "camera-cancel"):
                self.assertEqual(
                    self.request(
                        "POST",
                        "/api/start",
                        {
                            "id": ("d0" if operation_name.endswith("status") else "d1")
                            * 16,
                            "operation": operation_name,
                            "params": {},
                        },
                    )[0],
                    202,
                )
                self.wait_result()
                self.assertEqual(
                    self.request("POST", "/api/camera-preview", {"id": first})[0], 200
                )
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": second, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            self.wait_result()
            self.assertEqual(
                self.request("POST", "/api/camera-preview", {"id": first})[0], 410
            )
            code, raw = self.request("POST", "/api/camera-preview", {"id": second})
            self.assertEqual(code, 200)
            self.assertEqual(json.loads(raw)["id"], second)

    def test_camera_preview_ttl_and_close_discard_only_memory_copy(self):
        now = [1000.0]
        capture_id = "c4" * 16

        def run(args, **hooks):
            return {
                "state": "ready",
                "image_base64": "fixture",
                "valid": True,
                "release_confirmed": True,
            }

        clock = SimpleNamespace(monotonic=lambda: now[0])
        with patch.object(self.m, "time", clock, create=True), patch.object(
            self.m.workbench, "run", side_effect=run
        ):
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": capture_id, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            self.wait_result()
            self.assertEqual(
                self.request("POST", "/api/camera-preview", {"id": capture_id})[0], 200
            )
            now[0] += 120.001
            self.assertEqual(
                self.request("POST", "/api/camera-preview", {"id": capture_id})[0], 410
            )

    def test_camera_preview_unconfirmed_capture_is_unavailable(self):
        capture_id = "c5" * 16
        with patch.object(
            self.m.workbench,
            "run",
            return_value={
                "state": "ready",
                "image_base64": "must-not-publish",
                "valid": False,
                "release_confirmed": True,
            },
        ):
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": capture_id, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            result = self.wait_result()["result"]
            self.assertNotIn("image_base64", result)
            self.assertFalse(result["preview_available"])
            self.assertEqual(
                self.request("POST", "/api/camera-preview", {"id": capture_id})[0],
                410,
            )

    def test_camera_preview_close_discards_memory_copy(self):
        capture_id = "c6" * 16
        with patch.object(
            self.m.workbench,
            "run",
            return_value={
                "state": "ready",
                "image_base64": "fixture",
                "valid": True,
                "release_confirmed": True,
            },
        ):
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": capture_id, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            self.wait_result()
            self.assertEqual(
                self.request("POST", "/api/camera-preview", {"id": capture_id})[0],
                200,
            )
            self.service.close()
            self.assertEqual(
                self.request("POST", "/api/camera-preview", {"id": capture_id})[0],
                410,
            )

    def test_camera_cancel_uses_existing_cooperative_owner(self):
        entered = threading.Event()

        def run(args, **hooks):
            entered.set()
            deadline = time.monotonic() + 2
            while not hooks["cancel_requested"]() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(hooks["cancel_requested"]())
            return {"state": "canceled", "release_confirmed": True}

        with patch.object(self.m.workbench, "run", side_effect=run):
            self.assertEqual(
                self.request(
                    "POST",
                    "/api/start",
                    {"id": "c3" * 16, "operation": "camera-capture", "params": {}},
                )[0],
                202,
            )
            self.assertTrue(entered.wait(1))
            self.assertEqual(
                self.request("POST", "/api/cancel", {"id": "c3" * 16})[0], 202
            )
            self.assertTrue(self.wait_result()["result"]["release_confirmed"])

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
        policy = {
            words[0]: words[1:]
            for directive in self.response_headers["Content-Security-Policy"].split(";")
            if (words := directive.split())
        }
        # The preview is an in-memory JPEG data URL. Permit that source without
        # allowing remote images or relaxing scripts and device API requests.
        self.assertEqual(policy.get("img-src"), ["data:"])
        self.assertEqual(policy.get("default-src"), ["'none'"])
        self.assertEqual(policy.get("script-src"), ["'self'"])
        self.assertEqual(policy.get("connect-src"), ["'self'"])
        self.assertNotIn("Access-Control-Allow-Origin", self.response_headers)
        self.assertNotIn(self.server.token.encode(), page)
        self.assertNotIn(b"fixture.profile", page)
        self.assertEqual(self.request("GET", "/../../fixture.profile")[0], 404)
        self.assertEqual(self.request("GET", "/api/state", Authorization="")[0], 403)

    def test_invalid_upload_does_not_open_device(self):
        with patch.object(self.m.workbench, "run") as run:
            for data in ["!", "YQ=="]:
                body = {
                    "id": "06" * 16,
                    "operation": "resource-upload",
                    "params": {"data": data, "ttl_ms": 5000},
                }
                self.assertIn(self.request("POST", "/api/start", body)[0], (400, 413))
            # The server rejects an oversized declared body before consuming
            # it. Send headers only so an intentional early close cannot race
            # HTTPConnection.sendall and replace the HTTP rejection observation.
            connection = http.client.HTTPConnection(
                "127.0.0.1", self.server.server_port, timeout=3
            )
            try:
                connection.putrequest("POST", "/api/start")
                connection.putheader("Authorization", "Bearer " + self.server.token)
                connection.putheader("Origin", self.server.origin)
                connection.putheader("Content-Type", "application/json")
                connection.putheader("Content-Length", "200000")
                connection.endheaders()
                response = connection.getresponse()
                self.assertEqual(response.status, 413)
                response.read()
            finally:
                connection.close()
            self.assertEqual(list(self.service.directory.iterdir()), [])
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

    def test_upload_exact_128k_boundary(self):
        import base64

        with patch.object(
            self.m.workbench, "run", return_value={"accepted": True}
        ) as run:
            # HTTP admission budget, independent of later pack validation.
            data = base64.b64encode(bytes(131072)).decode()
            body = {
                "id": "09" * 16,
                "operation": "resource-upload",
                "params": {"data": data, "ttl_ms": 5000},
            }
            self.assertEqual(self.request("POST", "/api/start", body)[0], 202)
            self.assertEqual(self.wait_result()["phase"], "returned")
            self.assertEqual(run.call_count, 1)
            self.assertEqual(
                (self.service.directory / ("09" * 16 + ".bkep")).stat().st_size, 131072
            )

    def test_upload_128k_plus_one_rejected_before_spool_or_worker(self):
        import base64

        with patch.object(self.m.workbench, "run") as run:
            for number, size in enumerate((131073, 131250), 10):
                body = {
                    "id": f"{number:032x}",
                    "operation": "resource-upload",
                    "params": {
                        "data": base64.b64encode(bytes(size)).decode(),
                        "ttl_ms": 5000,
                    },
                }
                self.assertEqual(self.request("POST", "/api/start", body)[0], 400)
            run.assert_not_called()
            self.assertEqual(list(self.service.directory.iterdir()), [])

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
