#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the maintained cloud HTTP adapter with fragmented verified I/O."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
APPS = ROOT.parent / "apps"
MBEDTLS = APPS / "crypto/mbedtls/mbedtls"


class CloudHttpTest(unittest.TestCase):
    def test_http_adapter(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "nuttx").mkdir()
            (temp / "netutils").mkdir()
            (temp / "nuttx/config.h").write_text(
                "#include <nuttx/compiler.h>\n#define OK 0\n#define ERROR -1\n"
                "#define CONFIG_WEBCLIENT_MAXHOSTNAME 128\n"
                "#define CONFIG_WEBCLIENT_MAXFILENAME 256\n"
            )
            (temp / "nuttx/version.h").write_text("")
            (temp / "debug.h").write_text(
                "#include <assert.h>\n#include <string.h>\n"
                "static inline size_t strlcpy(char *d,const char *s,size_t n) "
                "{size_t k=strlen(s);if(n){size_t c=k<n-1?k:n-1;memcpy(d,s,c);d[c]=0;}return k;}\n"
                "#define DEBUGASSERT assert\n#define ninfo(...) ((void)0)\n"
                "#define nerr(...) ((void)0)\n#define nwarn(...) ((void)0)\n"
            )
            (temp / "netutils/netlib.h").write_text(
                "#include <stdint.h>\nstruct url_s {char *scheme;int schemelen;"
                "char *host;int hostlen;uint16_t port;char *path;int pathlen;};\n"
                "int netlib_parseurl(const char *,struct url_s *);\n"
            )
            executable = temp / "test"
            build = temp / "mbedtls"
            subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(MBEDTLS),
                    "-B",
                    str(build),
                    "-DENABLE_TESTING=OFF",
                    "-DENABLE_PROGRAMS=OFF",
                    "-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes",
                ],
                check=True,
            )
            subprocess.run(
                ["cmake", "--build", str(build), "--parallel", "2"], check=True
            )
            subprocess.run(
                [
                    "cc",
                    "-std=gnu11",
                    "-D_GNU_SOURCE",
                    "-pthread",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-I",
                    str(temp),
                    "-I",
                    str(ROOT / "tests/host/bk7258/mocks"),
                    "-I",
                    str(APPS / "include"),
                    "-I",
                    str(ROOT / "app/bk7258"),
                    "-I",
                    str(MBEDTLS / "include"),
                    str(ROOT / "app/bk7258/bk7258_cloud_http.c"),
                    str(ROOT / "app/bk7258/bk7258_cloud_config.c"),
                    str(ROOT / "tests/host/bk7258/test_bk7258_cloud_http.c"),
                    str(APPS / "netutils/webclient/webclient.c"),
                    str(APPS / "netutils/netlib/netlib_parseurl.c"),
                    "-L",
                    str(build / "library"),
                    "-lmbedcrypto",
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
