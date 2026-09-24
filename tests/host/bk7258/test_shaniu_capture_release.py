#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify capture release oracles against isolated, compilable single faults."""
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
AGENT = HERE.parents[3] / "packages/ai_agent"


def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    source = AGENT / "src/voice/audio_capture.c"
    original = source.read_text()
    faults = {
        "close-failure": (
            "int ret = close_media_recorder(cap, timeout_ms);\n            if (ret < 0) return ret;",
            "int ret = close_media_recorder(cap, timeout_ms);\n            (void)ret;",
        ),
        "route-failure": (
            "int ret = release_capture_route(cap, timeout_ms);\n    if (ret < 0) return ret;",
            "int ret = release_capture_route(cap, timeout_ms);\n    (void)ret;",
        ),
    }
    results = []
    with tempfile.TemporaryDirectory(prefix="capture-release-") as directory:
        folder = Path(directory)
        (folder / "voice").mkdir()
        for variant, (old, new) in faults.items():
            assert original.count(old) == 1
            for phase in ("mutant", "restored"):
                content = original.replace(old, new) if phase == "mutant" else original
                (folder / "voice/audio_capture.c").write_text(content)
                command = [
                    "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O2",
                    "-pthread",
                    "-DTEST_AGENT_CAPTURE",
                    str(HERE / "test_bk7258_agent_media_recorder.c"),
                    "-I",
                    str(folder),
                    "-I",
                    str(HERE / "mocks"),
                    "-I",
                    str(AGENT / "include"),
                    "-I",
                    str(AGENT / "src"),
                    "-o",
                    str(folder / "probe"),
                ]
                build = subprocess.run(
                    command, capture_output=True, text=True, timeout=30
                )
                run = (
                    None
                    if build.returncode
                    else subprocess.run(
                        [str(folder / "probe"), variant],
                        capture_output=True,
                        text=True,
                        timeout=10,
                    )
                )
                detected = (
                    run is not None
                    and run.returncode == -6
                    and "audio_capture_cleanup(1) == -EIO" in run.stderr
                )
                passed = (
                    run is not None
                    and run.returncode == 0
                    and "CONTRACT_PASS" in run.stdout
                )
                results.append(
                    dict(
                        id=variant + "." + phase,
                        status=(
                            ("DETECTED" if detected else "INVALID_MUTATION")
                            if phase == "mutant"
                            else ("PASS" if passed else "FAIL")
                        ),
                        build_exit=build.returncode,
                        exit_code=None if run is None else run.returncode,
                        output=build.stderr if run is None else run.stdout + run.stderr,
                        source_sha256=hashlib.sha256(content.encode()).hexdigest(),
                    )
                )
    assert source.read_text() == original
    print(json.dumps(results, indent=2))
    return 0 if all(r["status"] in ("DETECTED", "PASS") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
