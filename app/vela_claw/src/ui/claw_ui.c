/****************************************************************************
 * app/vela_claw/src/ui/claw_ui.c
 *
 * Screen UI front-end (LVGL + openvela UIKit) for Vela-Claw on the t5_board.
 *
 * It is an INPUT CHANNEL like the serial CLI: a "send" action packages the
 * textarea contents into a claw_event (platform "ui") and hands it to the
 * shared event router. Agent replies arrive through the registered "ui"
 * sender; because LVGL is not thread-safe, the sender only enqueues the text
 * and the UI thread draws it. The display (fb0) and touch (GT1151 /dev/input0)
 * ports are version-tolerant across LVGL 8 and 9.
 *
 * This file is only compiled when CONFIG_VELA_CLAW_UI is set (the LVGL/UIKit
 * headers and libraries are then available). Validate on the actual hardware
 * target — LVGL display/indev registration API differs between v8 and v9 and
 * the exact signatures below follow the documented common form.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <nuttx/config.h>
#include <nuttx/video/fb.h>
#include <nuttx/input/touchscreen.h>

#include <lvgl/lvgl.h>

#include "claw_common.h"
#include "claw_config.h"
#include "claw_event.h"
#include "claw_event_router.h"
#include "claw_rtos.h"
#include "claw_log.h"
#include "vela_claw_app.h"

/****************************************************************************
 * Display / touch port state
 ****************************************************************************/

#define VELA_UI_FBDEV   "/dev/fb0"
#define VELA_UI_TOUCHDEV "/dev/input0"

static int                 g_fb_fd   = -1;
static int                 g_touch_fd = -1;
static struct fb_videoinfo_s g_vinfo;
static struct fb_planeinfo_s g_pinfo;
static void               *g_fbmem;
static size_t              g_fbmem_size;
static lv_color_t         *g_draw_buf;

/* Response text queue (pointers to strdup'd strings) + UI thread. */
static claw_queue_t       *g_resp_q;
static claw_thread_t       g_ui_thread;
static volatile int        g_ui_running;

/* Chat widgets. */
static lv_obj_t           *g_list;
static lv_obj_t           *g_ta;
static lv_obj_t           *g_kb;

/****************************************************************************
 * LVGL display flush (copy LVGL render buffer -> framebuffer)
 ****************************************************************************/

#if LVGL_VERSION_MAJOR >= 9
static void fb_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
#else
static void fb_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                     lv_color_t *color_p)
#endif
{
  const uint8_t *src = (const uint8_t *)
#if LVGL_VERSION_MAJOR >= 9
    px_map;
#else
    color_p;
#endif

  int32_t w = lv_area_get_width(area);
  for (int32_t y = area->y1; y <= area->y2; y++)
    {
      uint8_t *dst = (uint8_t *)g_fbmem
                     + (size_t)g_pinfo.stride * y
                     + (size_t)area->x1 * sizeof(lv_color_t);
      memcpy(dst, src + (size_t)(y - area->y1) * w * sizeof(lv_color_t),
             (size_t)w * sizeof(lv_color_t));
    }

#if LVGL_VERSION_MAJOR >= 9
  lv_display_flush_ready(disp);
#else
  lv_disp_flush_ready(drv);
#endif
}

/****************************************************************************
 * LVGL touch input (GT1151 via /dev/input0)
 ****************************************************************************/

#if LVGL_VERSION_MAJOR >= 9
static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
#else
static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
#endif
{
  static int32_t last_x = 0;
  static int32_t last_y = 0;

  data->state = LV_INDEV_STATE_REL;

  if (g_touch_fd >= 0)
    {
      struct touch_sample_s sample;
      ssize_t n = read(g_touch_fd, &sample, sizeof(sample));
      if (n == (ssize_t)sizeof(sample))
        {
          if (sample.point[0].flags & TOUCH_DOWN)
            {
              last_x = sample.point[0].x;
              last_y = sample.point[0].y;
              data->state = LV_INDEV_STATE_PRESSED;
            }
          else if (sample.point[0].flags & TOUCH_MOVE)
            {
              last_x = sample.point[0].x;
              last_y = sample.point[0].y;
              data->state = LV_INDEV_STATE_PRESSED;
            }
        }
    }

  data->point.x = last_x;
  data->point.y = last_y;
}

/****************************************************************************
 * Chat UI
 ****************************************************************************/

static void append_message(const char *who, const char *text)
{
  if (!g_list) return;
  char line[1200];
  snprintf(line, sizeof(line), "[%s] %s", who ? who : "?", text ? text : "");
  lv_list_add_text(g_list, line);
#if LVGL_VERSION_MAJOR >= 9
  lv_obj_scroll_to_view(g_list, LV_ANIM_OFF);
#else
  lv_obj_scroll_to_bottom(g_list);
#endif
}

static void send_cb(lv_event_t *e)
{
  (void)e;
  const char *txt = lv_textarea_get_text(g_ta);
  if (!txt || !*txt) return;

  append_message("you", txt);

  claw_event_t ev;
  claw_event_init(&ev, CLAW_EVENT_MESSAGE, txt, "ui", "user", "default");
  claw_event_router_handle(&ev);

  lv_textarea_set_text(g_ta, "");
  if (g_kb) lv_keyboard_set_textarea(g_kb, NULL);
}

static void ta_focus_cb(lv_event_t *e)
{
  (void)e;
  if (g_kb) lv_keyboard_set_textarea(g_kb, g_ta);
}

static void build_chat_ui(int w, int h)
{
  lv_obj_t *scr = lv_scr_act();

  /* Scrollable message list fills most of the screen. */
  g_list = lv_list_create(scr);
  lv_obj_set_size(g_list, w, h - 80);
  lv_obj_align(g_list, LV_ALIGN_TOP_MID, 0, 0);
  append_message("vela", "Vela-Claw ready. Type and tap Send (or use the "
                        "serial CLI).");

  /* Input textarea. */
  g_ta = lv_textarea_create(scr);
  lv_obj_set_size(g_ta, w - 80, 60);
  lv_obj_align(g_ta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_textarea_set_placeholder_text(g_ta, "say something...");
  lv_obj_add_event_cb(g_ta, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

  /* Send button. */
  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 72, 60);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "Send");
  lv_obj_add_event_cb(btn, send_cb, LV_EVENT_CLICKED, NULL);

  /* On-screen keyboard (GT1151 touch). */
  g_kb = lv_keyboard_create(scr);
  lv_keyboard_set_textarea(g_kb, NULL);
  lv_keyboard_set_mode(g_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
}

/* Sender registered for platform "ui": enqueue the reply for the UI thread. */
static claw_err_t ui_sender(const char *text, const char *session)
{
  (void)session;
  if (!text || !g_resp_q) return CLAW_EINVAL;
  char *copy = strdup(text);
  if (!copy) return CLAW_ENOMEM;
  claw_queue_push(g_resp_q, &copy, 0);
  return CLAW_OK;
}

static void *ui_thread(void *arg)
{
  (void)arg;
  while (g_ui_running)
    {
      char *msg = NULL;
      if (claw_queue_pop(g_resp_q, &msg, 0) == CLAW_OK && msg)
        {
          append_message("agent", msg);
          free(msg);
        }
      lv_timer_handler();
      claw_sleep_ms(5);
    }
  return NULL;
}

claw_err_t vela_claw_ui_init(void)
{
  /* LVGL globals / library init (UIKit is not used here; its framework
   * sources do not compile in this tree). */
  lv_init();

  /* Open the framebuffer. */
  g_fb_fd = open(VELA_UI_FBDEV, O_RDWR);
  if (g_fb_fd < 0)
    {
      CLAW_LOGE("ui: cannot open %s", VELA_UI_FBDEV);
      return CLAW_EIO;
    }
  ioctl(g_fb_fd, FBIOGET_VIDEOINFO, (unsigned long)((uintptr_t)&g_vinfo));
  ioctl(g_fb_fd, FBIOGET_PLANEINFO, (unsigned long)((uintptr_t)&g_pinfo));

  g_fbmem_size = (size_t)g_pinfo.stride * g_vinfo.yres;
  g_fbmem = mmap(NULL, g_fbmem_size,
                 PROT_READ | PROT_WRITE, MAP_SHARED, g_fb_fd, 0);
  if (g_fbmem == MAP_FAILED)
    {
      CLAW_LOGE("ui: fb mmap failed");
      return CLAW_EIO;
    }

  int w = g_vinfo.xres;
  int h = g_vinfo.yres;
  size_t buf_px = (size_t)w * h;
  g_draw_buf = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
  if (!g_draw_buf) return CLAW_ENOMEM;

#if LVGL_VERSION_MAJOR >= 9
  lv_draw_buf_t dbuf;
  lv_draw_buf_init(&dbuf, w, h, LV_COLOR_FORMAT_RGB565,
                   g_pinfo.stride, g_draw_buf, buf_px * sizeof(lv_color_t));
  lv_display_t *disp = lv_display_create(w, h);
  lv_display_set_flush_cb(disp, fb_flush);
  lv_display_set_draw_buffers(disp, &dbuf, NULL);
  lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
  static lv_disp_draw_buf_t dbuf;
  lv_disp_draw_buf_init(&dbuf, g_draw_buf, NULL, buf_px);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.flush_cb = fb_flush;
  drv.draw_buf = &dbuf;
  drv.hor_res = w;
  drv.ver_res = h;
  lv_disp_drv_register(&drv);
#endif

  /* Touch input. */
  g_touch_fd = open(VELA_UI_TOUCHDEV, O_RDONLY | O_NONBLOCK);
  if (g_touch_fd < 0)
    CLAW_LOGW("ui: cannot open %s (touch disabled)", VELA_UI_TOUCHDEV);

#if LVGL_VERSION_MAJOR >= 9
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read);
#else
  static lv_indev_drv_t idrv;
  lv_indev_drv_init(&idrv);
  idrv.type = LV_INDEV_TYPE_POINTER;
  idrv.read_cb = touch_read;
  lv_indev_drv_register(&idrv);
#endif

  build_chat_ui(w, h);

  g_resp_q = claw_queue_create(8, sizeof(char *));
  claw_event_router_set_sender("ui", ui_sender);

  g_ui_running = 1;
  claw_thread_create(&g_ui_thread, ui_thread, NULL, 0, 16 * 1024);

  CLAW_LOGI("ui: screen UI up (%dx%d)", w, h);
  return CLAW_OK;
}

void vela_claw_ui_deinit(void)
{
  g_ui_running = 0;
  if (g_resp_q)
    {
      claw_queue_push(g_resp_q, &(char *){NULL}, 0);
      claw_thread_join(g_ui_thread);
      claw_queue_destroy(g_resp_q);
      g_resp_q = NULL;
    }
  if (g_fbmem != MAP_FAILED && g_fbmem) munmap(g_fbmem, g_fbmem_size);
  if (g_fb_fd >= 0) close(g_fb_fd);
  if (g_touch_fd >= 0) close(g_touch_fd);
  lv_deinit();
}
