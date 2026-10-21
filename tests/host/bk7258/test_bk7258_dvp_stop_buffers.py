#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the production DVP external-stop and QBUF functions in a host seam."""
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SOURCE = (ROOT / "chips/bk7258/ap/bk7258_dvp.c").read_text()


def function(name: str) -> str:
    match = re.search(rf"static int {name}\([^;]*?\)\n{{", SOURCE, re.DOTALL)
    if match is None:
        raise RuntimeError(f"production function {name} not found")
    depth = 0
    for index in range(match.end() - 1, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}" and not (depth := depth - 1):
            return SOURCE[match.start() : index + 1]
    raise RuntimeError("unclosed production function")


stop = function("bk7258_dvp_stop_stream")
set_buf = function("bk7258_dvp_data_set_buf")
fixture = f"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define FAR
#define LPWORK 0
#define IMAGE_MJPEG 1
typedef int irqstate_t;
typedef int mutex_t;
typedef int pid_t;
typedef void *camera_handle_t;
typedef int imgdata_format_t;
struct work_s {{ int unused; }};
struct imgdata_s;
typedef int (*imgdata_capture_t)(int, unsigned int, void *, void *);
struct bk7258_dvp_s {{
  mutex_t api_lock; int lock; camera_handle_t handle; bool stopping, capture_active;
  imgdata_capture_t capture_cb; void *capture_arg; uint8_t *next_buffer;
  uint32_t next_size; bool suspended, work_queued, worker_running;
  pid_t worker_tid; struct work_s complete_work; struct {{ int img_format; }} sdk_config;
}};
struct imgdata_s {{ struct bk7258_dvp_s *priv; }};
static unsigned int joins, suspends;
static uint8_t *revoked;
static struct bk7258_dvp_s *joining;
static int nxmutex_lock(mutex_t *lock) {{ (void)lock; return 0; }}
static int nxmutex_unlock(mutex_t *lock) {{ (void)lock; return 0; }}
static irqstate_t spin_lock_irqsave(int *lock) {{ (void)lock; return 0; }}
static void spin_unlock_irqrestore(int *lock, irqstate_t flags) {{ (void)lock; (void)flags; }}
static bool bk7258_dvp_is_current_worker(struct bk7258_dvp_s *priv) {{ (void)priv; return false; }}
static void bk7258_dvp_drop_events_locked(struct bk7258_dvp_s *priv) {{ (void)priv; }}
static int bk7258_dvp_error(int result) {{ return result; }}
static int bk_dvp_suspend(camera_handle_t handle) {{ (void)handle; suspends++; return 0; }}
static int work_cancel_sync(int queue, struct work_s *work) {{
  (void)queue; (void)work; joins++; assert(revoked != NULL);
  assert(joining->next_buffer == NULL && joining->next_size == 0); return 0; }}
static struct bk7258_dvp_s *bk7258_dvp_from_data(struct imgdata_s *data)
{{ return data->priv; }}
static int bk7258_dvp_validate_frame_setting(struct bk7258_dvp_s *priv,
  uint8_t count, imgdata_format_t *formats, void *interval)
{{ (void)priv; (void)count; (void)formats; (void)interval; return 0; }}
{stop}
{set_buf}
int main(void)
{{ struct bk7258_dvp_s priv; struct imgdata_s data; uint8_t old_pool[8], new_pool[8];
   memset(&priv, 0, sizeof(priv)); priv.handle = &priv; priv.next_buffer = old_pool;
   priv.next_size = sizeof(old_pool); data.priv = &priv; revoked = priv.next_buffer;
   joining = &priv;
   assert(bk7258_dvp_stop_stream(&priv) == 0);
   assert(joins == 1 && suspends == 1);
   assert(priv.next_buffer == NULL && priv.next_size == 0 && !priv.stopping);
   assert(bk7258_dvp_data_set_buf(&data, 0, NULL, new_pool, sizeof(new_pool)) == 0);
   assert(priv.next_buffer == new_pool && priv.next_size == sizeof(new_pool));
   return 0; }}
"""

with tempfile.TemporaryDirectory(prefix="bk7258-dvp-stop-") as directory:
    path = Path(directory)
    source = path / "fixture.c"
    binary = path / "fixture"
    source.write_text(fixture)
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
