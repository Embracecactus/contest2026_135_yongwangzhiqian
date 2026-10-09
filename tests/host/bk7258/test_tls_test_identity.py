#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent explicit-time certificate validity oracle for TLS fixtures."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class IdentityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from tls_test_identity import issue

        cls.temp = tempfile.TemporaryDirectory(prefix="tls-validity-test-")
        cls.root = Path(cls.temp.name)

        def run(args):
            subprocess.run(
                args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )

        cls.cert, cls.key = issue(run, cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def verify_at(self, epoch):
        return subprocess.run(
            [
                "openssl",
                "verify",
                "-attime",
                str(epoch),
                "-CAfile",
                str(self.cert),
                str(self.cert),
            ],
            capture_output=True,
            text=True,
        )

    def test_dates(self):
        text = subprocess.check_output(
            ["openssl", "x509", "-in", str(self.cert), "-noout", "-dates"], text=True
        )
        self.assertIn("notBefore=Jan  1 00:00:00 2024 GMT", text)
        self.assertIn("notAfter=Jan  1 00:00:00 2030 GMT", text)

    def test_not_yet_valid(self):
        r = self.verify_at(1704067199)
        self.assertEqual(r.returncode, 2)
        self.assertIn("certificate is not yet valid", r.stderr)

    def test_valid(self):
        self.assertEqual(self.verify_at(1790496265).returncode, 0)

    def test_expired(self):
        r = self.verify_at(1893456001)
        self.assertEqual(r.returncode, 2)
        self.assertIn("certificate has expired", r.stderr)


if __name__ == "__main__":
    p = unittest.main(exit=False)
    raise SystemExit(2 if p.result.errors else int(not p.result.wasSuccessful()))
