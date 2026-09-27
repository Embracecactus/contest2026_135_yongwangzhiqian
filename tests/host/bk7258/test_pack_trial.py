#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile actual renderer/cache/volume bodies with external mount/FB peers."""
from pathlib import Path
import re
import os
import json
import hashlib
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "app/bk7258"
HERE = ROOT / "tests/host/bk7258"


def main():
    source = (APP / "bk7258_display_service.c").read_text()
    fragments = []
    for marker in (
        "struct bkdisplay_service_s\n",
        "enum bkdisplay_diagnostic_stage_e\n",
    ):
        begin = source.index(marker)
        end = source.index("};", begin) + 2
        fragments.append(source[begin:end])
    for name in (
        "bkdisplay_service_errno",
        "bkdisplay_service_retryable",
        "bkdisplay_blockdev_acquire",
        "bkdisplay_blockdev_release",
        "bkdisplay_volume_open",
        "bkdisplay_volume_close",
        "bkdisplay_diagnostic_stage_name",
        "bkdisplay_diagnostic_failure",
        "bkdisplay_status_error",
        "bkdisplay_cache_frames",
        "bkdisplay_render_pack_pixels_locked",
    ):
        match = re.search(r"^static [^\n]*\b" + name + r"\([^;]*?\)\n\{", source, re.M)
        if not match:
            raise RuntimeError(
                "Required production renderer interface missing: " + name
            )
        start = match.start()
        pos = match.end()
        depth = 1
        while depth:
            depth += (source[pos] == "{") - (source[pos] == "}")
            pos += 1
        fragments.append(source[start:pos])
    with tempfile.TemporaryDirectory(prefix="pack-trial-build-") as d:
        temp = Path(d)
        (temp / "pack-trial-render.inc").write_text("\n".join(fragments))
        # Every palette entry is green: expected RGB565 is independently 0x07e0
        # for every rendered pixel, regardless of expression geometry.
        spec = json.loads((APP / "assets/display/shaniu-default-v1.json").read_text())
        spec["pack_id"] = "shaniu-upload-v1"
        spec["palette"] = ["#00FF00"] * len(spec["palette"])
        source_file = temp / "green.json"
        source_file.write_text(json.dumps(spec))
        fixture = temp / "green.bkep"
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/bk7258/bk7258.py"),
                "package",
                "eye-pack",
                "--source",
                str(source_file),
                "--output",
                str(fixture),
            ],
            check=True,
        )
        print(
            "PACK_TRIAL_INPUT_SHA256="
            + hashlib.sha256(fixture.read_bytes()).hexdigest(),
            flush=True,
        )
        subprocess.run(
            [
                "cc",
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pthread",
                "-I",
                str(temp),
                "-I",
                str(HERE / "mocks"),
                "-I",
                str(APP),
                str(HERE / "test_pack_trial.c"),
                str(APP / "bk7258_display_store.c"),
                str(APP / "bk7258_display_pack.c"),
                str(APP / "bk7258_media_volume.c"),
                str(APP / "bk7258_display_trial_control.c"),
                str(APP / "bk7258_control_session.c"),
                "-Wl,--wrap=write,--wrap=fsync",
                "-o",
                str(temp / "test"),
            ],
            check=True,
        )
        if sys.argv[1].startswith("pc-"):
            command = [
                str(temp / "test"),
                str(HERE / "build/shaniu-default-v1.bkep"),
                str(fixture),
                "--peer",
            ]
            result = subprocess.run(
                [
                    sys.executable,
                    str(HERE / "test_workbench_trial.py"),
                    "PackWireTest.test_" + sys.argv[1].removeprefix("pc-"),
                ],
                env=dict(os.environ, SHANIU_PACK_TEST_PEER=json.dumps(command)),
                timeout=20,
            )
            return result.returncode
        result = subprocess.run(
            [
                str(temp / "test"),
                str(HERE / "build/shaniu-default-v1.bkep"),
                str(fixture),
                sys.argv[1],
            ],
            timeout=20,
        )
        return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError, OSError) as e:
        print("SETUP_ERROR:", e, file=sys.stderr)
        raise SystemExit(2)
