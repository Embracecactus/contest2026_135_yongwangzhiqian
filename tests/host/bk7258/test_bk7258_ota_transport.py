#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run real OTA transport functions with deterministic host-only mocks.

No sockets, signing/TLS identities, RPMsg device or hardware reset are used.
The small source slices are included verbatim so mocks cannot replace the
production deadline, token validation or busy-ownership decisions.
"""

import os
import resource
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TESTS = Path(__file__).resolve().parent


def section(source, start, end):
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def run_fixture(fixture, includes):
    with tempfile.TemporaryDirectory(prefix="bk7258-ota-transport-") as name:
        work = Path(name)
        for filename, body in includes.items():
            (work / filename).write_text(body)
        binary = work / "test"
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-std=c11",
                "-D_POSIX_C_SOURCE=200809L",
                "-Wall",
                "-Wextra",
                "-Werror",
                *shlex.split(os.environ.get("OTA_TEST_CFLAGS", "")),
                "-I",
                str(work),
                str(TESTS / fixture),
                "-o",
                str(binary),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(binary)], check=True, timeout=5)


class OtaTransportTest(unittest.TestCase):
    def test_http_timeout(self):
        source = (ROOT / "chips/bk7258/ap/bk7258_ota_source_http.c").read_text()
        body = section(
            source,
            "static int bk7258_ota_http_io_remaining(",
            "static int bk7258_ota_http_wait_connected(",
        )
        run_fixture("test_bk7258_ota_http_timeout.c", {"http_io.inc": body})

    def test_reboot_prepare_commit(self):
        source = (ROOT / "chips/bk7258/common/bk7258_ota_rpmsg.c").read_text()
        worker = section(
            source,
            "static int bk7258_ota_rpmsg_lifecycle_worker(",
            "static int bk7258_ota_rpmsg_worker(",
        )
        receive = section(
            source,
            "  if (msg->header.command == BK7258_OTA_RPMSG_PAIR_STATUS ||",
            "  if (msg->header.command == BK7258_OTA_RPMSG_START)",
        )
        run_fixture(
            "test_bk7258_ota_reboot_race.c",
            {"lifecycle_worker.inc": worker, "lifecycle_receive.inc": receive},
        )


if __name__ == "__main__":
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    unittest.main()
