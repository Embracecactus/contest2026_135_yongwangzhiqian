# SPDX-License-Identifier: Apache-2.0
"""Host-only checks for the signed, independently linked native app."""

import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
from _lib import native_apps


class NativeAppPackageTest(unittest.TestCase):
    def test_unsigned_build_needs_no_key(self):
        with tempfile.TemporaryDirectory(prefix="native-app-build-") as temporary:
            output = Path(temporary) / "breathing.elf"
            result = native_apps.build(ROOT, argparse.Namespace(output=output))
            self.assertFalse(result["signed"])
            self.assertEqual(result["checks"]["undefined_symbols"], "0")
            self.assertTrue(result["checks"]["relocations"])
            self.assertEqual(result["image_size"], output.stat().st_size)

    def test_one_source_changes_image_and_undefined_import_is_rejected(self):
        program = """#include "bk7258_native_abi.h"
static int start(void *state, uint32_t argument) { (void)state; return (int)argument; }
static int step(void *state, uint32_t elapsed, struct bknative_frame_s *frame) { (void)state; frame->expression = elapsed + 7; return 0; }
const struct bknative_program_s *shaniu_native_entry(void) { static const struct bknative_program_s app = { .abi = BKNATIVE_ABI, .state_size = 0, .start = start, .step = step }; return &app; }
"""
        undefined = program.replace(
            "return 0;", "extern int outside(void); return outside();"
        )
        with tempfile.TemporaryDirectory(prefix="native-app-source-") as temporary:
            root = Path(temporary)
            source = root / "alternate.c"
            source.write_text(program)
            first = root / "sample.elf"
            second = root / "alternate.elf"
            native_apps.build(ROOT, argparse.Namespace(output=first, source=None))
            native_apps.build(ROOT, argparse.Namespace(output=second, source=source))
            self.assertNotEqual(first.read_bytes(), second.read_bytes())
            source.write_text(undefined)
            with self.assertRaisesRegex(ValueError, "undefined"):
                native_apps.build(
                    ROOT, argparse.Namespace(output=root / "bad.elf", source=source)
                )

    def test_breathing_package_is_arm_rel_and_signed(self):
        key = (
            ROOT.parent
            / "apps/crypto/mbedtls/mbedtls/tests/data_files/ecdsa_secp256r1.key"
        )
        with tempfile.TemporaryDirectory(prefix="native-app-package-") as temporary:
            root = Path(temporary)
            output = root / "package"
            result = native_apps.pack(
                ROOT,
                argparse.Namespace(
                    id="breathing-demo",
                    name="Breathing demo",
                    description="A signed display-only breathing animation.",
                    version=1,
                    signing_key=key,
                    output=output,
                    openssl=None,
                ),
            )
            self.assertEqual(result["checks"]["undefined_symbols"], "0")
            self.assertLessEqual(result["image_size"], native_apps.IMAGE_MAX)
            package = json.loads((output / "package.json").read_text())
            self.assertEqual(package["format"], native_apps.PACKAGE_FORMAT)
            self.assertEqual(
                base64.b64decode(package["manifest"]),
                (output / "manifest.json").read_bytes(),
            )
            public = root / "public.pem"
            signature = root / "signature.der"
            signature.write_bytes(base64.b64decode(package["signature"], validate=True))
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
            subprocess.run(
                [
                    "openssl",
                    "dgst",
                    "-sha256",
                    "-verify",
                    str(public),
                    "-signature",
                    str(signature),
                    str(output / "manifest.json"),
                ],
                check=True,
                capture_output=True,
            )
            catalog = root / "catalog.json"
            report = native_apps.catalog_entry(
                argparse.Namespace(
                    package=output / "package.json",
                    package_url="https://store.example.test/breathing/package.json",
                    output=catalog,
                )
            )
            self.assertEqual(report["format"], native_apps.CATALOG_FORMAT)
            self.assertEqual(
                json.loads(catalog.read_text())["apps"][0]["id"], "breathing-demo"
            )

    def test_permissions_follow_device_rules(self):
        self.assertEqual(native_apps._permissions(1, "display"), ["display"])
        self.assertEqual(
            native_apps._permissions(2, "display,motion,haptic,sound"),
            ["display", "motion", "haptic", "sound"],
        )
        for abi, value in (
            (1, "display,motion"),
            (2, "motion"),
            (2, "display,haptic,motion"),
            (2, "display,motion,motion"),
            (2, "display,camera"),
            (2, "display,clock"),
            (3, "display,storage,clock"),
            (4, "display"),
        ):
            with self.assertRaises(ValueError, msg=f"{abi}:{value}"):
                native_apps._permissions(abi, value)

    def test_abi3_grants_and_result_labels(self):
        self.assertEqual(
            native_apps._permissions(3, "display,motion,haptic,clock,storage"),
            ["display", "motion", "haptic", "clock", "storage"],
        )
        self.assertEqual(native_apps._results(3, "去吧,再想想"), ["去吧", "再想想"])
        self.assertEqual(native_apps._results(2, ""), [])
        with self.assertRaises(ValueError):
            native_apps._results(2, "去吧")
        with self.assertRaises(ValueError):
            native_apps._results(3, "a,b,c,d,e")
        with self.assertRaises(ValueError):
            native_apps._results(3, "超过十六个字节的标签")

    def test_app_hash_matches_device_fnv1a(self):
        # Pinned in test_bk7258_native_apps.c against bknative_app_hash().
        self.assertEqual(native_apps.app_hash("shake"), 0xBD8BB2F5)
        self.assertEqual(native_apps.app_hash(""), 2166136261)

    def test_shake_decide_packs_as_abi3(self):
        key = (
            ROOT.parent
            / "apps/crypto/mbedtls/mbedtls/tests/data_files/ecdsa_secp256r1.key"
        )
        with tempfile.TemporaryDirectory(prefix="native-app-v2-") as temporary:
            output = Path(temporary) / "package"
            result = native_apps.pack(
                ROOT,
                argparse.Namespace(
                    id="shake",
                    name="Shake to decide",
                    description="Shake for a yes or no face with a short pulse.",
                    version=1,
                    abi=3,
                    permissions="display,motion,haptic",
                    results="去吧,再想想",
                    signing_key=key,
                    output=output,
                    openssl=None,
                    source=ROOT / "app/bk7258/native-apps/shake_decide.c",
                ),
            )
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual(manifest["abi"], 3)
            self.assertEqual(manifest["results"], ["去吧", "再想想"])
            self.assertEqual(manifest["permissions"], ["display", "motion", "haptic"])
            self.assertEqual(result["app_hash"], "bd8bb2f5")
            self.assertEqual(result["checks"]["undefined_symbols"], "0")


if __name__ == "__main__":
    unittest.main()
