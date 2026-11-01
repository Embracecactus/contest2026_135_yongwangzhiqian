# SPDX-License-Identifier: Apache-2.0
"""Real OpenSSL package signing using the upstream public test key only."""

import argparse
import base64
import errno
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
from _lib import skills


class SkillPackageTest(unittest.TestCase):
    def test_device_verifies_signed_boundaries_independently(self):
        host = ROOT / "tests/host/bk7258"
        subprocess.run(
            ["make", "-C", str(host), "build/test_bk7258_runtime_skills"],
            check=True,
            capture_output=True,
        )
        key = (
            ROOT.parent
            / "apps/crypto/mbedtls/mbedtls/tests/data_files/ecdsa_secp256r1.key"
        )
        value = json.loads(
            (ROOT / "tools/bk7258/examples/skill-focus.json").read_text()
        )
        value.update(
            name='带"引号"的技能',
            tools=["device_status"],
            permissions=["device_status"],
            body='First line\nSecond "quoted" line',
        )
        good = json.dumps(value, ensure_ascii=False).encode()
        mismatch = dict(value, permissions=["camera_capture"])
        cases = [
            (good, 0),
            (json.dumps(mismatch).encode(), -errno.EACCES),
            (good + b"not-json", -errno.EINVAL),
            (good.replace(b'"name":', b'"unknown":0,"name":'), -errno.EINVAL),
            (good.replace(b'"name":', b'"name":"duplicate","name":'), -errno.EINVAL),
            (good.replace(b'"name": "', b'"name": "\xff'), -errno.EINVAL),
        ]
        with tempfile.TemporaryDirectory(prefix="skill-device-vector-") as temp:
            manifest = Path(temp) / "manifest.json"
            signature = Path(temp) / "signature.der"
            for raw, expected in cases:
                manifest.write_bytes(raw)
                subprocess.run(
                    [
                        "openssl",
                        "dgst",
                        "-sha256",
                        "-sign",
                        str(key),
                        "-out",
                        str(signature),
                        str(manifest),
                    ],
                    check=True,
                    capture_output=True,
                )
                subprocess.run(
                    [
                        str(host / "build/test_bk7258_runtime_skills"),
                        "--check-vector",
                        str(manifest),
                        str(signature),
                        str(expected),
                    ],
                    check=True,
                    capture_output=True,
                )

    def test_signed_package_preserves_exact_manifest_and_verifies(self):
        manifest = ROOT / "tools/bk7258/examples/skill-focus.json"
        key = (
            ROOT.parent
            / "apps/crypto/mbedtls/mbedtls/tests/data_files/ecdsa_secp256r1.key"
        )
        with tempfile.TemporaryDirectory(prefix="skill-package-test-") as temp:
            output = Path(temp) / "pack"
            args = argparse.Namespace(
                manifest=manifest, signing_key=key, output=output, openssl=None
            )
            result = skills.run(args)
            self.assertFalse(result["installed"])
            package = json.loads((output / "package.json").read_text())
            self.assertEqual(
                base64.b64decode(package["manifest"]), manifest.read_bytes()
            )
            signature = Path(temp) / "signature.der"
            signature.write_bytes(base64.b64decode(package["signature"], validate=True))
            public = Path(temp) / "public.pem"
            subprocess.run(
                [
                    "openssl",
                    "pkey",
                    "-pubin",
                    "-inform",
                    "DER",
                    "-in",
                    str(output / "publisher.der"),
                    "-out",
                    str(public),
                ],
                check=True,
                capture_output=True,
            )
            command = [
                "openssl",
                "dgst",
                "-sha256",
                "-verify",
                str(public),
                "-signature",
                str(signature),
                str(output / "manifest.json"),
            ]
            subprocess.run(command, check=True, capture_output=True)
            (output / "manifest.json").write_bytes(manifest.read_bytes() + b" ")
            self.assertNotEqual(
                subprocess.run(command, capture_output=True).returncode, 0
            )
            with self.assertRaises(ValueError):
                skills.run(args)

    def test_unknown_duplicate_and_escalated_permissions_are_rejected(self):
        raw = (ROOT / "tools/bk7258/examples/skill-focus.json").read_bytes()
        with self.assertRaises(ValueError):
            skills.validate(raw.replace(b'"id":', b'"id":"duplicate","id":'))
        for mutate in (
            lambda doc: doc.update(extra=True),
            lambda doc: doc.update(id="../escape"),
            lambda doc: doc.update(triggers=["专注助手"]),
            lambda doc: doc.update(permissions=[]),
            lambda doc: doc.update(
                tools=["camera_capture"], permissions=["camera_capture"]
            ),
        ):
            document = json.loads(raw)
            mutate(document)
            with self.assertRaises(ValueError):
                skills.validate(json.dumps(document, ensure_ascii=False).encode())


if __name__ == "__main__":
    unittest.main()
