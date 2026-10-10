/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bk7258_pc_camera.h"
#include "bk7258_vision_service.h"

static uint64_t now = 1000;
static unsigned int captures, wakes;
static int fault;

int fixture_clock_gettime(clockid_t clock, struct timespec *ts)
{
  (void)clock; ts->tv_sec = now / 1000; ts->tv_nsec = (now % 1000) * 1000000;
  return 0;
}
int bk7258_vision_pc_wake(void) { wakes++; return 0; }
int bk7258_vision_pc_capture(uint8_t *data, size_t capacity,
  struct bkvision_rpc_response_s *view, bool (*canceled)(void))
{
  captures++; assert(capacity == 102400); assert(!canceled());
  if (fault == 1) { bkcamera_close(); assert(canceled()); return -ECANCELED; }
  if (fault == 2) return -EIO;
#ifdef BKCAMERA_BACKEND_ONLY
#include "fixtures/camera-synthetic-jpeg.h"
#define jpeg camera_synthetic_jpeg
#else
  const uint8_t jpeg[] = {255,216,1,2,255,217};
#endif
  memcpy(data, jpeg, sizeof(jpeg));
  view->bytes_used = sizeof(jpeg); view->width = 640; view->height = 480;
#ifdef BKCAMERA_BACKEND_ONLY
  view->width = 2; view->height = 2;
#endif
  view->pixel_format = BKVISION_PIXEL_FORMAT_JPEG; view->capture_sequence = 17;
  return 0;
}
#ifndef BKCAMERA_BACKEND_ONLY
static void put(uint8_t *p, uint64_t value, unsigned int n)
{ while (n) { p[--n] = value; value >>= 8; } }
static uint64_t get(const uint8_t *p, unsigned int n)
{ uint64_t v = 0; while (n--) v = (v << 8) | *p++; return v; }
static int request(unsigned int action, uint64_t id)
{
  uint8_t record[32] = "CCQ1";
  struct bkcontrol_status_s status;
  put(record + 4, action, 4); put(record + 8, id, 8); record[31] = id + 1;
  return bkcamera_control(BKCONTROL_CONFIG_APPLY, BKCONTROL_CONFIG_CAMERA,
                           0, record, sizeof(record), &status);
}
static void snapshot(uint8_t *out)
{
  struct bkcontrol_status_s status;
  for (unsigned int offset = 0; offset < 80; offset += 16)
    {
      assert(bkcamera_control(BKCONTROL_CONFIG_READ, BKCONTROL_CONFIG_CAMERA,
                               offset, NULL, 0, &status) == 0);
      assert(status.config_total == 80); memcpy(out + offset, status.config_chunk, 16);
    }
}
int main(void)
{
  uint8_t view[80], selector[16] = {0}; struct bkcontrol_status_s status;
  assert(request(1, 0) == -EBUSY); assert(captures == 0);
  bkcamera_step(now, true); assert(request(1, 0) == 0); assert(wakes == 1);
  assert(request(1, 0) == -ESTALE); snapshot(view); assert(get(view + 4, 4) == 1);
  assert(captures == 0); assert(request(2, 1) == 0); assert(!bkcamera_work());
  assert(request(1, 1) == 0); assert(bkcamera_work()); assert(captures == 1);
  snapshot(view); assert(get(view + 4, 4) == 3); assert(get(view + 40, 4) == 6);
  assert(get(view + 44, 4) == 640); assert(get(view + 68, 4) == 17);
  put(selector, 2, 8); put(selector + 12, 6, 4);
  assert(bkcamera_control(BKCONTROL_CONFIG_READ, BKCONTROL_CONFIG_CAMERA_FRAME,
    0, selector, 16, &status) == 0); assert(status.config_chunk[0] == 255);
  selector[7] = 1;
  assert(bkcamera_control(BKCONTROL_CONFIG_READ, BKCONTROL_CONFIG_CAMERA_FRAME,
    0, selector, 16, &status) == -ESTALE);
  assert(request(1, 2) == -EBUSY); now += 120001; bkcamera_step(now, true);
  snapshot(view); assert(get(view + 4, 4) == 6); assert(get(view + 40, 4) == 0);
  assert(request(1, 2) == 0); fault = 1; assert(bkcamera_work());
  snapshot(view); assert(get(view + 4, 4) == 5); assert(!bkcamera_busy());
  bkcamera_step(now, true); assert(request(1, 3) == 0); fault = 2; assert(bkcamera_work());
  snapshot(view); assert(get(view + 4, 4) == 4); assert(get(view + 40, 4) == 0);
  assert(request(1, 4) == 0); now--; bkcamera_step(now, true);
  assert(!bkcamera_work()); snapshot(view); assert(get(view + 4, 4) == 5);
  bkcamera_step(++now, true); assert(request(1, 5) == 0);
  bkcamera_step(now + 8001, true); assert(!bkcamera_work());
  snapshot(view); assert(get(view + 4, 4) == 6); bkcamera_close();
  puts("PC_CAMERA_PRODUCTION_PASS"); return 0;
}

#endif
