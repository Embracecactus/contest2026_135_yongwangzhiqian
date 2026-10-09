#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
OPENVELA = ROOT.parent
MBEDTLS = OPENVELA / "apps/crypto/mbedtls/mbedtls"
CJSON = OPENVELA / "apps/netutils/cjson/cJSON"

with tempfile.TemporaryDirectory(prefix="bkcloud-fixture-") as temp:
    binary = Path(temp) / "fixture"
    command = ["cc", "-std=gnu11", "-pthread", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
        "-I", str(ROOT / "app/bk7258"),
        "-I", str(MBEDTLS / "include"), "-I", str(CJSON),
        str(HERE / "test_bk7258_cloud_fixture.c"),
        str(ROOT / "app/bk7258/bk7258_cloud_fixture.c"),
        str(ROOT / "app/bk7258/bk7258_cloud_tts.c"),
        str(ROOT / "app/bk7258/bk7258_cloud_request.c"), str(CJSON / "cJSON.c"),
        str(MBEDTLS / "library/base64.c"), str(MBEDTLS / "library/constant_time.c"),
        str(MBEDTLS / "library/platform_util.c"), "-lm", "-o", str(binary)]
    built = subprocess.run(command, text=True, capture_output=True)
    if built.returncode:
        sys.stderr.write(built.stderr)
        raise SystemExit(built.returncode)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
