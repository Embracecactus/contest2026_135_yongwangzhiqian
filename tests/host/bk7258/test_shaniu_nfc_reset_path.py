#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Production filesystem admission and product cleanup, external peers only."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_nfc_rf_lifecycle import function, ROOT


def run(code):
    with tempfile.TemporaryDirectory() as d:
        p = Path(d)
        (p / "test.c").write_text(code)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-I" + str(ROOT / "app/bk7258"),
                str(p / "test.c"),
                "-o",
                str(p / "test"),
            ],
            check=True,
        )
        subprocess.run([str(p / "test")], check=True)


class NfcResetPathTest(unittest.TestCase):
    def test_filesystem(self):
        source = (ROOT / "app/bk7258/bk7258_provision_store.c").read_text()
        source = source.replace("int bkprov_store_check_filesystem(",
                                "static int bkprov_store_check_filesystem(")
        code = (
            r"""
#include <assert.h>
#include <errno.h>
#include <string.h>
#define bkprov_store_check_filesystem unused_store_declaration
#include "bk7258_provision_store.h"
#undef bkprov_store_check_filesystem
#define __NuttX__ 1
#define CONFIG_BK7258_RPMSGFS 1
#define CONFIG_BK7258_AP_CORE 1
#define LITTLEFS_SUPER_MAGIC 1
#define RPMSGFS_MAGIC 2
struct statfs { int f_type, f_blocks, f_bsize; };
static int blocks=100;
static int statfs(const char *root, struct statfs *fs) {
 (void)root;fs->f_type=RPMSGFS_MAGIC;fs->f_blocks=blocks;fs->f_bsize=512;return 0;
}
"""
            + function(source, "bkprov_store_check_filesystem")
            + r"""
int main(void) {
 assert(bkprov_store_check_filesystem("/cpdata/shaniu/nfc-cards")==0);
 assert(bkprov_store_check_filesystem("/cpdata/shaniu/pc-grants")==0);
 assert(bkprov_store_check_filesystem("/cpdata/shaniu/pc-grants-evil")==-EXDEV);
 assert(bkprov_store_check_filesystem("/cpdata/shaniu/pc-grants/../identity")==-EXDEV);
 assert(bkprov_store_check_filesystem("/cpdata/shaniu/nfc-cards-evil")==-EXDEV);
 assert(bkprov_store_check_filesystem("/cpdata/shaniu/nfc-cards/../identity")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT)==0);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_SCHEDULE)==0);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE "app-1")==0);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE "1234567890123456789012345678901")==0);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_LEGACY_STATE "app-1")==0);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_LEGACY_STATE "12345678901234567890123456")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT "/schedule/../identity")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT "/schedule-extra")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE)==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE "-bad")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE "../identity")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE "app/nested")==-EXDEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_STATE "12345678901234567890123456789012")==-EXDEV);
 blocks=0;assert(bkprov_store_check_filesystem("/cpdata/shaniu/nfc-cards")==-ENODEV);
 assert(bkprov_store_check_filesystem(BKPROV_NATIVE_ROOT BKPROV_NATIVE_SCHEDULE)==-ENODEV);
 return 0;
}
"""
        )
        run(code)

    def test_native_state_path_fits_target_inode(self):
        native = (ROOT / "app/bk7258/bk7258_native_apps.c").read_text()
        inode = (ROOT.parent / "nuttx/fs/inode/fs_inodesearch.c").read_text()
        code = (
            r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_provision_store.h"
#define BKNATIVE_ROOT BKPROV_NATIVE_ROOT
#define NAME_MAX 32
#define PATH_MAX 256
#define OK 0
"""
            + function(native, "state_path")
            + function(inode, "_inode_checkpath")
            + r"""
int main(void) {
 char id[32], path[96], file[128], other[96];
 for (size_t n=25; n<=31; n++) {
   memset(id, 'a', n); id[n]=0;
   state_path(id, path, sizeof(path));
   snprintf(file, sizeof(file), "%s/config.bin", path);
   assert(_inode_checkpath(path)==0 && _inode_checkpath(file)==0);
 }
 id[30]='b'; state_path(id, other, sizeof(other));
 assert(strcmp(path, other)!=0);
 snprintf(file, sizeof(file), BKNATIVE_ROOT "/state-%s/config.bin", id);
 assert(_inode_checkpath(file)==-ENAMETOOLONG);
 return 0;
}
"""
        )
        run(code)

    def test_cleanup(self):
        source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
        code = (
            r"""
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#define CONFIG_BK7258_NFC_SERVICE 1
#define CONFIG_BK7258_PREFERENCES 1
static int bkagent_memory_reset(void){return 0;}
static int bk7258_preferences_reset(void){return 0;}
#define CONFIG_BK7258_PROVISION_GATT 1
#define BKNFC_BINDINGS_ROOT "/cpdata/shaniu/nfc-cards"
static bool g_trigger_started;
static atomic_bool g_probe_running,g_voice_initialized;
static int stop_error, reset_error, resets;
static bool voice_channel_is_idle(void){return true;}
static int bk7258_nfc_service_quiesce(bool stop){assert(stop);return stop_error;}
static int bk7258_nfc_bindings_reset(void){resets++;return reset_error;}
"""
            + function(source, "product_reset_cleanup")
            + r"""
int main(void) {
 stop_error=-EBUSY;assert(product_reset_cleanup()==-EBUSY && resets==0);
 stop_error=0;reset_error=-EINPROGRESS;
 assert(product_reset_cleanup()==-EINPROGRESS && resets==1);
 reset_error=0;assert(product_reset_cleanup()==0 && resets==2);
 g_trigger_started=true;assert(product_reset_cleanup()==-EBUSY && resets==2);
 return 0;
}
"""
        )
        run(code)


if __name__ == "__main__":
    unittest.main()
