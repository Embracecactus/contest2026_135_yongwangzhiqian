#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify that the pinned BK7258 SDK version is manifest metadata, not a ref."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))

from _lib import sdk  # noqa: E402


REVISION = "0" * 40


class ManifestSdkTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bk7258-sdk-manifest-")
        self.repository = Path(self.temporary.name) / "team"
        self.repository.mkdir()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def manifest(
        self, annotations: str, *, upstream: str = "refs/heads/fix/sdio-completion-mask"
    ) -> None:
        (self.repository / "team.xml").write_text(
            '<manifest><project path="vendor/sdk" groups="bk7258-sdk" '
            f'revision="{REVISION}" upstream="{upstream}">{annotations}'
            "</project></manifest>",
            encoding="utf-8",
        )

    def test_annotation_is_independent_of_upstream_branch_name(self) -> None:
        self.manifest('<annotation name="bk7258-sdk-version" value="v3.1.1.9"/>')
        selected = sdk.manifest_sdk(self.repository)
        self.assertEqual(selected.version, "v3.1.1.9")
        self.assertEqual(selected.upstream, "refs/heads/fix/sdio-completion-mask")

    def test_missing_annotation_is_rejected(self) -> None:
        self.manifest("")
        with self.assertRaisesRegex(sdk.SdkError, "exactly one valid"):
            sdk.manifest_sdk(self.repository)

    def test_duplicate_annotation_is_rejected(self) -> None:
        self.manifest(
            '<annotation name="bk7258-sdk-version" value="v3.1.1.9"/>'
            '<annotation name="bk7258-sdk-version" value="v3.1.1.9"/>'
        )
        with self.assertRaisesRegex(sdk.SdkError, "exactly one valid"):
            sdk.manifest_sdk(self.repository)

    def test_invalid_annotation_is_rejected(self) -> None:
        for version in ("", "3.1.1.9", "v3.1.1", "v3.1.1.9/unsafe"):
            with self.subTest(version=version):
                self.manifest(
                    f'<annotation name="bk7258-sdk-version" value="{version}"/>'
                )
                with self.assertRaisesRegex(sdk.SdkError, "exactly one valid"):
                    sdk.manifest_sdk(self.repository)


if __name__ == "__main__":
    unittest.main()
