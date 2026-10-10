#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production TLS/PC permission/SDC1/camera state; synthetic frame I/O only."""
import base64
import hashlib
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/bk7258"))
from _lib import workbench as w
from _lib import workbench_camera as camera


def run(executable, certificate, key, peer_mode):
    with tempfile.TemporaryDirectory(prefix="camera-pc-identity-") as root:
        process = subprocess.Popen(
            [executable, peer_mode, certificate, key, root],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        os.set_blocking(process.stdin.fileno(), False)
        os.set_blocking(process.stdout.fileno(), False)

        class Pipe:
            def write(self, data):
                try:
                    return os.write(process.stdin.fileno(), bytes(data[:19]))
                except BlockingIOError:
                    return 0

            def read(self, size):
                try:
                    return os.read(process.stdout.fileno(), min(size, 29))
                except BlockingIOError:
                    return None

            def close(self):
                if not process.stdin.closed:
                    process.stdin.close()

        pem = Path(certificate).read_text()
        pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(pem)).hexdigest()
        client = w.ControlClient(Pipe(), pem, pin)
        try:
            client.start(bytes([84]) + bytes(31))
            if peer_mode == "--pc-peer":
                try:
                    camera.status(client)
                except w.ControlError:
                    pass
                else:
                    raise AssertionError("resources/tasks did not reject camera access")
            else:
                first = camera.capture(client, preview=True)
                assert first["state"] == "ready" and first["release_confirmed"]
                image = base64.b64decode(first["image_base64"], validate=True)
                assert hashlib.sha256(image).hexdigest() == first["sha256"]
                from PIL import Image
                from io import BytesIO

                with Image.open(BytesIO(image)) as picture:
                    picture.load()
                    assert picture.size == (first["width"], first["height"])
                second = camera.capture(client, cancel_requested=lambda: True)
                assert second["state"] == "canceled" and second["release_confirmed"]
                assert camera.status(client)["size"] == 0
                third = camera.capture(client)
                assert third["state"] == "ready" and "image_base64" not in third
                assert third["id"] > first["id"]
                assert camera.status(client)["state"] == "canceled"
        finally:
            client.close()
            try:
                result = process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise
            detail = process.stderr.read().decode(errors="replace")
            process.stdout.close()
            process.stderr.close()
        assert result == 0, detail


if __name__ == "__main__":
    for mode in ("--camera-peer", "--pc-peer"):
        run(*sys.argv[1:], mode)
    print("CAMERA_TLS_PC_PRODUCTION_PASS synthetic_frame=true hardware=false")
