#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path
from test_bk7258_cloud_http import ROOT, APPS, build_http_fixture

HERE = Path(__file__).resolve().parent
CJSON = APPS / "netutils/cjson/cJSON"
with tempfile.TemporaryDirectory(prefix="bkcloud-fixture-http-") as directory:
    temp = Path(directory)
    binary = build_http_fixture(temp, HERE / "test_bk7258_cloud_fixture_http.c",
        (ROOT / "app/bk7258/bk7258_cloud_fixture.c",
         ROOT / "app/bk7258/bk7258_cloud_audio.c",
         ROOT / "app/bk7258/bk7258_cloud_request.c",
         ROOT / "app/bk7258/bk7258_cloud_tts.c", CJSON / "cJSON.c"),
        ("-I", str(CJSON), "-lm"))
    raise SystemExit(subprocess.run([str(binary)]).returncode)
