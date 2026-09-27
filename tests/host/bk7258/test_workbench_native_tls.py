#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Python client through embedded TLS/PC guard/session and native installer.

External transport is a Unix socket; no USB port or physical board is used.
The same native worker survives a real socket close and fresh TLS/SDC1 AUTH.
Called with the existing TLS fixture's exact executable and synthetic identity.
"""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
w = importlib.import_module("_lib.workbench")
m = importlib.import_module("_lib.workbench_resources")
PACK = ROOT / "tests/host/bk7258/build/shaniu-default-v1.bkep"


def run(executable, certificate, key, selected=None):
    class NativeTls(unittest.TestCase):
        def exercise(self, variant):
            with tempfile.TemporaryDirectory(prefix="native-pack-tls-") as directory:
                root = Path(directory)
                grant = root / "grant"
                grant.mkdir()
                shutil.copyfile(PACK, grant / "expected.bkep")
                address = root / "wire"
                sessions = 2 if variant == "reconnect" else 1
                process = subprocess.Popen(
                    [
                        executable,
                        "--pc-resource-peer",
                        certificate,
                        key,
                        str(grant),
                        str(address),
                        str(sessions),
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                clients = []
                try:
                    deadline = time.monotonic() + 5
                    while not address.exists():
                        if process.poll() is not None:
                            self.fail(
                                "native resource peer exited before interface became ready"
                            )
                        if time.monotonic() > deadline:
                            self.fail("native resource interface unavailable")
                        time.sleep(0.005)
                    pem = Path(certificate).read_text()
                    pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()

                    class Channel:
                        def __init__(self):
                            self.sock = socket.socket(
                                socket.AF_UNIX, socket.SOCK_STREAM
                            )
                            self.sock.connect(str(address))
                            self.sock.setblocking(False)

                        def write(self, data):
                            try:
                                return self.sock.send(data[:97])
                            except BlockingIOError:
                                return 0

                        def read(self, n):
                            try:
                                return self.sock.recv(min(n, 83))
                            except BlockingIOError:
                                return None

                        def close(self):
                            self.sock.close()

                    def connect(principal=84):
                        c = w.ControlClient(Channel(), pem, pin, timeout=30)
                        clients.append(c)
                        c.start(bytes([principal]) + bytes(31))
                        return c

                    if variant == "wrong-principal":
                        with self.assertRaises(w.ControlError):
                            connect(42)
                    else:
                        c = connect()
                        a = argparse.Namespace(
                            operation="resource-upload",
                            file=PACK,
                            receipt=root / "receipt.json",
                            ttl_ms=60000,
                        )
                        p = m.prepare(a)
                        try:
                            if variant == "reconnect":
                                first = c.resource_status()
                                receipt = dict(
                                    format="shaniu-resource-receipt/1",
                                    device_sha256=pin,
                                    epoch=first["epoch"],
                                    nonce="1234567890abcdef1234567890abcdef",
                                    previous_id=first["id"],
                                    total=p.total,
                                    sha256=p.sha256,
                                    ttl_ms=60000,
                                )
                                m.create_receipt(a.receipt, receipt)
                                c.resource_request(
                                    "begin",
                                    first["epoch"],
                                    receipt["nonce"],
                                    first["id"],
                                    p.total,
                                    60000,
                                )
                                state = c.resource_status()
                                until = time.monotonic() + 5
                                while state["state"] != "receiving":
                                    self.assertLess(time.monotonic(), until)
                                    time.sleep(0.01)
                                    state = c.resource_status()
                                p.stream.seek(0)
                                c.resource_request(
                                    "append",
                                    state["epoch"],
                                    state["nonce"],
                                    state["id"],
                                    0,
                                    data=p.stream.read(4096),
                                )
                                old_id = state["id"]
                                old_epoch = state["epoch"]
                                c.close()
                                c = connect()
                                current = c.resource_status()
                                self.assertEqual(
                                    (current["id"], current["epoch"]),
                                    (old_id, old_epoch),
                                )
                                a.operation = "resource-resume"
                                a.ttl_ms = None
                                p.close()
                                p = m.prepare(a)
                            if variant == "cancel":
                                original = c.resource_request

                                def stop(operation, *values, **kwargs):
                                    if operation == "append":
                                        raise w.ControlError("caller stops before data")
                                    return original(operation, *values, **kwargs)

                                c.resource_request = stop
                                with self.assertRaises(w.ControlError):
                                    m.perform(c, a, p)
                                c.resource_request = original
                                a.operation = "resource-cancel"
                                a.file = None
                                a.ttl_ms = None
                                p.close()
                                p = m.prepare(a)
                                result = m.perform(c, a, p)
                                self.assertEqual(result["state"], "canceled")
                                self.assertFalse(result["resources_held"])
                            else:
                                result = m.perform(c, a, p)
                                self.assertTrue(result["installed"])
                                self.assertEqual(result["written"], PACK.stat().st_size)
                        finally:
                            p.close()
                        c.close()
                    process.wait(timeout=8)
                    detail = process.stderr.read().decode()
                    self.assertEqual(process.returncode, 0, detail)
                    print(
                        "\nRESOURCE_EVIDENCE "
                        + json.dumps(
                            dict(
                                case=variant,
                                connections=sessions,
                                certificate_sha256=pin,
                                input_sha256=hashlib.sha256(
                                    PACK.read_bytes()
                                ).hexdigest(),
                                native=detail.strip(),
                            )
                        ),
                        flush=True,
                    )
                finally:
                    for c in clients:
                        c.close()
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                    process.stdout.close()
                    process.stderr.close()

        def test_upload(self):
            self.exercise("upload")

        def test_reconnect(self):
            self.exercise("reconnect")

        def test_cancel(self):
            self.exercise("cancel")

        def test_wrong_principal(self):
            self.exercise("wrong-principal")

    suite = (
        unittest.TestSuite([NativeTls("test_" + selected)])
        if selected
        else unittest.defaultTestLoader.loadTestsFromTestCase(NativeTls)
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 2 if result.errors else (0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("certificate")
    parser.add_argument("key")
    parser.add_argument(
        "--case", choices=("upload", "reconnect", "cancel", "wrong_principal")
    )
    a = parser.parse_args()
    raise SystemExit(run(a.executable, a.certificate, a.key, a.case))
