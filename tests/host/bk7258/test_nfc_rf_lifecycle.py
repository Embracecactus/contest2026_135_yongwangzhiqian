#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute production NFC idle/close paths with descriptor and RF failures."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def function(source, name):
    match = re.search(r"static int " + re.escape(name) + r"\([^;{}]*\)\s*\{", source)
    if match is None:
        raise ValueError("production function missing: " + name)
    start = match.start()
    brace = match.end() - 1
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RfLifecycleTest(unittest.TestCase):
    def test_idle_and_close_release_field_and_descriptor(self):
        source = (ROOT / "app/bk7258/bk7258_nfc_service.c").read_text()
        code = r"""
#include <assert.h>
#include <errno.h>
#include <string.h>
#define CONFIG_CL_MFRC522_FRAME 1
#define CONFIG_BK7258_NFC_DEVPATH "/dev/nfc0"
#define MFRC522IOC_SET_RF 14
#define O_RDONLY 0
struct bknfc_source_s { int fd; };
static int opens, releases, closes, open_error, rf_error, close_error;
static int open(const char *path, int flags) {
 assert(!strcmp(path,CONFIG_BK7258_NFC_DEVPATH) && flags==O_RDONLY);
 opens++; if(open_error){errno=open_error;return -1;}return 7;
}
static int ioctl(int fd, unsigned long command, int enabled) {
 assert(fd==7 && command==MFRC522IOC_SET_RF && enabled==0);
 releases++;if(rf_error){errno=rf_error;return -1;}return 0;
}
static int close(int fd) {
 assert(fd==7);closes++;if(close_error){errno=close_error;return -1;}return 0;
}
static int bknfc_errno(void){return errno>0?-errno:-EIO;}
"""
        code += function(source, "bknfc_close") + function(source, "bknfc_idle")
        code += r"""
int main(void) {
 struct bknfc_source_s s={-1};
 assert(bknfc_idle(&s)==0 && s.fd==-1);
 assert(opens==1 && releases==1 && closes==1);
 assert(bknfc_close(&s)==-EBADF && releases==1 && closes==1);
 s.fd=7;rf_error=EIO;
 assert(bknfc_close(&s)==-EIO && s.fd==-1 && closes==2);
 s.fd=7;close_error=EBADF;
 assert(bknfc_close(&s)==-EIO && closes==3);
 s.fd=7;rf_error=0;
 assert(bknfc_close(&s)==-EBADF && s.fd==-1 && closes==4);
 open_error=ENOENT;
 assert(bknfc_idle(&s)==-ENOENT && s.fd==-1 && closes==4);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)

    def test_controlled_open_failure_releases_partial_field(self):
        source = (ROOT / "app/bk7258/bk7258_nfc_service.c").read_text()
        code = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_CL_MFRC522_RF 1
#define CONFIG_BK7258_NFC_DEVPATH "/dev/nfc0"
#define MFRC522IOC_SET_RF 0x240f
#define O_RDONLY 0
struct bknfc_source_s { int fd; };
static int field, enables, disables, closes, fail_enable;
static int open(const char *p,int flags) { (void)p;(void)flags;return 7; }
static int close(int fd) { assert(fd==7);closes++;return 0; }
static int ioctl(int fd,int command,int on) {
 assert(fd==7 && command==MFRC522IOC_SET_RF);
 field=on;
 if(on) { enables++;if(fail_enable){errno=EIO;return -1;} }
 else disables++;
 return 0;
}
static int nxsig_usleep(unsigned n) { assert(n==6000);return 0; }
static int bknfc_errno(void) { return errno>0?-errno:-EIO; }
"""
        code += function(source, "bknfc_close") + function(source, "bknfc_open")
        code += r"""
int main(void) {
 struct bknfc_source_s s={-1};
 fail_enable=1;
 assert(bknfc_open(&s)==-EIO);
 assert(field==0 && enables==1 && disables==1 && closes==1 && s.fd==-1);
 fail_enable=0;
 assert(bknfc_open(&s)==0 && field==1);
 assert(bknfc_close(&s)==0 && field==0 && s.fd==-1);
 assert(enables==2 && disables==2 && closes==2);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "test.c"),
                    "-o",
                    str(path / "test"),
                ],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
