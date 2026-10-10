/* SPDX-License-Identifier: Apache-2.0 */
/* Single bounded request, executed only by the existing vision worker. */
#include <nuttx/config.h>
#include <nuttx/spinlock.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bk7258_pc_camera.h"
#include "bk7258_vision_service.h"

#define CAMERA_CAPACITY 102400u
enum { EMPTY, PENDING, RUNNING, READY, FAILED, CANCELED, EXPIRED };
static spinlock_t g_lock = SP_UNLOCKED;
static uint64_t g_id, g_last, g_deadline, g_captured;
static uint8_t g_nonce[16];
static uint8_t *g_frame;
static struct bkvision_rpc_response_s g_meta;
static unsigned int g_phase;
static int g_error;
static uint32_t g_elapsed;
static bool g_admitted, g_cancel, g_working;

static uint64_t now_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint64_t get(const uint8_t *p, unsigned int n)
{
  uint64_t value = 0;
  while (n--) value = (value << 8) | *p++;
  return value;
}

static void put(uint8_t *p, uint64_t value, unsigned int n)
{
  while (n) { p[--n] = value; value >>= 8; }
}

static void dispose(uint8_t *frame)
{
  if (frame != NULL)
    {
      volatile uint8_t *bytes = frame;
      for (size_t i = 0; i < CAMERA_CAPACITY; i++) bytes[i] = 0;
      free(frame);
    }
}

static uint8_t *discard(unsigned int phase, int error)
{
  uint8_t *frame = g_frame;
  g_frame = NULL;
  memset(&g_meta, 0, sizeof(g_meta));
  g_cancel = true;
  g_phase = phase;
  g_error = error;
  return frame;
}

void bkcamera_step(uint64_t now, bool admitted)
{
  irqstate_t flags;
  uint8_t *frame = NULL;
  flags = spin_lock_irqsave(&g_lock);
  bool rollback = now < g_last;
  g_admitted = admitted && !rollback;
  if (g_phase == PENDING || g_phase == RUNNING || g_phase == READY)
    {
      if (!g_admitted) frame = discard(CANCELED, -ECANCELED);
      else if (now >= g_deadline) frame = discard(EXPIRED, -ETIMEDOUT);
    }
  if (now >= g_last) g_last = now;
  spin_unlock_irqrestore(&g_lock, flags);
  dispose(frame);
}

void bkcamera_close(void)
{
  irqstate_t flags;
  uint8_t *frame;
  flags = spin_lock_irqsave(&g_lock);
  g_admitted = false;
  frame = discard(CANCELED, -ECANCELED);
  spin_unlock_irqrestore(&g_lock, flags);
  dispose(frame);
}

bool bkcamera_busy(void)
{
  irqstate_t flags;
  flags = spin_lock_irqsave(&g_lock);
  bool busy = g_working || g_phase == PENDING;
  spin_unlock_irqrestore(&g_lock, flags);
  return busy;
}

static bool canceled(void)
{
  irqstate_t flags;
  uint64_t now = now_ms();
  flags = spin_lock_irqsave(&g_lock);
  bool cancel = g_cancel || !g_admitted || now < g_last || now >= g_deadline;
  spin_unlock_irqrestore(&g_lock, flags);
  return cancel;
}

bool bkcamera_work(void)
{
  irqstate_t flags;
  struct bkvision_rpc_response_s meta;
  uint8_t *frame;
  uint64_t started = now_ms();
  flags = spin_lock_irqsave(&g_lock);
  if (g_phase != PENDING || g_working)
    {
      spin_unlock_irqrestore(&g_lock, flags);
      return false;
    }
  g_working = true;
  g_phase = RUNNING;
  spin_unlock_irqrestore(&g_lock, flags);

  memset(&meta, 0, sizeof(meta));
  frame = malloc(CAMERA_CAPACITY);
  int ret = frame == NULL ? -ENOMEM : canceled() ? -ECANCELED :
    bk7258_vision_pc_capture(frame, CAMERA_CAPACITY, &meta, canceled);
  uint64_t ended = now_ms();
  flags = spin_lock_irqsave(&g_lock);
  bool expired = ended < started || ended >= g_deadline;
  if (ret == 0 && (meta.bytes_used < 4 || meta.bytes_used > CAMERA_CAPACITY ||
                  meta.pixel_format != BKVISION_PIXEL_FORMAT_JPEG ||
                  meta.width == 0 || meta.height == 0)) ret = -EPROTO;
  if (ret == -ECANCELED && expired) ret = -ETIMEDOUT;
  if (ret == 0 && (g_cancel || !g_admitted || expired))
    ret = expired ? -ETIMEDOUT : -ECANCELED;
  if (ret == 0)
    {
      g_meta = meta;
      g_frame = frame;
      frame = NULL;
      g_captured = ended;
      g_elapsed = ended - started;
      g_deadline = ended + 120000;
      g_phase = READY;
      g_error = 0;
    }
  else
    {
      g_phase = ret == -ECANCELED ? CANCELED :
                ret == -ETIMEDOUT ? EXPIRED : FAILED;
      g_error = ret;
    }
  g_working = false;
  spin_unlock_irqrestore(&g_lock, flags);
  dispose(frame);
  return true;
}

int bkcamera_control(enum bkcontrol_command_e command, uint32_t kind,
  uint32_t offset, const uint8_t *record, size_t size,
  struct bkcontrol_status_s *status)
{
  irqstate_t flags;
  uint64_t now = now_ms();
  uint8_t *discarded = NULL;
  uint8_t snapshot[80] = {0};
  int ret = -EINVAL;
  bool wake = false;
  if (status == NULL) return -EINVAL;
  flags = spin_lock_irqsave(&g_lock);
  if (command == BKCONTROL_CONFIG_READ && kind == BKCONTROL_CONFIG_CAMERA)
    {
      if (size || offset >= sizeof(snapshot) || offset % 16) goto out;
      memcpy(snapshot, "CCS1", 4);
      put(snapshot + 4, g_phase, 4);
      put(snapshot + 8, g_id, 8);
      memcpy(snapshot + 16, g_nonce, 16);
      put(snapshot + 32, g_captured, 8);
      put(snapshot + 40, g_meta.bytes_used, 4);
      put(snapshot + 44, g_meta.width, 4);
      put(snapshot + 48, g_meta.height, 4);
      put(snapshot + 52, g_meta.pixel_format, 4);
      put(snapshot + 56, (uint32_t)g_error, 4);
      bool valid = g_phase == READY && g_frame != NULL && g_admitted &&
                   now >= g_last && now < g_deadline;
      put(snapshot + 60, g_admitted | (valid ? 2 : 0), 4);
      put(snapshot + 64, g_phase == READY ? CONFIG_BK7258_VISION_FPS : 0, 4);
      put(snapshot + 68, g_meta.capture_sequence, 4);
      put(snapshot + 72, g_elapsed, 4);
      put(snapshot + 76, valid ? g_deadline - now : 0, 4);
      status->config_total = sizeof(snapshot);
      memcpy(status->config_chunk, snapshot + offset, 16);
      ret = 0;
    }
  else if (command == BKCONTROL_CONFIG_READ &&
           kind == BKCONTROL_CONFIG_CAMERA_FRAME)
    {
      if (record == NULL || size != 16 || offset) goto out;
      uint32_t pos = get(record + 8, 4);
      if (get(record, 8) != g_id || g_phase != READY || !g_admitted ||
          g_frame == NULL || now < g_last || now >= g_deadline ||
          get(record + 12, 4) != g_meta.bytes_used)
        { ret = -ESTALE; goto out; }
      if (pos % 16 || pos >= g_meta.bytes_used) goto out;
      size_t count = g_meta.bytes_used - pos;
      if (count > 16) count = 16;
      status->config_total = g_meta.bytes_used;
      memset(status->config_chunk, 0, 16);
      memcpy(status->config_chunk, g_frame + pos, count);
      ret = 0;
    }
  else if (kind == BKCONTROL_CONFIG_CAMERA && size == 32)
    {
      if (command == BKCONTROL_CONFIG_BEGIN) { ret = 0; goto out; }
      if (command != BKCONTROL_CONFIG_APPLY || record == NULL ||
          memcmp(record, "CCQ1", 4)) goto out;
      unsigned int action = get(record + 4, 4);
      if (get(record + 8, 8) != g_id) { ret = -ESTALE; goto out; }
      if (action == 2)
        { discarded = discard(CANCELED, -ECANCELED); ret = 0; goto out; }
      uint8_t any = 0;
      for (unsigned int i = 16; i < 32; i++) any |= record[i];
      if (action != 1 || !any) goto out;
      if (!g_admitted || now < g_last || g_working || g_phase == READY ||
          g_phase == PENDING || g_id == UINT64_MAX)
        { ret = -EBUSY; goto out; }
      g_id++;
      g_phase = PENDING;
      g_cancel = false;
      g_error = 0;
      g_deadline = now + 8000;
      g_captured = g_elapsed = 0;
      memset(&g_meta, 0, sizeof(g_meta));
      memcpy(g_nonce, record + 16, 16);
      wake = true;
      ret = 0;
    }
out:
  spin_unlock_irqrestore(&g_lock, flags);
  dispose(discarded);
  if (wake)
    {
      ret = bk7258_vision_pc_wake();
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&g_lock);
          discarded = discard(FAILED, ret);
          spin_unlock_irqrestore(&g_lock, flags);
          dispose(discarded);
        }
    }
  return ret;
}
