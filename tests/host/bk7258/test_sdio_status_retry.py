#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual CMD13 retry helper with deterministic SDK responses."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
s = (root / "chips/bk7258/ap/bk7258_sdio.c").read_text()
a = s.index("static bk_err_t bk7258_sdio_retry_status(")
b = s.index("\nstatic int bk7258_sdio_configure_pins", a)
code = (
    r"""#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define FAR
#define BK_OK 0
#define BK_ERR_SDIO_HOST_CMD_RSP_TIMEOUT -2
#define LOG_WARNING 1
#define syslog(...) ((void)0)
typedef int bk_err_t;
typedef struct {uint32_t cmd_index;} sdio_host_cmd_cfg_t;
static unsigned sent,waited,slept,failures;
static int start_error;
static int nxsig_usleep(unsigned us){assert(us==1000);slept++;return 0;}
static int bk_sdio_host_send_command(const sdio_host_cmd_cfg_t*c){assert(c->cmd_index==13);sent++;return start_error;}
static int bk7258_sdio_wait_command(uint32_t cmd){assert(cmd==13);waited++;return waited<=failures?-2:0;}
"""
    + s[a:b]
    + r"""
int main(void){
sdio_host_cmd_cfg_t c={24};assert(bk7258_sdio_retry_status(&c,-2)==-2 && sent==0);
c.cmd_index=13;assert(bk7258_sdio_retry_status(&c,-3)==-3 && sent==0);
assert(bk7258_sdio_retry_status(&c,0)==0 && sent==0);
failures=2;assert(bk7258_sdio_retry_status(&c,-2)==0 && sent==3 && waited==3 && slept==3);
sent=waited=slept=0;failures=99;assert(bk7258_sdio_retry_status(&c,-2)==-2 && sent==8 && waited==8 && slept==8);
sent=waited=slept=0;start_error=-4;assert(bk7258_sdio_retry_status(&c,-2)==-4 && sent==1 && waited==0);
puts("PASS: CMD13 transient recovery, bounded persistent timeout, data/CRC exclusions, send failure");
}
"""
)
with tempfile.TemporaryDirectory(prefix="sd-status-host-") as directory:
    p = Path(directory)
    (p / "test.c").write_text(code)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(p / "test.c"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True)
