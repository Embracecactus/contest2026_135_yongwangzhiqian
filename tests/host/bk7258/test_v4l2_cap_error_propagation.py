#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the changed NuttX V4L2 start/caller functions in a host seam."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_NUTTX = ROOT.parent / "nuttx"


def extract(source: str, name: str) -> str:
    match = re.search(rf"static int {name}\([^;]*?\)\n{{", source, re.S)
    if match is None:
        raise RuntimeError(f"production function {name} was not found")
    depth = 0
    for pos in range(match.end() - 1, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}" and (depth := depth - 1) == 0:
            return source[match.start() : pos + 1]
    raise RuntimeError(f"production function {name} is not closed")


parser = argparse.ArgumentParser()
parser.add_argument("--nuttx", type=Path, default=DEFAULT_NUTTX)
args = parser.parse_args()
source = (args.nuttx / "drivers/video/v4l2_cap.c").read_text()
start = extract(source, "start_capture")
change = extract(source, "change_capture_state")
dqbuf = extract(source, "capture_dqbuf")
streamon = extract(source, "capture_streamon")

fixture = f"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define OK 0
#define MAX_CAPTURE_FMT 2
#define CAPTURE_FMT_MAIN 0
#define CAPTURE_FMT_SUB 1
#define IMGDATA_FMT_MAIN 0
#define IMGDATA_FMT_SUB 1
#define IMGSENSOR_FMT_MAIN 0
#define IMGSENSOR_FMT_SUB 1
#define IMGSENSOR_STREAM_TYPE_VIDEO 1
#define IMGSENSOR_STREAM_TYPE_STILL 2
#define V4L2_BUF_TYPE_VIDEO_CAPTURE 1
#define V4L2_BUF_TYPE_STILL_CAPTURE 2
#define O_NONBLOCK 04000
#define ASSERT(x) assert(x)
#define DEBUGASSERT(x) assert(x)
typedef int mutex_t;
typedef int sem_t;
typedef int enum_dummy_t;
typedef unsigned int uint;
enum v4l2_buf_type {{ V4L2_VIDEO = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                      V4L2_STILL = V4L2_BUF_TYPE_STILL_CAPTURE }};
enum capture_state_e {{ CAPTURE_STATE_STREAMOFF, CAPTURE_STATE_STREAMON,
                       CAPTURE_STATE_CAPTURE }};
enum capture_state_cause_e {{ CAUSE_CAPTURE_STOP, CAUSE_CAPTURE_START,
                              CAUSE_CAPTURE_DQBUF }};
enum capture_waitend_cause_e {{ WAITEND_CAUSE_CAPTUREDONE,
                                WAITEND_CAUSE_DQCANCEL,
                                WAITEND_CAUSE_STILLSTOP }};
struct v4l2_rect {{ uint16_t width, height; }};
struct v4l2_fract {{ uint32_t numerator, denominator; }};
struct v4l2_buffer {{ uint32_t type, length; union {{ uintptr_t userptr; }} m; }};
typedef struct {{ uint16_t width, height; uint32_t pixelformat, sizeimage; }} video_format_t;
typedef struct {{ int ignored; }} imgdata_format_t;
typedef struct {{ int ignored; }} imgsensor_format_t;
typedef struct {{ int ignored; }} imgdata_interval_t;
typedef struct {{ int ignored; }} imgsensor_interval_t;
typedef struct vbuf_container_s {{ struct v4l2_buffer buf; }} vbuf_container_t;
typedef struct {{ vbuf_container_t *vacant; vbuf_container_t *valid; }} video_framebuff_t;
struct capture_wait_capture_s {{ sem_t dqbuf_wait_flg; vbuf_container_t *done_container;
 enum capture_waitend_cause_e waitend_cause; }};
typedef struct capture_type_inf_s {{ mutex_t lock_state; enum capture_state_e state;
 struct capture_wait_capture_s wait_capture; uint8_t nr_fmt;
 video_format_t fmt[MAX_CAPTURE_FMT]; struct v4l2_rect clip;
 struct v4l2_fract frame_interval; video_framebuff_t bufinf; uint32_t seqnum; }} capture_type_inf_t;
struct imgdata_s {{ int ignored; }}; struct imgsensor_s {{ int ignored; }};
typedef struct capture_mng_s {{ capture_type_inf_t capture_inf, still_inf;
 struct imgdata_s *imgdata; struct imgsensor_s *imgsensor; mutex_t mutex; }} capture_mng_t;
struct inode {{ void *i_private; }}; struct file {{ struct inode *f_inode; int f_oflags; }};
static int set_ret, data_start_ret, sensor_start_ret, data_stop_calls, sensor_stop_calls;
static int set_calls, data_start_calls, sensor_start_calls, wait_calls;
static int destination_accepted;
static int mock_set(void) {{ set_calls++; if (!set_ret) destination_accepted = 1; return set_ret; }}
static int mock_data_start(void) {{ data_start_calls++; return data_start_ret; }}
static int mock_sensor_start(void) {{ sensor_start_calls++; return sensor_start_ret; }}
static void mock_data_stop(void) {{ data_stop_calls++; destination_accepted = 0; }}
static void mock_sensor_stop(void) {{ sensor_stop_calls++; }}
#define IMGDATA_SET_BUF(a,b,c,d,e) mock_set()
#define IMGDATA_START_CAPTURE(a,b,c,d,e,f) mock_data_start()
#define IMGSENSOR_START_CAPTURE(a,b,c,d,e) mock_sensor_start()
#define IMGDATA_STOP_CAPTURE(a) mock_data_stop()
#define IMGSENSOR_STOP_CAPTURE(a,b) mock_sensor_stop()
static void get_clipped_format(uint8_t n, video_format_t *a, struct v4l2_rect *b, video_format_t *c)
{{ (void)n; (void)b; memcpy(c, a, sizeof(video_format_t) * MAX_CAPTURE_FMT); }}
static void convert_to_imgdatafmt(video_format_t *a, imgdata_format_t *b) {{ (void)a; (void)b; }}
static void convert_to_imgdatainterval(struct v4l2_fract *a, imgdata_interval_t *b) {{ (void)a; (void)b; }}
static void convert_to_imgsensorfmt(video_format_t *a, imgsensor_format_t *b) {{ (void)a; (void)b; }}
static void convert_to_imgsensorinterval(struct v4l2_fract *a, imgsensor_interval_t *b) {{ (void)a; (void)b; }}
static int complete_capture(uint8_t a, uint32_t b, const void *c, void *d)
{{ (void)a; (void)b; (void)c; (void)d; return 0; }}
static vbuf_container_t *video_framebuff_get_vacant_container(video_framebuff_t *b) {{ return b->vacant; }}
static vbuf_container_t *video_framebuff_dq_valid_container(video_framebuff_t *b) {{ return b->valid; }}
static void video_framebuff_free_container(video_framebuff_t *a, vbuf_container_t *b) {{ (void)a; (void)b; }}
static int nxmutex_lock(mutex_t *a) {{ (void)a; return 0; }}
static int nxmutex_unlock(mutex_t *a) {{ (void)a; return 0; }}
static int nxsem_wait_uninterruptible(sem_t *a) {{ (void)a; wait_calls++; return 0; }}
static capture_type_inf_t *get_capture_type_inf(capture_mng_t *m, uint8_t type)
{{ return type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? &m->capture_inf :
         type == V4L2_BUF_TYPE_STILL_CAPTURE ? &m->still_inf : 0; }}
static enum capture_state_e estimate_next_capture_state(capture_mng_t *m, enum capture_state_cause_e c)
{{ (void)m; return c == CAUSE_CAPTURE_STOP ? CAPTURE_STATE_STREAMOFF : CAPTURE_STATE_CAPTURE; }}
static int is_taking_still_picture(capture_mng_t *m) {{ (void)m; return 0; }}
{start}
static void stop_capture(capture_mng_t *m, enum v4l2_buf_type t)
{{ (void)t; mock_data_stop(); mock_sensor_stop(); (void)m; }}
{change}
{dqbuf}
{streamon}
static void reset(void)
{{ set_ret = data_start_ret = sensor_start_ret = 0; data_stop_calls = sensor_stop_calls = 0;
   set_calls = data_start_calls = sensor_start_calls = wait_calls = 0; destination_accepted = 0; }}
static void setup(capture_mng_t *m, vbuf_container_t *v)
{{ memset(m, 0, sizeof(*m)); m->imgdata = (void *)1; m->imgsensor = (void *)1;
   m->capture_inf.state = CAPTURE_STATE_STREAMON; m->capture_inf.bufinf.vacant = v;
   v->buf.m.userptr = 0x1234; v->buf.length = 64; }}
static void failed_start(capture_mng_t *m, vbuf_container_t *v, int stage)
{{ reset(); setup(m, v); if (stage == 0) set_ret = -EIO;
   if (stage == 1) data_start_ret = -EIO;
   if (stage == 2) sensor_start_ret = -EIO;
   assert(change_capture_state(m, CAPTURE_STATE_CAPTURE) == -EIO);
   assert(m->capture_inf.state == CAPTURE_STATE_STREAMON);
   assert(destination_accepted == 0 && data_stop_calls == 1);
   assert(set_calls == 1 && data_start_calls == (stage == 0 ? 0 : 1));
   assert(sensor_start_calls == (stage < 2 ? 0 : 1));
   assert(sensor_stop_calls == (stage == 2 ? 1 : 0)); }}
int main(void)
{{ capture_mng_t m; vbuf_container_t v; struct inode inode = {{ .i_private = &m }};
   struct file file = {{ .f_inode = &inode }}; struct v4l2_buffer b = {{ .type = V4L2_BUF_TYPE_VIDEO_CAPTURE }};
   enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
   failed_start(&m, &v, 0); failed_start(&m, &v, 1); failed_start(&m, &v, 2);
   reset(); setup(&m, &v); assert(change_capture_state(&m, CAPTURE_STATE_CAPTURE) == 0);
   assert(m.capture_inf.state == CAPTURE_STATE_CAPTURE && destination_accepted == 1);
   reset(); setup(&m, &v); m.capture_inf.state = CAPTURE_STATE_STREAMOFF; set_ret = -EIO;
   assert(capture_streamon(&file, &type) == -EIO && m.capture_inf.state == CAPTURE_STATE_STREAMOFF);
   reset(); setup(&m, &v); set_ret = -EIO;
   assert(capture_dqbuf(&file, &b) == -EIO && wait_calls == 0);
   return 0; }}
"""

with tempfile.TemporaryDirectory(prefix="v4l2-cap-propagation-") as directory:
    path = Path(directory)
    c_file, binary = path / "fixture.c", path / "fixture"
    c_file.write_text(fixture)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-Wno-unused-function",
            str(c_file),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
