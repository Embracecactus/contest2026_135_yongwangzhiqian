#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host-only tests for bk7258_hil_download.py; no hardware access."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("bk7258_hil_download.py")


def fake_loader(path: Path, lines: list[str], exit_code: int) -> None:
    body = "#!/bin/sh\n" + "\n".join(
        f"printf '%s\\n' {json.dumps(line)}" for line in lines
    )
    body += f"\nexit {exit_code}\n"
    path.write_text(body, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def invoke(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


class DownloadToolTest(unittest.TestCase):
    def test_profiles_cover_three_current_boards(self) -> None:
        completed = invoke("profiles")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        document = json.loads(completed.stdout)
        self.assertEqual(
            set(document["profiles"]),
            {"aidk_ai_toy", "t5ai_core", "t5_board"},
        )

    def test_aidk_profile_owns_software_reboot_and_rejects_small_image(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            image = root / "FILE.bin"
            fake_loader(loader, [], 0)
            image.write_bytes(b"too-small")
            completed = invoke(
                "preflight",
                "--board",
                "aidk",
                "--artifact-kind",
                "direct-full",
                "--loader",
                str(loader),
                "--image",
                str(image),
                "--port",
                "COM8",
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("must be 8388608 bytes", completed.stderr)

            image.write_bytes(b"\xff" * 8388608)
            completed = invoke(
                "preflight",
                "--board",
                "aidk-ai-toy",
                "--artifact-kind",
                "direct-full",
                "--loader",
                str(loader),
                "--image",
                str(image),
                "--port",
                "COM8",
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(completed.stdout)
            command = result["command"]
            self.assertEqual(result["board_profile"], "aidk_ai_toy")
            self.assertEqual(result["port"], "COM8")
            self.assertEqual(command[command.index("--swrst") + 1], "reset reboot")
            self.assertEqual(command[command.index("--hard-reset") + 1], "0")
            self.assertEqual(command[command.index("--fast-link") + 1], "1")

    def test_t5_multi_transport_hashes_and_bounds_segments(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            first = root / "bl.bin"
            second = root / "app.bin"
            fake_loader(loader, [], 0)
            first.write_bytes(b"boot")
            second.write_bytes(b"application")
            first_hash = hashlib.sha256(first.read_bytes()).hexdigest()
            second_hash = hashlib.sha256(second.read_bytes()).hexdigest()
            completed = invoke(
                "preflight",
                "--board",
                "t5board",
                "--artifact-kind",
                "signed-segments",
                "--loader",
                str(loader),
                "--port",
                "COM3",
                "--segment",
                f"{first}@0x0-0x4",
                "--segment",
                f"{second}@0x11000-0xb",
                "--segment-sha256",
                first_hash,
                "--segment-sha256",
                second_hash,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(completed.stdout)
            command = result["command"]
            self.assertEqual(result["transport"], "multi")
            self.assertEqual(result["board_profile"], "t5_board")
            self.assertEqual(result["reset_strategy"], "loader-usb-uart-rts")
            self.assertEqual(command[command.index("-b") + 1], "6000000")
            self.assertEqual(command[command.index("--uart-type") + 1], "OTHER")
            self.assertIn("--mainBin-multi", command)
            self.assertNotIn("--swrst", command)
            self.assertEqual(len(result["artifacts"]), 2)

    def test_aidk_signed_segments_require_bounded_hashes_and_keep_software_reset(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            cp_b = root / "cp_b.bin"
            ap_b = root / "ap_b.bin"
            fake_loader(loader, [], 0)
            cp_b.write_bytes(b"c" * 4096)
            ap_b.write_bytes(b"a" * 4096)
            cp_hash = hashlib.sha256(cp_b.read_bytes()).hexdigest()
            ap_hash = hashlib.sha256(ap_b.read_bytes()).hexdigest()
            base = (
                "preflight",
                "--board", "aidk",
                "--transport", "multi",
                "--artifact-kind", "signed-segments",
                "--loader", str(loader),
                "--port", "COM8",
                "--segment", f"{cp_b}@0x4000-0x1000",
                "--segment", f"{ap_b}@0x8000-0x1000",
            )

            missing_bounds = invoke(*base, "--segment-sha256", cp_hash,
                                    "--segment-sha256", ap_hash)
            self.assertNotEqual(missing_bounds.returncode, 0)
            self.assertIn("--write-bound", missing_bounds.stderr)

            missing_hash = invoke(*base, "--write-bound", "0x4000-0x1000",
                                  "--write-bound", "0x8000-0x1000")
            self.assertNotEqual(missing_hash.returncode, 0)
            self.assertIn("--segment-sha256", missing_hash.stderr)

            wrong_hash = invoke(
                *base, "--segment-sha256", "0" * 64, "--segment-sha256", ap_hash,
                "--write-bound", "0x4000-0x1000",
                "--write-bound", "0x8000-0x1000",
            )
            self.assertNotEqual(wrong_hash.returncode, 0)
            self.assertIn("SHA256 mismatch", wrong_hash.stderr)

            out_of_bound = invoke(
                *base, "--segment-sha256", cp_hash, "--segment-sha256", ap_hash,
                "--write-bound", "0x4000-0x1000",
                "--write-bound", "0x9000-0x1000",
            )
            self.assertNotEqual(out_of_bound.returncode, 0)
            self.assertIn("outside every", out_of_bound.stderr)

            unaligned = invoke(
                *base, "--segment-sha256", cp_hash, "--segment-sha256", ap_hash,
                "--write-bound", "0x4001-0x1000",
                "--write-bound", "0x8000-0x1000",
            )
            self.assertNotEqual(unaligned.returncode, 0)
            self.assertIn("4 KiB aligned", unaligned.stderr)

            capacity = invoke(
                *base, "--segment-sha256", cp_hash, "--segment-sha256", ap_hash,
                "--write-bound", "0x4000-0x1000",
                "--write-bound", "0x800000-0x1000",
            )
            self.assertNotEqual(capacity.returncode, 0)
            self.assertIn("known flash capacity", capacity.stderr)

            completed = invoke(
                *base, "--segment-sha256", cp_hash, "--segment-sha256", ap_hash,
                "--write-bound", "0x4000-0x1000",
                "--write-bound", "0x8000-0x1000",
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(completed.stdout)
            self.assertEqual(result["transport"], "multi")
            self.assertEqual(len(result["operator_authorized_write_bounds"]), 2)
            command = result["command"]
            self.assertEqual(command[command.index("--swrst") + 1], "reset reboot")
            self.assertEqual(command[command.index("--hard-reset") + 1], "0")
            self.assertEqual(command.count("--uart-type"), 1)
            self.assertEqual(command[command.index("--fast-link") + 1], "1")

    def test_multi_transport_rejects_overlap(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            first = root / "a.bin"
            second = root / "b.bin"
            fake_loader(loader, [], 0)
            first.write_bytes(b"1234")
            second.write_bytes(b"5678")
            completed = invoke(
                "preflight",
                "--board",
                "t5ai-core",
                "--artifact-kind",
                "signed-segments",
                "--loader",
                str(loader),
                "--port",
                "7",
                "--segment",
                f"{first}@0x0-0x4",
                "--segment",
                f"{second}@0x2-0x4",
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("overlap", completed.stderr)

    def test_artifact_kind_must_match_transport(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            image = root / "full.bin"
            segment = root / "app.bin"
            fake_loader(loader, [], 0)
            image.write_bytes(b"firmware")
            segment.write_bytes(b"segment")

            full_over_multi = invoke(
                "preflight",
                "--board",
                "t5-board",
                "--transport",
                "multi",
                "--artifact-kind",
                "direct-full",
                "--loader",
                str(loader),
                "--port",
                "COM3",
                "--segment",
                f"{segment}@0x0-0x7",
            )
            self.assertNotEqual(full_over_multi.returncode, 0)
            self.assertIn("requires transport=single", full_over_multi.stderr)

            segments_over_single = invoke(
                "preflight",
                "--board",
                "t5-board",
                "--transport",
                "single",
                "--artifact-kind",
                "direct-segments",
                "--loader",
                str(loader),
                "--port",
                "COM3",
                "--image",
                str(image),
            )
            self.assertNotEqual(segments_over_single.returncode, 0)
            self.assertIn("requires transport=multi", segments_over_single.stderr)

    def test_multi_transport_rejects_single_only_loader_options(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            segment = root / "app.bin"
            fake_loader(loader, [], 0)
            segment.write_bytes(b"segment")
            completed = invoke(
                "preflight",
                "--board",
                "t5ai-core",
                "--artifact-kind",
                "direct-segments",
                "--loader",
                str(loader),
                "--port",
                "COM7",
                "--segment",
                f"{segment}@0x0-0x7",
                "--start",
                "0x1000",
                "--allow-profile-override",
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("--start", completed.stderr)
            self.assertIn("single-image options", completed.stderr)

    def test_profile_override_is_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            image = root / "full.bin"
            fake_loader(loader, [], 0)
            image.write_bytes(b"firmware")
            base = (
                "preflight",
                "--board",
                "t5-board",
                "--transport",
                "single",
                "--artifact-kind",
                "direct-full",
                "--loader",
                str(loader),
                "--port",
                "COM3",
                "--image",
                str(image),
                "--baud",
                "460800",
            )
            refused = invoke(*base)
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("--allow-profile-override", refused.stderr)
            accepted = invoke(*base, "--allow-profile-override")
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            result = json.loads(accepted.stdout)
            self.assertEqual(result["profile_overrides"], ["baud"])

    def test_debug_plan_uses_generic_tool_and_t5core_rts_reset_port(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            completed = invoke(
                "debug-plan",
                "--board",
                "t5core",
                "--console-port",
                "COM11",
                "--reset-port",
                "COM7",
                "--action",
                "serial-pulse",
                "--output-dir",
                temporary,
                "--expected-regex",
                "NuttShell",
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(completed.stdout)
            command = result["command"]
            self.assertIn("windows-hardware-debug", command[0])
            self.assertEqual(command[command.index("--baud") + 1], "460800")
            self.assertEqual(command[command.index("--pulse-mode") + 1], "RTS")
            self.assertIn("--allow-target-control", command)

    def test_debug_plan_rejects_aidk_serial_pulse(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            completed = invoke(
                "debug-plan",
                "--board",
                "aidk",
                "--console-port",
                "COM8",
                "--action",
                "serial-pulse",
                "--output-dir",
                temporary,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("does not permit", completed.stderr)

    def test_debug_plan_uses_inline_rts_for_t5board(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            completed = invoke(
                "debug-plan",
                "--board",
                "t5board",
                "--console-port",
                "COM3",
                "--action",
                "serial-pulse",
                "--output-dir",
                temporary,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(completed.stdout)
            command = result["command"]
            self.assertEqual(command[command.index("--pulse-mode") + 1], "RTS")
            self.assertNotIn("--reset-port", command)

    def test_success_markers_override_advisory_exit_one(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            image = root / "full.bin"
            evidence = root / "evidence"
            image.write_bytes(b"firmware")
            fake_loader(
                loader,
                [
                    "Gotten Bus",
                    "EraseFlash ->pass",
                    "WriteFlash ->pass",
                    "Writing Flash OK",
                    "{All Finished Successfully}",
                ],
                1,
            )
            completed = invoke(
                "run",
                "--board",
                "t5-board",
                "--transport",
                "single",
                "--artifact-kind",
                "direct-full",
                "--loader",
                str(loader),
                "--image",
                str(image),
                "--port",
                "COM3",
                "--evidence-dir",
                str(evidence),
                "--execute",
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads((evidence / "result.json").read_text())
            self.assertEqual(result["status"], "passed")
            self.assertEqual(result["loader_exit_code"], 1)
            self.assertEqual(result["board_profile"], "t5_board")

    def test_getbus_failure_is_not_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "failure.log"
            log.write_text("GetBus fail\n", encoding="utf-8")
            completed = invoke("verify-log", "--log", str(log))
            self.assertEqual(completed.returncode, 2)
            result = json.loads(completed.stdout)
            self.assertEqual(result["status"], "failed")
            self.assertTrue(result["safe_prewrite_failure"])

    def test_last_session_ignores_older_failed_attempt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "append.log"
            log.write_text(
                "GetBus fail\nWriting Flash Failed, code 0\n"
                "Gotten Bus\nEraseFlash ->pass\nWriteFlash ->pass\n"
                "Writing Flash OK\n{All Finished Successfully}\n",
                encoding="utf-8",
            )
            completed = invoke(
                "verify-log", "--log", str(log), "--last-session"
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(completed.stdout)
            self.assertEqual(result["status"], "passed")
            self.assertEqual(result["scope"], "last-session")
            self.assertGreater(result["session_offset"], 0)

    def test_run_requires_explicit_execute_switch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            loader = root / "loader.sh"
            image = root / "full.bin"
            fake_loader(loader, [], 0)
            image.write_bytes(b"firmware")
            completed = invoke(
                "run",
                "--board",
                "t5-board",
                "--transport",
                "single",
                "--artifact-kind",
                "direct-full",
                "--loader",
                str(loader),
                "--image",
                str(image),
                "--port",
                "3",
                "--evidence-dir",
                str(root / "evidence"),
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertFalse((root / "evidence").exists())


if __name__ == "__main__":
    unittest.main()
