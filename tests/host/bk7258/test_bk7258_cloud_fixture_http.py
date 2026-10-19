#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path
from test_bk7258_cloud_http import ROOT, APPS, build_http_fixture

HERE = Path(__file__).resolve().parent
CJSON = APPS / "netutils/cjson/cJSON"


def serve(args):
    from voice_tts_relay import serve_cli
    serve_cli(args)


def fixture_test():
    with tempfile.TemporaryDirectory(prefix="bkcloud-fixture-http-") as directory:
        temp = Path(directory)
        binary = build_http_fixture(temp, HERE / "test_bk7258_cloud_fixture_http.c",
            (ROOT / "app/bk7258/bk7258_cloud_fixture.c",
             ROOT / "app/bk7258/bk7258_cloud_audio.c",
             ROOT / "app/bk7258/bk7258_cloud_request.c",
             ROOT / "app/bk7258/bk7258_cloud_tts.c", CJSON / "cJSON.c"),
            ("-I", str(CJSON), "-lm"))
        return subprocess.run([str(binary)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serve", action="store_true")
    parser.add_argument("--input-file")
    parser.add_argument("--cloud-config")
    parser.add_argument("--response-file")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--listen", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--report-dir")
    parser.add_argument("--mode", choices=("A", "B", "C"))
    args = parser.parse_args()
    if args.serve:
        required = ("input_file", "cloud_config", "response_file", "cert", "key",
                    "report_dir", "mode")
        if any(getattr(args, item) is None for item in required):
            parser.error("--serve needs input/config/response/cert/key/report/mode")
        serve(args)
        return 0
    return fixture_test()


if __name__ == "__main__":
    raise SystemExit(main())
