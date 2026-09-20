#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual single-write cleanup against a bounded card model."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "chips/bk7258/ap/bk7258_sdio.c").read_text()
start = source.index("static int bk7258_sdio_finish_single_transfer(")
end = source.index("\n#ifdef CONFIG_SDIO_V2P0\nstatic void", start)
body = source[start:end]
prefix = r"""
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#define CONFIG_SDIO_V2P0 1
#define FAR
#define OK 0
#define BK_OK 0
#define SDIO_HOST_CMD_RSP_SHORT 1
#define SDIO_HOST_RSP0 0
#define SDIOWAIT_TRANSFERDONE 1
#define SDIOWAIT_ERROR 2
typedef int bk_err_t;
typedef struct {unsigned cmd_index,response,wait_rsp_timeout;bool crc_check;}
sdio_host_cmd_cfg_t;
struct bk7258_sdio_priv_s {
  bool single_write,single_read,xfer_pending;
  unsigned cmd_timeout,events;
  int xfer_result;
};
static unsigned stops,waits,tails,resets;
static int send_error,wait_error;
static uint32_t status;
static void bk_sdio_host_reset_sd_state(void) {resets++;}
static void bk_sdio_clk_gate_config(unsigned v) {assert(v==1);}
static int bk_sdio_host_send_command(const sdio_host_cmd_cfg_t *c) {
  assert(c->cmd_index==12 && c->crc_check && c->wait_rsp_timeout==20000);
  assert(c->response==SDIO_HOST_CMD_RSP_SHORT);stops++;return send_error;
}
static int bk7258_sdio_wait_command(unsigned c) {
  assert(c==12);waits++;return wait_error;
}
static uint32_t bk_sdio_host_get_cmd_rsp_argument(unsigned r) {
  assert(r==0);return status;
}
static void bk7258_sdio_finish_stop_transmission(void) {tails++;}
static int bk7258_sdio_map_err(int e) {return e;}
"""
suffix = r"""
int main(void) {
  for (unsigned test=0;test<10;test++) {
    struct bk7258_sdio_priv_s p={.single_write=true,.xfer_pending=true,
                                .cmd_timeout=20000};
    stops=waits=tails=resets=0;send_error=wait_error=0;status=0x900;
    int original=0,want=0;
    if(test==1)send_error=want=-ETIMEDOUT;
    if(test==2)wait_error=want=-ETIMEDOUT;
    if(test==3){status=0x80000900;want=-EIO;}
    if(test==4){original=want=-EFAULT;wait_error=-ETIMEDOUT;}
    if(test==5)p.single_write=false;
    if(test==6){p.single_write=false;p.single_read=true;}
    if(test==7)original=want=-EIO;
    if(test==8)original=want=-EINVAL;
    if(test==9){p.single_write=false;p.single_read=true;original=want=-EIO;}
#ifdef CONFIG_SDIO_GDMA_EN
    bool need_stop=test!=5 && test!=6;
    if(!need_stop)want=original;
#else
    bool need_stop=test!=5 && original!=0;
    if(!need_stop)want=original;
#endif
    assert(bk7258_sdio_finish_single_transfer(&p,original)==want);
    assert(!p.single_write && !p.single_read && !p.xfer_pending && p.xfer_result==want);
    assert(p.events==(want==0?SDIOWAIT_TRANSFERDONE:SDIOWAIT_ERROR));
    assert(stops==(unsigned)need_stop && tails==stops && resets==stops);
    assert(waits==(unsigned)(need_stop && test!=1));
    unsigned before=stops;
    bk7258_sdio_finish_single_transfer(&p,0);assert(stops==before);
  }
#ifdef CONFIG_SDIO_GDMA_EN
  puts("PASS: DMA TX retains STOP; CPU RX keeps native CMD17 and aborts on error");
#else
  puts("PASS: native CPU singles omit STOP on success and abort on error");
#endif
}
"""
with tempfile.TemporaryDirectory(prefix="sdio-single-write-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(prefix + body + suffix)
    for defines in ([], ["-DCONFIG_SDIO_GDMA_EN=1"]):
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                *defines,
                str(path / "test.c"),
                "-o",
                str(path / "test"),
            ],
            check=True,
        )
        subprocess.run([str(path / "test")], check=True)
