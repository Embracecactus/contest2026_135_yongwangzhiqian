#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile and run the real bounded TTS SSE decoder."""
import subprocess
import tempfile
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OPENVELA = ROOT.parent
MBEDTLS = OPENVELA / "apps/crypto/mbedtls/mbedtls"
CJSON = OPENVELA / "apps/netutils/cjson/cJSON"
CRYPTO = HERE / "build/voice-ota-store-mbedtls/library/libmbedcrypto.a"

# Reuse the maintained dependency recipe, including on a fresh host-test tree.
subprocess.run(["make", str(CRYPTO.relative_to(HERE))], cwd=HERE, check=True)

with tempfile.TemporaryDirectory(prefix="bkcloud-tts-bounds-") as directory:
    binary = Path(directory) / "tts-bounds"
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "app/bk7258"),
            "-I",
            str(MBEDTLS / "include"),
            "-I",
            str(CJSON),
            str(HERE / "test_bk7258_cloud_tts_boundaries.c"),
            str(ROOT / "app/bk7258/bk7258_cloud_tts.c"),
            str(ROOT / "app/bk7258/bk7258_cloud_request.c"),
            str(CJSON / "cJSON.c"),
            str(CRYPTO),
            "-lm",
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
