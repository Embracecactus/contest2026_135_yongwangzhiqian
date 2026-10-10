#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real V4L2 wait and shutdown bodies; only kernel I/O/clock peers replaced."""
from pathlib import Path
import subprocess
import tempfile
from test_nfc_rf_lifecycle import function, ROOT
from test_pack_trial import public_function

source = (ROOT / "app/bk7258/bk7258_vision_service.c").read_text()
bodies = (
    function(source, "bkvision_wait_frame")
    + "\n"
    + public_function(source, "bk7258_vision_quiesce")
)
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#define FAR
#define CONFIG_BK7258_VISION_CAPTURE_TIMEOUT_MS 3000
#define MSEC2TICK(x) (x)
#define VIDIOC_DQBUF 1
#define BKVISION_PC_CAMERA 1
struct v4l2_buffer {int unused;};
static clock_t tick;
static int io, closes, lock_result, busy;
static int g_bkvision_capture_lock;
static bool g_bkvision_quiesced;
static clock_t clock_systime_ticks(void) {return tick;}
static int bkvision_ioctl(int fd,int command,struct v4l2_buffer *buffer)
{(void)fd;(void)command;(void)buffer;io++;return -EAGAIN;}
static void nxsig_usleep(unsigned us) {tick+=us/1000;}
static bool canceled(void) {return tick>=5;}
static void bkcamera_close(void) {closes++;}
static bool bkcamera_busy(void) {return busy;}
static int nxmutex_trylock(void *p) {(void)p;return lock_result;}
static void nxmutex_unlock(void *p) {(void)p;}
"""
suffix = r"""
int main(void) {
 struct v4l2_buffer buffer;
 assert(bkvision_wait_frame(1,&buffer,canceled)==-ECANCELED);
 assert(tick==5 && io==1);
 io=0;assert(bkvision_wait_frame(1,&buffer,canceled)==-ECANCELED && io==0);
 tick=0;assert(bkvision_wait_frame(1,&buffer,NULL)==-ETIMEDOUT && tick==3000);
 busy=1;assert(bk7258_vision_quiesce(true)==-EAGAIN && !g_bkvision_quiesced);
 busy=0;lock_result=-EBUSY;assert(bk7258_vision_quiesce(true)==-EBUSY && !g_bkvision_quiesced);
 lock_result=0;assert(bk7258_vision_quiesce(true)==0 && g_bkvision_quiesced);
 assert(closes==3);assert(bk7258_vision_quiesce(false)==0 && !g_bkvision_quiesced);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="camera-v4l2-wait-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(prefix + bodies + suffix)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            path / "test.c",
            "-o",
            path / "test",
        ],
        check=True,
    )
    subprocess.run([path / "test"], check=True)
print("CAMERA_PRODUCTION_CANCEL_QUIESCE_PASS")
