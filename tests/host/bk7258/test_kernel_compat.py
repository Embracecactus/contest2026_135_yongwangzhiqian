#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Host tests for the pinned BK7258 kernel-wrapper compatibility gate."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))

from _lib import kernel_compat  # noqa: E402


class KernelCompatTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bk7258-kcompat-")
        self.root = Path(self.temporary.name)
        self.repository = self.root / "contest"
        self.nuttx = self.root / "nuttx"
        self.provenance = self.root / "provenance-nuttx"
        self.repository.mkdir()
        self.nuttx.mkdir()
        self.provenance.mkdir()
        self._write_contract()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_contract(self) -> None:
        rows = []
        for name in sorted(kernel_compat.FILES):
            path = self.nuttx / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name + "\n", encoding="utf-8")
            rows.append(
                {"path": name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            )
            provenance = self.provenance / name
            provenance.parent.mkdir(parents=True, exist_ok=True)
            provenance.write_bytes(path.read_bytes())
        document = {
            "schema": 1,
            "nuttx": {"commit": "a" * 40, "provenance": "host fixture"},
            "source_files": rows,
            "wrappers": {
                name: {
                    "disposition": "C",
                    "physical_validation": "pending",
                    "roles": list(roles),
                }
                for name, roles in kernel_compat.WRAPPERS.items()
            },
        }
        path = self.repository / kernel_compat.CONTRACT
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(document), encoding="utf-8")

    def _verify_sources(self) -> kernel_compat.Contract:
        completed = mock.Mock(stdout="a" * 40 + "\n")
        with mock.patch.object(kernel_compat.subprocess, "run", return_value=completed):
            return kernel_compat.verify_sources(
                self.repository,
                self.nuttx,
                provenance_root=self.provenance,
            )

    def _config(self, content: str) -> Path:
        path = self.root / ".config"
        path.write_text(content, encoding="utf-8")
        return path

    def test_reviewed_sources_and_normal_role_configs_pass(self) -> None:
        contract = self._verify_sources()
        self.assertEqual(contract.commit, "a" * 40)
        cp = self._config(
            "CONFIG_BUILD_FLAT=y\nCONFIG_ARCH_ARMV8M=y\n"
            "CONFIG_ARCH_CHIP_BK7258=y\nCONFIG_LTO_NONE=y\n"
            "# CONFIG_LTO_FULL is not set\n# CONFIG_SMP is not set\n"
        )
        kernel_compat.verify_role_config("cp", cp)
        ap = self._config(
            "CONFIG_BUILD_FLAT=y\nCONFIG_ARCH_ARMV8M=y\n"
            "CONFIG_ARCH_CHIP_BK7258=y\nCONFIG_LTO_NONE=y\n"
            "CONFIG_BK7258_AP_SMP_SCHED_ONLINE=y\nCONFIG_SMP=y\n"
        )
        kernel_compat.verify_role_config("ap", ap)

    def _rewrite_wrappers(self, mutate) -> None:
        path = self.repository / kernel_compat.CONTRACT
        document = json.loads(path.read_text(encoding="utf-8"))
        mutate(document["wrappers"])
        path.write_text(json.dumps(document), encoding="utf-8")

    def test_wrapper_validation_state_progression(self) -> None:
        def all_passed(wrappers: dict) -> None:
            for row in wrappers.values():
                row["physical_validation"] = "passed"
                row["evidence"] = "acceptance record 2026-09-20 sha256=ab12"

        # A completed acceptance may be recorded without touching the code
        # gate, as long as each passed wrapper cites its evidence.
        self._rewrite_wrappers(all_passed)
        self._verify_sources()

        # A passed row without evidence, an unknown state and a disposition
        # drift are all rejected.
        for mutate, message in (
            (
                lambda wrappers: next(iter(wrappers.values())).pop("evidence"),
                "without evidence",
            ),
            (
                lambda wrappers: wrappers.__setitem__(
                    next(iter(wrappers)),
                    {
                        "disposition": "C",
                        "physical_validation": "skipped",
                        "roles": ["ap"],
                    },
                ),
                "invalid reviewed disposition",
            ),
            (
                lambda wrappers: wrappers.__setitem__(
                    next(iter(wrappers)),
                    {
                        "disposition": "R",
                        "physical_validation": "pending",
                        "roles": ["ap"],
                    },
                ),
                "invalid reviewed disposition",
            ),
        ):
            with self.subTest(message=message):
                self._write_contract()
                self._rewrite_wrappers(all_passed)
                self._rewrite_wrappers(mutate)
                with self.assertRaisesRegex(kernel_compat.KernelCompatError, message):
                    kernel_compat.load(self.repository)

    def test_copied_source_and_provenance_drift_fail_without_auto_update(self) -> None:
        changed = self.nuttx / next(iter(kernel_compat.FILES))
        changed.write_text("changed\n", encoding="utf-8")
        with self.assertRaisesRegex(
            kernel_compat.KernelCompatError, "workspace build source"
        ):
            self._verify_sources()
        self._write_contract()
        missing = self.nuttx / next(iter(kernel_compat.FILES))
        missing.unlink()
        with self.assertRaisesRegex(
            kernel_compat.KernelCompatError, "missing workspace build source"
        ):
            self._verify_sources()
        self._write_contract()
        changed = self.provenance / next(iter(kernel_compat.FILES))
        changed.write_text("changed provenance\n", encoding="utf-8")
        with self.assertRaisesRegex(
            kernel_compat.KernelCompatError, "canonical provenance source"
        ):
            self._verify_sources()

    def test_contract_path_traversal_is_rejected(self) -> None:
        path = self.repository / kernel_compat.CONTRACT
        document = json.loads(path.read_text(encoding="utf-8"))
        document["source_files"][0]["path"] = "../outside.c"
        path.write_text(json.dumps(document), encoding="utf-8")
        with self.assertRaisesRegex(
            kernel_compat.KernelCompatError, "unsafe|reviewed set"
        ):
            kernel_compat.load(self.repository)

    def test_compat_only_ap_requires_no_lto_without_trace_or_smp(self) -> None:
        base = (
            "CONFIG_BUILD_FLAT=y\nCONFIG_ARCH_ARMV8M=y\n"
            "CONFIG_ARCH_CHIP_BK7258=y\nCONFIG_LTO_NONE=y\n"
        )
        for symbol in (
            "CONFIG_BK7258_BT_CONN_RX_REF_COMPAT",
            "CONFIG_BK7258_BT_ATT_MTU_COMPAT",
        ):
            with self.subTest(symbol=symbol):
                config = base + symbol + "=y\n"
                kernel_compat.verify_role_config("ap", self._config(config))
                with self.assertRaises(kernel_compat.KernelCompatError):
                    kernel_compat.verify_role_config(
                        "ap", self._config(config + "CONFIG_LTO_FULL=y\n")
                    )

    def test_lto_is_rejected_but_nonwrapper_ap_diagnostic_is_allowed(self) -> None:
        lto = self._config(
            "CONFIG_BUILD_FLAT=y\nCONFIG_ARCH_ARMV8M=y\n"
            "CONFIG_ARCH_CHIP_BK7258=y\nCONFIG_LTO_NONE=y\nCONFIG_LTO_FULL=y\n"
        )
        with self.assertRaisesRegex(kernel_compat.KernelCompatError, "CONFIG_LTO_FULL"):
            kernel_compat.verify_role_config("cp", lto)
        diagnostic = self._config("CONFIG_LTO_FULL=y\n")
        kernel_compat.verify_role_config("ap", diagnostic)


if __name__ == "__main__":
    unittest.main()
