# SPDX-License-Identifier: Apache-2.0

"""Host regression for deterministic Shaniu dual-eye asset packs."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
TOOLS = REPOSITORY / "tools/bk7258"
SOURCE = REPOSITORY / "app/bk7258/assets/display/shaniu-default-v1.json"
sys.path.insert(0, str(TOOLS))

from _lib import display_assets as display_domain  # noqa: E402


class DisplayAssetsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="bk7258-display-assets-test-"
        )
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _source_document(self) -> dict[str, object]:
        return json.loads(SOURCE.read_text(encoding="utf-8"))

    def _write_source(self, name: str, document: dict[str, object]) -> Path:
        path = self.root / name
        path.write_text(
            json.dumps(document, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        return path

    def test_default_pack_is_deterministic_and_previewable(self) -> None:
        first = self.root / "first.bkep"
        second = self.root / "second.bkep"
        first_previews = self.root / "first-previews"
        second_previews = self.root / "second-previews"

        report = display_domain.build(SOURCE, first, first_previews)
        display_domain.build(SOURCE, second, second_previews)

        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(report.pack_id, "shaniu-default-v1")
        self.assertEqual(report.revision, 1)
        self.assertEqual((report.width, report.height), (160, 160))
        self.assertEqual(len(report.entries), 11)
        frames = report.entries[1:]
        self.assertEqual(len(frames), 10)
        self.assertTrue(all(row.decoded_size == 160 * 160 for row in frames))
        self.assertTrue(all(row.codec == display_domain.CODEC_RLE8 for row in frames))
        previews = sorted(first_previews.glob("*.png"))
        self.assertEqual(len(previews), 20)
        self.assertTrue(
            all(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n") for path in previews)
        )
        self.assertNotEqual(
            (first_previews / "neutral-left.png").read_bytes(),
            (first_previews / "neutral-right.png").read_bytes(),
        )
        self.assertEqual(
            (first_previews / "thinking-left.png").read_bytes(),
            (first_previews / "thinking-right.png").read_bytes(),
        )
        self.assertEqual(
            {path.name for path in first_previews.iterdir()},
            {path.name for path in second_previews.iterdir()},
        )

    def test_pack_rejects_overwrite_and_payload_corruption(self) -> None:
        output = self.root / "default.bkep"
        accepted = display_domain.build(SOURCE, output)
        original = output.read_bytes()

        with self.assertRaises(display_domain.EyePackError):
            display_domain.build(SOURCE, output)
        self.assertEqual(output.read_bytes(), original)

        corrupt = self.root / "corrupt.bkep"
        changed = bytearray(original)
        changed[-1] ^= 0xFF
        corrupt.write_bytes(changed)
        with self.assertRaisesRegex(display_domain.EyePackError, "payload CRC"):
            display_domain.verify(corrupt)
        self.assertEqual(accepted.sha256, display_domain.verify(output).sha256)

    def test_source_rejects_unknown_fields_and_incomplete_sides(self) -> None:
        unknown = self._source_document()
        unknown["framebuffer"] = "/dev/fb0"
        with self.assertRaisesRegex(display_domain.EyePackError, "unknown fields"):
            display_domain.build(
                self._write_source("unknown.json", unknown),
                self.root / "unknown.bkep",
            )

        incomplete = self._source_document()
        neutral = incomplete["expressions"][0]
        neutral["side"] = "left"
        neutral["mirror_for_right"] = False
        with self.assertRaisesRegex(display_domain.EyePackError, "both left and right"):
            display_domain.build(
                self._write_source("incomplete.json", incomplete),
                self.root / "incomplete.bkep",
            )

        duplicate = self.root / "duplicate.json"
        duplicate.write_text(
            '{"format":"shaniu-eye-source/1","format":"shadowed"}\n',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(display_domain.EyePackError, "duplicate JSON"):
            display_domain.build(duplicate, self.root / "duplicate.bkep")

    def test_source_requires_aidk_canvas_and_neutral_fallback(self) -> None:
        wrong_canvas = self._source_document()
        wrong_canvas["canvas"]["width"] = 159
        with self.assertRaisesRegex(display_domain.EyePackError, "160x160"):
            display_domain.build(
                self._write_source("wrong-canvas.json", wrong_canvas),
                self.root / "wrong-canvas.bkep",
            )

        no_neutral = self._source_document()
        no_neutral["expressions"] = no_neutral["expressions"][1:]
        with self.assertRaisesRegex(display_domain.EyePackError, "neutral"):
            display_domain.build(
                self._write_source("no-neutral.json", no_neutral),
                self.root / "no-neutral.bkep",
            )

    def test_public_cli_builds_and_verifies_from_another_cwd(self) -> None:
        output = self.root / "cli.bkep"
        preview = self.root / "cli-previews"
        build = subprocess.run(
            [
                sys.executable,
                str(TOOLS / "bk7258.py"),
                "package",
                "eye-pack",
                "--source",
                "app/bk7258/assets/display/shaniu-default-v1.json",
                "--output",
                str(output),
                "--preview-dir",
                str(preview),
            ],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(build.returncode, 0, build.stderr)
        self.assertIn("bk7258 package eye-pack: PASS", build.stdout)

        verify = subprocess.run(
            [
                sys.executable,
                str(TOOLS / "bk7258.py"),
                "verify",
                "eye-pack",
                "--package",
                str(output),
            ],
            cwd=self.root,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(verify.returncode, 0, verify.stderr)
        self.assertIn("bk7258 verify eye-pack: PASS", verify.stdout)
        self.assertIn("entries=11", verify.stdout)


if __name__ == "__main__":
    unittest.main()
