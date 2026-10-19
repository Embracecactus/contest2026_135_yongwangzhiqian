#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local-only contract tests for the dialect-2 HTTPS TTS relay/replay."""
import hashlib
import base64
import http.client
import json
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from tls_test_identity import issue
from voice_tts_relay import RelayServer, RelaySettings


INPUT = "local fixture text"
PCM = b"\x00\x01" * 8192
ENTITY = (
    b'data: {"choices":[{"index":0,"delta":{"audio":{"data":"' +
    base64.b64encode(PCM) + b'"}},"finish_reason":null}]}\n\n'
    b'data: {"choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}\n\n'
    b"data: [DONE]\n\n"
)


class _Upstream(BaseHTTPRequestHandler):
    calls = 0
    request_hash = None
    authorization = None
    first_sent = threading.Event()
    release_second = threading.Event()

    def log_message(self, *_args):
        pass

    def do_POST(self):
        size = int(self.headers["Content-Length"])
        type(self).calls += 1
        type(self).request_hash = hashlib.sha256(self.rfile.read(size)).hexdigest()
        type(self).authorization = self.headers.get("Authorization")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        split = len(ENTITY) // 2
        self.wfile.write(("%x\r\n" % split).encode() + ENTITY[:split] + b"\r\n")
        self.wfile.flush()
        type(self).first_sent.set()
        type(self).release_second.wait(2)
        self.wfile.write(("%x\r\n" % (len(ENTITY) - split)).encode() + ENTITY[split:] +
                         b"\r\n0\r\n\r\n")
        self.wfile.flush()


class RelayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="tts-relay-")
        cls.root = Path(cls.temp.name)

        def run(args):
            subprocess.run(args, check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)

        cls.cert, cls.key = issue(run, cls.root)
        cls.context = ssl.create_default_context(cafile=str(cls.cert))
        cls.upstream = ThreadingHTTPServer(("127.0.0.1", 0), _Upstream)
        upstream_tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        upstream_tls.load_cert_chain(cls.cert, cls.key)
        cls.upstream.socket = upstream_tls.wrap_socket(cls.upstream.socket,
                                                        server_side=True)
        cls.upstream_thread = threading.Thread(target=cls.upstream.serve_forever,
                                               daemon=True)
        cls.upstream_thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.upstream.shutdown()
        cls.upstream.server_close()
        cls.temp.cleanup()

    def make_settings(self, mode):
        source = self.root / "input.txt"
        source.write_text(INPUT, encoding="utf-8")
        key_file = self.root / "key.txt"
        key_file.write_text("test-only-token\n", encoding="utf-8")
        response = self.root / "response.bin"
        config = self.root / "cloud.json"
        config.write_text(json.dumps({
            "upstream_url": "https://localhost:%d/v1" % self.upstream.server_port,
            "ca_file": str(self.cert),
            "key_file": str(key_file),
            "tts_model": "mimo-v2.5-tts",
            "tts_voice": "mimo_default",
            "text_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
            "max_body": 65536,
        }), encoding="utf-8")
        return RelaySettings.from_paths(source, config, response, self.cert, self.key,
                                        "127.0.0.1", 0, self.root / "reports", mode,
                                        delay_ms=25)

    def request(self, server):
        body = json.dumps({
            "model": "mimo-v2.5-tts", "stream": True,
            "audio": {"format": "pcm16", "voice": "mimo_default"},
            "messages": [{"role": "assistant", "content": INPUT}],
        }, separators=(",", ":")).encode()
        client = http.client.HTTPSConnection("localhost", server.port,
                                             context=self.context, timeout=3)
        client.request("POST", "/v1/chat/completions", body,
                       {"Content-Type": "application/json"})
        return client, client.getresponse()

    def test_a_proxies_one_request_and_caches_the_entity(self):
        server = RelayServer(self.make_settings("A"))
        _Upstream.first_sent.clear()
        _Upstream.release_second.clear()
        result = {}
        def client_read():
            client, response = self.request(server)
            result["status"] = response.status
            result["first"] = response.read(1)
            result["first_before_eof"] = True
            result["rest"] = response.read()
            client.close()
        with server.running():
            client_thread = threading.Thread(target=client_read)
            client_thread.start()
            self.assertTrue(_Upstream.first_sent.wait(1))
            for _ in range(20):
                if result.get("first_before_eof"):
                    break
                time.sleep(0.01)
            self.assertTrue(result.get("first_before_eof"))
            _Upstream.release_second.set()
            client_thread.join(2)
            self.assertFalse(client_thread.is_alive())
        self.assertEqual(result["status"], 200)
        self.assertEqual(result["first"] + result["rest"], ENTITY)
        self.assertEqual(_Upstream.calls, 1)
        self.assertEqual(_Upstream.authorization, "Bearer test-only-token")
        self.assertEqual((self.root / "response.bin").read_bytes(), ENTITY)
        report = json.loads((self.root / "reports" / "tts-relay-a.json").read_text())
        self.assertEqual(report["response_sha256"], hashlib.sha256(ENTITY).hexdigest())
        self.assertEqual(report["upstream_posts"], 1)
        self.assertGreaterEqual(len(report["upstream_reads"]), 2)
        self.assertGreaterEqual(len(report["downstream_writes"]), 2)

    def test_b_delays_after_a_bounded_split_and_c_replays_immediately(self):
        response = self.root / "response.bin"
        response.write_bytes(ENTITY)
        before = _Upstream.calls
        for mode in ("B", "C"):
            server = RelayServer(self.make_settings(mode))
            with server.running():
                start = time.monotonic()
                client, reply = self.request(server)
                self.assertEqual(reply.status, 200)
                entity = reply.read()
                elapsed = time.monotonic() - start
                client.close()
            self.assertEqual(entity, ENTITY)
            if mode == "B":
                self.assertGreaterEqual(elapsed, 0.015)
                report = json.loads((self.root / "reports" / "tts-relay-b.json").read_text())
                self.assertEqual(report["prefix_source_pcm_bytes"], len(PCM))
                self.assertGreater(report["prefix_bytes"], 0)
            else:
                self.assertLess(elapsed, 0.015)
        self.assertEqual(_Upstream.calls, before)


if __name__ == "__main__":
    unittest.main()
