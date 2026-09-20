#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Test exact-name maintenance deletion in a disposable host directory."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_camera_control_patches import function

REPO = Path(__file__).resolve().parents[3]
APP = REPO / "app/bk7258"


class RemoveRecord(unittest.TestCase):
    def test_parser_and_exact_file_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "nuttx").mkdir()
            (root / "nuttx/config.h").write_text("")
            main = root / "cli.o"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Dmain=bkvision_cli",
                    "-I",
                    str(root),
                    "-I",
                    str(APP),
                    "-c",
                    str(APP / "bk7258_vision_main.c"),
                    "-o",
                    str(main),
                ],
                check=True,
            )
            source = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include "bk7258_vision_core.h"
#define FAR
#define BKVISION_RECORD_ROOT "."
static int mounts, closes, exchanges;
static int bkvision_volume_open(void) { mounts++; return 0; }
static int bkvision_volume_close(void) { closes++; return 0; }
static void bkvision_log_storage(const char *s) { (void)s; }
static int bkvision_errno(void) { return -errno; }
static void bkvision_operation_failed(struct bkvision_rpc_response_s *r, int e) { r->operation_status=e; r->flags=0; }
"""
            source += function(
                (APP / "bk7258_vision_service.c").read_text(),
                "static int bkvision_remove_record(",
            )
            source += r"""
int bkvision_rpc_exchange(struct bkvision_rpc_request_s *q, struct bkvision_rpc_response_s *r, unsigned int timeout) {
  (void)timeout; exchanges++;
  assert(q->command==BKVISION_RPC_REMOVE_RECORD && q->duration_ms==0x28012486 && q->reserved==2);
  q->magic=BKVISION_RPC_MAGIC; q->version=BKVISION_RPC_VERSION; q->session_id=9; q->sequence=7;
  assert(bkvision_rpc_request_valid(q));
  bkvision_rpc_make_response(r,q,0);
  return bkvision_remove_record(q,r);
}
int bkvision_cli(int argc, char **argv);
static void create(const char *path) { FILE *f=fopen(path,"w"); assert(f); fputs("fixture",f); fclose(f); }
int main(void) {
  char *args[]={"bkvision","remove-record","28012486","00000002",0};
  create("video-28012486-00000002.avi"); create("video-28012486-00000003.avi"); create("keep.txt");
  assert(bkvision_cli(4,args)==0);
  assert(access("video-28012486-00000002.avi",F_OK)<0);
  assert(access("video-28012486-00000003.avi",F_OK)==0 && access("keep.txt",F_OK)==0);
  assert(bkvision_cli(4,args)!=0); /* absent is an error, no other file selected */
  const char *bad[]={"../00002","00000000","-0000001","0000000*","100000000"," 0000002"};
  for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
    args[3]=(char *)bad[i]; int before=exchanges;
    assert(bkvision_cli(4,args)!=0 && exchanges==before);
  }
  args[3]="00000002";
  assert(mkdir("video-28012486-00000002.avi",0700)==0);
  assert(bkvision_cli(4,args)!=0);
  assert(access("video-28012486-00000002.avi",F_OK)==0);
  assert(mounts==closes);
  return 0;
}
"""
            p = root / "test.c"
            p.write_text(source)
            binary = root / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(APP),
                    str(p),
                    str(main),
                    str(APP / "bk7258_vision_core.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], cwd=root, check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
