#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test-only entropy observation: never linked to target firmware."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
int __wrap_mbedtls_ctr_drbg_random(void *, unsigned char *, size_t);
time_t __wrap_time(time_t *);
int __real_mbedtls_ctr_drbg_random(void *ctx, unsigned char *out, size_t n)
{
  (void)ctx;
  for (size_t i = 0; i < n; i++) out[i] = (unsigned char)(getpid() + i);
  return 0;
}
time_t __real_time(time_t *ptr)
{
  time_t value = (time_t)getpid();
  if (ptr) *ptr = value;
  return value;
}
int main(int argc, char **argv)
{
  unsigned char bytes[9];
  size_t n = argc > 1 ? (size_t)atoi(argv[1]) : sizeof(bytes);
  if (n > sizeof(bytes)) return 2;
  if (__wrap_mbedtls_ctr_drbg_random(NULL, bytes, n)) return 3;
  for (size_t i = 0; i < n; i++) printf("%02x", bytes[i]);
  time_t time;
  time_t observed = __wrap_time(&time);
  if (observed != time) return 4;
  printf(" %lld\n", (long long)time);
  return 0;
}
"""


class EntropyTapeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        driver = cls.root / "driver.c"
        driver.write_text(DRIVER)
        cls.binary = cls.root / "driver"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(driver),
                str(HERE / "tls_entropy_tape.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.area = tempfile.TemporaryDirectory(dir=self.root)
        self.addCleanup(self.area.cleanup)
        self.tape = Path(self.area.name) / "tape"

    def run_mode(self, mode=None, size=None):
        env = {
            k: v for k, v in os.environ.items() if not k.startswith("SHANIU_TLS_TAPE_")
        }
        if mode:
            env["SHANIU_TLS_TAPE_" + mode] = str(self.tape)
        args = [str(self.binary)] + ([] if size is None else [str(size)])
        return subprocess.run(args, env=env, capture_output=True)

    def record(self):
        result = self.run_mode("RECORD")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.tape.stat().st_mode & 0o777, 0o600)
        return result

    def test_roundtrip_preserves_observed_random_and_time(self):
        result = self.record()
        replay = self.run_mode("REPLAY")
        self.assertEqual(replay.returncode, 0, replay.stderr)
        self.assertEqual(replay.stdout, result.stdout)

    def test_truncated_tape_fails(self):
        self.record()
        self.tape.write_bytes(self.tape.read_bytes()[:-1])
        self.assertEqual(self.run_mode("REPLAY").returncode, 86)

    def test_changed_request_size_fails(self):
        self.record()
        self.assertEqual(self.run_mode("REPLAY", 8).returncode, 86)

    def test_extra_unconsumed_record_fails(self):
        self.record()
        with self.tape.open("ab") as out:
            out.write(b"extra")
        self.assertEqual(self.run_mode("REPLAY").returncode, 86)

    def test_existing_tape_never_overwritten(self):
        self.record()
        before = self.tape.read_bytes()
        self.assertEqual(self.run_mode("RECORD").returncode, 86)
        self.assertEqual(self.tape.read_bytes(), before)

    def test_disabled_uses_real_source_without_file(self):
        self.assertEqual(self.run_mode().returncode, 0)
        self.assertFalse(self.tape.exists())


if __name__ == "__main__":
    unittest.main()
