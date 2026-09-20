/****************************************************************************
 * app/dolphin/dolphin_ui.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>
#include <syslog.h>
#include <unistd.h>

#ifdef CONFIG_BK7258_GT1151
#  include <nuttx/input/touchscreen.h>
#endif

#include <lvgl/lvgl.h>

#include "dolphin_ui.h"
#ifdef CONFIG_DOLPHIN_RECORDER
#  include "dolphin_recording.h"
#endif
#ifdef CONFIG_DOLPHIN_ADC_KEY
#  include "dolphin_adc_key.h"
#endif
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
#  include <arch/chip/bk7258_lvgl_fb.h>
#endif
#if defined(CONFIG_BK7258_BT_IPC) && defined(CONFIG_WIRELESS_BLUETOOTH_HOST)
#  include <arch/chip/bk7258_ble_scan.h>
#  define DOLPHIN_HAS_BLE_SCAN 1
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
#  include <arch/chip/bk7258_wifi.h>
#endif
#ifdef CONFIG_BK7258_USBHOST
#  include <arch/chip/bk7258_usbhost.h>
#endif

#ifndef CONFIG_DOLPHIN_STORAGE_ROOT
#  define CONFIG_DOLPHIN_STORAGE_ROOT "/mnt/tf"
#endif

#define DOLPHIN_UI_PRIORITY       45
#ifndef CONFIG_DOLPHIN_UI_STACKSIZE
#  define CONFIG_DOLPHIN_UI_STACKSIZE 16384
#endif
#define DOLPHIN_UI_STACKSIZE      CONFIG_DOLPHIN_UI_STACKSIZE
#define DOLPHIN_MAX_FILES         64
#define DOLPHIN_MAX_ENUMERATED    256
#define DOLPHIN_NAME_SIZE         48
#define DOLPHIN_PATH_SIZE         192
#define DOLPHIN_PREVIEW_BYTES     2048
#define DOLPHIN_REPORT_BYTES      2048
#define DOLPHIN_REPORT_ATTEMPTS   100u

struct dolphin_file_s
{
  char name[DOLPHIN_NAME_SIZE];
  off_t size;
  bool directory;
};

struct dolphin_scan_s
{
  pthread_mutex_t lock;
  struct dolphin_file_s files[DOLPHIN_MAX_FILES];
  char relative[DOLPHIN_PATH_SIZE];
  unsigned int count;
  int error;
  bool more;
  bool truncated;
  uint64_t capacity_total;
  uint64_t capacity_free;
  int capacity_error;
  bool running;
  bool complete;
};

struct dolphin_preview_s
{
  pthread_mutex_t lock;
  char relative[DOLPHIN_PATH_SIZE];
  char name[DOLPHIN_NAME_SIZE];
  char text[DOLPHIN_PREVIEW_BYTES + 1];
  off_t size;
  size_t length;
  unsigned int request;
  unsigned int completed_request;
  int error;
  bool truncated;
  bool running;
  bool complete;
};

struct dolphin_report_s
{
  pthread_mutex_t lock;
  char text[DOLPHIN_REPORT_BYTES];
  char job_text[DOLPHIN_REPORT_BYTES];
  char path[DOLPHIN_PATH_SIZE];
  unsigned int sequence;
  uint32_t page_generation;
  int error;
  bool running;
  bool complete;
};

static struct dolphin_scan_s g_scan =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};
static struct dolphin_preview_s g_preview =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};
static struct dolphin_report_s g_report =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};

static bool dolphin_report_append_line(char *report, size_t capacity,
                                       size_t *used, const char *line,
                                       size_t reserve)
{
  size_t length = strlen(line);
  size_t remaining;

  if (*used > capacity)
    {
      return false;
    }
  remaining = capacity - *used;
  if (length > remaining || remaining - length < 2 ||
      reserve > remaining - length - 2)
    {
      return false;
    }
  memcpy(report + *used, line, length);
  *used += length;
  report[(*used)++] = '\n';
  report[*used] = '\0';
  return true;
}
#ifdef DOLPHIN_HOST_TEST
static volatile bool g_report_test_hold;
#endif
#ifdef DOLPHIN_HAS_BLE_SCAN
static lv_obj_t *g_ble_page;
static lv_obj_t *g_ble_status;
static lv_obj_t *g_ble_results;
static uint32_t g_ble_started;
static bool g_ble_owned;
static bool g_ble_stop_sent;
static struct bk7258_ble_scan_snapshot_s g_ble_snapshot;
static bool g_ble_snapshot_valid;
static void dolphin_ble(lv_event_t *event);
#endif
static bool g_ui_started;
static lv_obj_t *g_page;
#ifdef CONFIG_DOLPHIN_ADC_KEY
static lv_group_t *g_adc_key_group;
#endif
static lv_obj_t *g_files_page;
static lv_obj_t *g_preview_page;
static lv_display_t *g_display;
#ifdef CONFIG_BK7258_GT1151
static int g_touch_fd = -1;
static lv_point_t g_touch_point;
static lv_indev_state_t g_touch_state = LV_INDEV_STATE_RELEASED;

/* 当前官方 GT9xx 只返回单点样本，没有 TSIOC_GETMAXPOINTS。
 * 通过 LVGL 输入扩展点消费标准样本，不替换或复制公共驱动。 */

static void dolphin_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  struct touch_sample_s sample;

  (void)indev;
  if (read(g_touch_fd, &sample, sizeof(sample)) == sizeof(sample) &&
      sample.npoints == 1)
    {
      if (sample.point[0].flags & TOUCH_POS_VALID)
        {
          g_touch_point.x = sample.point[0].x;
          g_touch_point.y = sample.point[0].y;
        }

      if (sample.point[0].flags & TOUCH_UP)
        {
          g_touch_state = LV_INDEV_STATE_RELEASED;
        }
      else if (sample.point[0].flags & (TOUCH_DOWN | TOUCH_MOVE))
        {
          g_touch_state = LV_INDEV_STATE_PRESSED;
        }
    }

  data->point = g_touch_point;
  data->state = g_touch_state;
}

static void dolphin_touch_close(lv_event_t *event)
{
  (void)event;
  close(g_touch_fd);
  g_touch_fd = -1;
  g_touch_state = LV_INDEV_STATE_RELEASED;
}

static lv_indev_t *dolphin_touch_create(const char *path)
{
  lv_indev_t *indev;

  g_touch_fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (g_touch_fd < 0)
    {
      syslog(LOG_ERR, "dolphin-ui: touch open failed: %d\n", errno);
      return NULL;
    }

  indev = lv_indev_create();
  if (indev == NULL)
    {
      dolphin_touch_close(NULL);
      return NULL;
    }

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, dolphin_touch_read);
  lv_indev_add_event_cb(indev, dolphin_touch_close, LV_EVENT_DELETE, NULL);
  return indev;
}
#endif

static uint32_t g_page_generation;
static void dolphin_report_timer(lv_timer_t *timer);
static void dolphin_report_save(lv_event_t *event);
#ifdef CONFIG_DOLPHIN_RECORDER
static lv_obj_t *g_recording_page;
static lv_obj_t *g_recording_status;
static uint32_t g_recording_generation;
static char g_recording_path[DOLPHIN_PATH_SIZE];
static unsigned int g_recording_sequence;
static int g_recording_ui_error;
static void dolphin_recording_page(lv_event_t *event);
static void dolphin_recording_timer(lv_timer_t *timer);
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
static lv_obj_t *g_wifi_page;
static uint32_t g_wifi_ticket;
static uint32_t g_wifi_connect_ticket;
static uint32_t g_wifi_connect_generation;
static lv_obj_t *g_wifi_connect_page;
static char g_wifi_connect_ssid[BK7258_WIFI_SSID_MAX_LEN + 1];
static uint8_t g_wifi_connect_security;
static lv_obj_t *g_wifi_connect_password;
static lv_obj_t *g_wifi_connect_keyboard;
static lv_obj_t *g_wifi_connect_status;
static uint32_t g_channels_ticket;
static lv_obj_t *g_channels_page;
static uint32_t g_gateway_ticket;
static lv_obj_t *g_gateway_page;
static struct bk7258_wifi_scan_snapshot_s g_wifi_snapshot;
static bool g_wifi_snapshot_valid;
static void dolphin_channels(lv_event_t *event);
static void dolphin_channels_switch(lv_event_t *event);
static void dolphin_gateway_check(lv_event_t *event);
static void dolphin_gateway_timer(lv_timer_t *timer);
static void dolphin_wifi_scan(lv_event_t *event);
static void dolphin_wifi_detail(lv_event_t *event);
static void dolphin_wifi_connect(lv_event_t *event);
static void dolphin_wifi_connect_timer(lv_timer_t *timer);
static void dolphin_wifi_clear_password(bool destroy_text);
#endif
#ifdef CONFIG_BK7258_USBHOST
static void dolphin_usbhost(lv_event_t *event);
#endif

static void dolphin_home(lv_event_t *event);
static void dolphin_files(lv_event_t *event);
static void dolphin_network(lv_event_t *event);
static void dolphin_device(lv_event_t *event);
static void dolphin_files_up(lv_event_t *event);
static void dolphin_preview_timer(lv_timer_t *timer);

static void dolphin_label(lv_obj_t *parent, const char *text, int32_t size)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  if (size > 0)
    {
      lv_obj_set_width(label, size);
      lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
}

static lv_obj_t *dolphin_page(const char *title)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *header;
  lv_obj_t *home;
  lv_obj_t *title_label;
  lv_obj_t *home_label;
  int32_t width = lv_display_get_horizontal_resolution(g_display);
  int32_t height = lv_display_get_vertical_resolution(g_display);

  g_page_generation++;
  if (g_page_generation == 0)
    {
      g_page_generation = 1;
    }

#ifdef CONFIG_DOLPHIN_RECORDER
  if (g_recording_page != NULL)
    {
      struct dolphin_recording_snapshot_s snapshot;

      if (dolphin_recording_snapshot(&snapshot) == OK &&
          (snapshot.state == DOLPHIN_RECORDING_STARTING ||
           snapshot.state == DOLPHIN_RECORDING_ACTIVE ||
           snapshot.state == DOLPHIN_RECORDING_STOPPING))
        {
          (void)dolphin_recording_stop();
        }
      g_recording_page = NULL;
      g_recording_status = NULL;
      g_recording_generation = 0;
      g_recording_ui_error = 0;
    }
#endif
#ifdef DOLPHIN_HAS_BLE_SCAN
  if (g_ble_page != NULL && g_ble_owned && !g_ble_stop_sent)
    {
      (void)bk7258_ble_scan_stop();
      g_ble_stop_sent = true;
    }
  g_ble_page = NULL;
#endif
#ifdef CONFIG_DOLPHIN_ADC_KEY
  if (g_adc_key_group != NULL) lv_group_remove_all_objs(g_adc_key_group);
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  if (g_wifi_connect_password != NULL)
    dolphin_wifi_clear_password(true);
#endif
  lv_obj_clean(screen);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  g_files_page = NULL;
  g_preview_page = NULL;
#ifdef CONFIG_BK7258_WIFI_VNET
  if (g_wifi_page != NULL && g_wifi_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_wifi_ticket);
  if (g_wifi_connect_page != NULL && g_wifi_connect_ticket != 0)
    (void)bk7258_wifi_connect_cancel(g_wifi_connect_ticket);
  if (g_channels_page != NULL && g_channels_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_channels_ticket);
  if (g_gateway_page != NULL && g_gateway_ticket != 0)
    (void)bk7258_wifi_ping_cancel(g_gateway_ticket);
  g_wifi_page = NULL;
  g_channels_page = NULL;
  g_gateway_page = NULL;
#endif
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101516), LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  header = lv_obj_create(screen);
  lv_obj_set_size(header, width, 48);
  lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x1a2224), LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
  title_label = lv_label_create(header);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xff9d22), LV_PART_MAIN);
  lv_obj_set_style_text_font(title_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 12, 0);
  home = lv_button_create(header);
  lv_obj_set_size(home, 76, 36);
  lv_obj_align(home, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_set_style_bg_color(home, lv_color_hex(0xf08a24), LV_PART_MAIN);
  lv_obj_set_style_radius(home, 8, LV_PART_MAIN);
  lv_obj_add_event_cb(home, dolphin_home, LV_EVENT_CLICKED, NULL);
  home_label = lv_label_create(home);
  lv_label_set_text(home_label, "HOME");
  lv_obj_set_style_text_font(home_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(home_label, lv_color_hex(0x252525), LV_PART_MAIN);
  lv_obj_center(home_label);
  g_page = lv_obj_create(screen);
  lv_obj_set_size(g_page, width, height - 48);
  lv_obj_set_pos(g_page, 0, 48);
  lv_obj_set_style_bg_color(g_page, lv_color_hex(0x151d1f), LV_PART_MAIN);
  lv_obj_set_style_border_width(g_page, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_page, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_page, 12, LV_PART_MAIN);
  lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  return g_page;
}

static lv_obj_t *dolphin_button(lv_obj_t *parent, const char *text,
                                lv_event_cb_t callback, void *data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *label = lv_label_create(button);

  lv_obj_set_size(button, LV_PCT(94), 52);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x202b2e), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(0xc66b19), LV_PART_MAIN |
                             LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(button, lv_color_hex(0xbd6d25), LV_PART_MAIN);
  lv_obj_set_style_text_color(button, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
#ifdef CONFIG_DOLPHIN_ADC_KEY
  if (g_adc_key_group != NULL) lv_group_add_obj(g_adc_key_group, button);
#endif
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

static void dolphin_feature_card(lv_obj_t *parent, const char *title,
                                 const char *detail, lv_event_cb_t callback)
{
  lv_obj_t *card = dolphin_button(parent, title, callback, NULL);
  lv_obj_t *title_label = lv_obj_get_child(card, 0);
  lv_obj_t *detail_label = lv_label_create(card);

  lv_obj_set_height(card, 72);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 14, 10);
  lv_obj_set_width(title_label, LV_PCT(88));
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(title_label, 0, LV_PART_MAIN);
  lv_obj_remove_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);
  lv_label_set_text(detail_label, detail);
  lv_obj_set_style_text_font(detail_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(detail_label, lv_color_hex(0xb8c2bd), LV_PART_MAIN);
  lv_obj_set_width(detail_label, LV_PCT(88));
  lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(detail_label, 0, LV_PART_MAIN);
  lv_obj_remove_flag(detail_label, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 14, -9);
}

static void dolphin_home_art(lv_obj_t *parent)
{
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_t *body;
  lv_obj_t *fin;
  lv_obj_t *dot;
  lv_obj_t *title;
  lv_obj_t *caption;

  lv_obj_set_size(card, LV_PCT(94), 82);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1d292b), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x355052), LV_PART_MAIN);
  lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
  body = lv_obj_create(card);
  lv_obj_set_size(body, 54, 26);
  lv_obj_set_pos(body, 18, 29);
  lv_obj_set_style_radius(body, 24, LV_PART_MAIN);
  lv_obj_set_style_bg_color(body, lv_color_hex(0x4b7776), LV_PART_MAIN);
  lv_obj_set_style_border_width(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  fin = lv_obj_create(card);
  lv_obj_set_size(fin, 18, 12);
  lv_obj_set_pos(fin, 59, 23);
  lv_obj_set_style_radius(fin, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(fin, lv_color_hex(0x4b7776), LV_PART_MAIN);
  lv_obj_set_style_border_width(fin, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(fin, 0, LV_PART_MAIN);
  lv_obj_remove_flag(fin, LV_OBJ_FLAG_SCROLLABLE);
  dot = lv_obj_create(card);
  lv_obj_set_size(dot, 9, 9);
  lv_obj_set_pos(dot, 78, 37);
  lv_obj_set_style_radius(dot, 9, LV_PART_MAIN);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0xf08a24), LV_PART_MAIN);
  lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  title = lv_label_create(card);
  lv_label_set_text(title, "DOLPHIN DESK");
  lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(title, lv_color_hex(0xf2ead8), LV_PART_MAIN);
  lv_obj_set_pos(title, 112, 20);
  lv_obj_set_width(title, LV_PCT(52));
  lv_obj_set_style_pad_all(title, 0, LV_PART_MAIN);
  lv_obj_remove_flag(title, LV_OBJ_FLAG_SCROLLABLE);
  caption = lv_label_create(card);
  lv_label_set_text(caption, "Local status");
  lv_obj_set_style_text_font(caption, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_color(caption, lv_color_hex(0xb8c2bd), LV_PART_MAIN);
  lv_obj_set_pos(caption, 112, 43);
  lv_obj_set_width(caption, LV_PCT(52));
  lv_label_set_long_mode(caption, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_all(caption, 0, LV_PART_MAIN);
  lv_obj_remove_flag(caption, LV_OBJ_FLAG_SCROLLABLE);
}

static int dolphin_storage_path(char *path, size_t length, const char *relative)
{
  int result;

  if (relative[0] == '\0')
    {
      result = snprintf(path, length, "%s", CONFIG_DOLPHIN_STORAGE_ROOT);
    }
  else
    {
      result = snprintf(path, length, "%s/%s", CONFIG_DOLPHIN_STORAGE_ROOT,
                        relative);
    }

  return result < 0 || (size_t)result >= length ? -ENAMETOOLONG : OK;
}

/* Each component is checked before opendir().  This rejects a configured
 * root or an entered intermediate component that is a symlink; it is not a
 * substitute for an fd-based TOCTOU isolation boundary.
 */

static int dolphin_verify_storage_path(const char *relative)
{
  struct stat st;
  char current[DOLPHIN_PATH_SIZE];
  char next[DOLPHIN_PATH_SIZE];
  char components[DOLPHIN_PATH_SIZE];
  char *state;
  char *component;
  int result;

  result = snprintf(current, sizeof(current), "%s", CONFIG_DOLPHIN_STORAGE_ROOT);
  if (result < 0 || (size_t)result >= sizeof(current))
    {
      return -ENAMETOOLONG;
    }

  if (lstat(current, &st) < 0)
    {
      return -errno;
    }
  if (S_ISLNK(st.st_mode))
    {
      return -ELOOP;
    }
  if (!S_ISDIR(st.st_mode))
    {
      return -ENOTDIR;
    }

  snprintf(components, sizeof(components), "%s", relative);
  component = strtok_r(components, "/", &state);
  while (component != NULL)
    {
      result = snprintf(next, sizeof(next), "%s/%s", current, component);
      if (result < 0 || (size_t)result >= sizeof(next))
        {
          return -ENAMETOOLONG;
        }
      snprintf(current, sizeof(current), "%s", next);
      if (lstat(current, &st) < 0)
        {
          return -errno;
        }
      if (S_ISLNK(st.st_mode))
        {
          return -ELOOP;
        }
      if (!S_ISDIR(st.st_mode))
        {
          return -ENOTDIR;
        }
      component = strtok_r(NULL, "/", &state);
    }

  return OK;
}

static int dolphin_storage_directory(const char *relative)
{
  char path[DOLPHIN_PATH_SIZE];
  struct stat st;
  int ret;

  ret = dolphin_storage_path(path, sizeof(path), relative);
  if (ret < 0)
    {
      return ret;
    }

  if (mkdir(path, 0700) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  if (lstat(path, &st) < 0)
    {
      return -errno;
    }

  if (S_ISLNK(st.st_mode))
    {
      return -ELOOP;
    }

  return S_ISDIR(st.st_mode) ? OK : -ENOTDIR;
}

static void *dolphin_report_worker(void *arg)
{
  char text[DOLPHIN_REPORT_BYTES];
  char path[DOLPHIN_PATH_SIZE];
  unsigned int sequence;
  int fd = -1;
  int error;
  size_t length;

  (void)arg;
  pthread_mutex_lock(&g_report.lock);
  memcpy(text, g_report.job_text, sizeof(text));
  sequence = g_report.sequence;
  pthread_mutex_unlock(&g_report.lock);
  text[sizeof(text) - 1] = '\0';
  length = strlen(text);

#ifdef DOLPHIN_HOST_TEST
  while (g_report_test_hold)
    {
      usleep(1000);
    }
#endif

  error = dolphin_verify_storage_path("");
  if (error == OK)
    {
      error = dolphin_storage_directory("dolphin");
    }
  if (error == OK)
    {
      error = dolphin_storage_directory("dolphin/reports");
    }

  for (unsigned int attempt = 0; error == OK && attempt < DOLPHIN_REPORT_ATTEMPTS;
       attempt++, sequence++)
    {
      int result = snprintf(path, sizeof(path), "%s/dolphin/reports/scan-%08u.txt",
                            CONFIG_DOLPHIN_STORAGE_ROOT, sequence);
      if (result < 0 || (size_t)result >= sizeof(path))
        {
          error = -ENAMETOOLONG;
          break;
        }
      fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (fd >= 0)
        {
          break;
        }
      if (errno != EEXIST)
        {
          error = -errno;
          break;
        }
    }

  if (error == OK && fd < 0)
    {
      error = -EEXIST;
    }
  for (size_t written = 0; error == OK && written < length;)
    {
      ssize_t count = write(fd, text + written, length - written);
      if (count <= 0)
        {
          error = count < 0 ? -errno : -EIO;
        }
      else
        {
          written += (size_t)count;
        }
    }
  if (fd >= 0 && error == OK && fsync(fd) < 0)
    {
      error = -errno;
    }
  if (fd >= 0 && close(fd) < 0 && error == OK)
    {
      error = -errno;
    }

  pthread_mutex_lock(&g_report.lock);
  g_report.error = error;
  if (error == OK)
    {
      snprintf(g_report.path, sizeof(g_report.path), "%s", path);
      g_report.sequence = sequence + 1u;
    }
  g_report.running = false;
  g_report.complete = true;
  pthread_mutex_unlock(&g_report.lock);
  return NULL;
}

static void dolphin_report_save(lv_event_t *event)
{
  pthread_t thread;
  pthread_attr_t attr;
  int ret;

  (void)event;
  pthread_mutex_lock(&g_report.lock);
  if (g_report.running)
    {
      pthread_mutex_unlock(&g_report.lock);
      return;
    }
  g_report.error = 0;
  g_report.complete = false;
  g_report.running = true;
  memcpy(g_report.job_text, g_report.text, sizeof(g_report.job_text));
  g_report.page_generation = g_page_generation;
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, 16384);
      if (ret == 0)
        {
          ret = pthread_create(&thread, &attr, dolphin_report_worker, NULL);
        }
      pthread_attr_destroy(&attr);
    }
  if (ret == 0)
    {
      pthread_detach(thread);
    }
  else
    {
      g_report.error = -ret;
      g_report.running = false;
      g_report.complete = true;
    }
  pthread_mutex_unlock(&g_report.lock);
  if (ret == 0)
    {
      dolphin_label(g_page, "Saving report...", LV_PCT(94));
    }
}

static void dolphin_report_timer(lv_timer_t *timer)
{
  char path[DOLPHIN_PATH_SIZE];
  char line[DOLPHIN_PATH_SIZE + 48];
  uint32_t page_generation;
  int error;

  (void)timer;
  pthread_mutex_lock(&g_report.lock);
  if (!g_report.complete)
    {
      pthread_mutex_unlock(&g_report.lock);
      return;
    }
  g_report.complete = false;
  page_generation = g_report.page_generation;
  error = g_report.error;
  memcpy(path, g_report.path, sizeof(path));
  pthread_mutex_unlock(&g_report.lock);
  if (page_generation != g_page_generation)
    {
      return;
    }
  if (error == OK)
    {
      snprintf(line, sizeof(line), "Saved: %s", path);
    }
  else if (error == -ENOSPC)
    {
      snprintf(line, sizeof(line), "Save failed: storage full (%d)", error);
    }
  else if (error == -EROFS)
    {
      snprintf(line, sizeof(line), "Save failed: storage is read-only (%d)", error);
    }
  else if (error == -ENOENT || error == -ENODEV)
    {
      snprintf(line, sizeof(line), "Save failed: storage is not mounted (%d)", error);
    }
  else
    {
      snprintf(line, sizeof(line), "Save failed (%d)", error);
    }
  dolphin_label(g_page, line, LV_PCT(94));
}

static void *dolphin_scan_worker(void *arg)
{
  struct dolphin_scan_s *scan = &g_scan;
  struct dirent *entry;
  DIR *directory = NULL;
  char path[DOLPHIN_PATH_SIZE];
  char child[DOLPHIN_PATH_SIZE];
  char relative[DOLPHIN_PATH_SIZE];
  unsigned int count = 0;
  unsigned int enumerated = 0;
  int error = 0;
  int capacity_error = -ENOSYS;
  uint64_t capacity_total = 0;
  uint64_t capacity_free = 0;
  bool more = false;
  bool truncated = false;

  (void)arg;
  pthread_mutex_lock(&scan->lock);
  memcpy(relative, scan->relative, sizeof(relative));
  error = dolphin_storage_path(path, sizeof(path), relative);
  pthread_mutex_unlock(&scan->lock);
  if (error == 0)
    {
      error = dolphin_verify_storage_path(relative);
    }

  if (error == 0)
    {
      struct statfs filesystem = {0};
      if (statfs(path, &filesystem) < 0)
        {
          capacity_error = -errno;
          if (capacity_error == 0) capacity_error = -EIO;
        }
      else if (filesystem.f_bsize == 0)
        {
          capacity_error = -EIO;
        }
      else if ((uint64_t)filesystem.f_blocks > UINT64_MAX /
                 (uint64_t)filesystem.f_bsize ||
               (uint64_t)filesystem.f_bavail > UINT64_MAX /
                 (uint64_t)filesystem.f_bsize)
        {
          capacity_error = -EOVERFLOW;
        }
      else
        {
          capacity_total = (uint64_t)filesystem.f_blocks *
                           (uint64_t)filesystem.f_bsize;
          capacity_free = (uint64_t)filesystem.f_bavail *
                          (uint64_t)filesystem.f_bsize;
          capacity_error = 0;
      }
    }

  if (error != 0)
    capacity_error = error;

  if (error == 0 && (directory = opendir(path)) == NULL)
    {
      error = -errno;
    }

  while (error == 0)
    {
      struct stat st;
      size_t name_length;

      errno = 0;
      entry = readdir(directory);
      if (entry == NULL)
        {
          if (errno != 0)
            {
              error = -errno;
            }
          break;
        }

      name_length = strlen(entry->d_name);

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
          name_length == 0)
        {
          continue;
        }

      if (++enumerated > DOLPHIN_MAX_ENUMERATED)
        {
          truncated = true;
          break;
        }

      if (name_length >= DOLPHIN_NAME_SIZE ||
          strchr(entry->d_name, '/') != NULL)
        {
          continue;
        }

      if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
          (int)sizeof(child) || lstat(child, &st) < 0 || S_ISLNK(st.st_mode))
        {
          continue;
        }

      if (count == DOLPHIN_MAX_FILES)
        {
          more = true;
        }
      else
        {
          pthread_mutex_lock(&scan->lock);
          snprintf(scan->files[count].name, sizeof(scan->files[count].name), "%s",
                   entry->d_name);
          scan->files[count].size = st.st_size;
          scan->files[count].directory = S_ISDIR(st.st_mode);
          pthread_mutex_unlock(&scan->lock);
          count++;
        }
    }

  if (directory != NULL)
    {
      closedir(directory);
    }

  pthread_mutex_lock(&scan->lock);
  scan->count = count;
  scan->error = error;
  scan->more = more;
  scan->truncated = truncated;
  scan->capacity_total = capacity_total;
  scan->capacity_free = capacity_free;
  scan->capacity_error = capacity_error;
  scan->running = false;
  scan->complete = true;
  pthread_mutex_unlock(&scan->lock);
  syslog(LOG_INFO, "dolphin-ui: storage scan complete: result=%d entries=%u%s\n",
         error, count, truncated ? " truncated" : "");
  return NULL;
}

static bool dolphin_preview_text_name(const char *name)
{
  const char *suffix = strrchr(name, '.');

  return suffix != NULL &&
         (strcmp(suffix, ".txt") == 0 || strcmp(suffix, ".log") == 0 ||
          strcmp(suffix, ".md") == 0 || strcmp(suffix, ".csv") == 0 ||
          strcmp(suffix, ".json") == 0 || strcmp(suffix, ".ini") == 0 ||
          strcmp(suffix, ".cfg") == 0);
}

static int dolphin_preview_ascii(const char *text, size_t length)
{
  size_t index;

  for (index = 0; index < length; index++)
    {
      unsigned char value = (unsigned char)text[index];

      if (value < 0x20 && value != '\n' && value != '\r' && value != '\t')
        {
          return -EILSEQ;
        }
      if (value > 0x7e)
        {
          return -ENOTSUP;
        }
    }

  return OK;
}

static void *dolphin_preview_worker(void *arg)
{
  char relative[DOLPHIN_PATH_SIZE];
  char name[DOLPHIN_NAME_SIZE];
  char path[DOLPHIN_PATH_SIZE];
  char text[DOLPHIN_PREVIEW_BYTES + 1];
  struct stat st;
  unsigned int request;
  size_t length = 0;
  int fd = -1;
  int error = 0;
  bool truncated = false;

  (void)arg;
  pthread_mutex_lock(&g_preview.lock);
  memcpy(relative, g_preview.relative, sizeof(relative));
  memcpy(name, g_preview.name, sizeof(name));
  request = g_preview.request;
  pthread_mutex_unlock(&g_preview.lock);
  error = dolphin_verify_storage_path(relative);
  if (error == 0)
    {
      error = dolphin_storage_path(path, sizeof(path), relative);
    }
  if (error == 0)
    {
      size_t path_length = strlen(path);
      int appended = snprintf(path + path_length, sizeof(path) - path_length,
                              "/%s", name);

      if (appended < 0 || (size_t)appended >= sizeof(path) - path_length)
        {
          error = -ENAMETOOLONG;
        }
    }
  if (error == 0 && lstat(path, &st) < 0)
    {
      error = -errno;
    }
  else if (error == 0 && (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode)))
    {
      error = -EINVAL;
    }
  if (error == 0 && (fd = open(path, O_RDONLY)) < 0)
    {
      error = -errno;
    }
  while (error == 0 && length < DOLPHIN_PREVIEW_BYTES)
    {
      ssize_t read_count = read(fd, text + length, DOLPHIN_PREVIEW_BYTES - length);

      if (read_count < 0)
        {
          error = -errno;
        }
      else if (read_count == 0)
        {
          break;
        }
      else
        {
          length += (size_t)read_count;
        }
    }
  if (error == 0 && length == DOLPHIN_PREVIEW_BYTES)
    {
      char extra;
      ssize_t read_count = read(fd, &extra, 1);

      if (read_count < 0)
        {
          error = -errno;
        }
      else if (read_count > 0)
        {
          truncated = true;
        }
    }
  if (fd >= 0)
    {
      close(fd);
    }
  if (error == 0 && (error = dolphin_preview_ascii(text, length)) < 0)
    {
      length = 0;
    }
  text[length] = '\0';

  pthread_mutex_lock(&g_preview.lock);
  if (request == g_preview.request)
    {
      memcpy(g_preview.text, text, length + 1);
      g_preview.length = length;
      g_preview.error = error;
      g_preview.truncated = truncated;
      g_preview.completed_request = request;
      g_preview.running = false;
      g_preview.complete = true;
    }
  pthread_mutex_unlock(&g_preview.lock);
  return NULL;
}

static int dolphin_preview_start(const struct dolphin_file_s *file)
{
  pthread_t thread;
  pthread_attr_t attr;
  int result;

  if (!dolphin_preview_text_name(file->name))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_preview.lock);
  if (g_preview.running)
    {
      pthread_mutex_unlock(&g_preview.lock);
      return -EBUSY;
    }
  pthread_mutex_lock(&g_scan.lock);
  memcpy(g_preview.relative, g_scan.relative, sizeof(g_preview.relative));
  pthread_mutex_unlock(&g_scan.lock);
  snprintf(g_preview.name, sizeof(g_preview.name), "%s", file->name);
  g_preview.size = file->size;
  g_preview.length = 0;
  g_preview.error = 0;
  g_preview.truncated = false;
  g_preview.complete = false;
  g_preview.request++;
  g_preview.running = true;
  result = pthread_attr_init(&attr);
  if (result == 0)
    {
      result = pthread_attr_setstacksize(&attr, 16384);
      if (result == 0)
        {
          result = pthread_create(&thread, &attr, dolphin_preview_worker, NULL);
        }
      pthread_attr_destroy(&attr);
    }
  if (result == 0)
    {
      pthread_detach(thread);
    }
  else
    {
      g_preview.running = false;
      g_preview.error = -result;
      g_preview.completed_request = g_preview.request;
      g_preview.complete = true;
    }
  pthread_mutex_unlock(&g_preview.lock);
  return result == 0 ? OK : -result;
}

static void dolphin_file_detail(lv_event_t *event)
{
  const struct dolphin_file_s *file = lv_event_get_user_data(event);
  char text[96];
  int result;

  if (file->directory)
    {
      return;
    }

  dolphin_page("FILE DETAIL");
  snprintf(text, sizeof(text), "%s\nSize: %lld bytes", file->name,
           (long long)file->size);
  dolphin_label(g_page, text, LV_PCT(94));
  result = dolphin_preview_start(file);
  if (result == OK)
    {
      g_preview_page = g_page;
      dolphin_label(g_page, "Loading text preview...", LV_PCT(94));
    }
  else if (result == -EINVAL)
    {
      dolphin_label(g_page, "Preview available for text files only", LV_PCT(94));
    }
  else if (result == -EBUSY)
    {
      dolphin_label(g_page, "Preview worker is busy", LV_PCT(94));
    }
  else
    {
      snprintf(text, sizeof(text), "Preview unavailable: %d", -result);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_button(g_page, "BACK", dolphin_files, NULL);
}

static void dolphin_open_directory(lv_event_t *event)
{
  const struct dolphin_file_s *file = lv_event_get_user_data(event);
  char next[DOLPHIN_PATH_SIZE];
  int result;

  if (!file->directory)
    {
      dolphin_file_detail(event);
      return;
    }

  pthread_mutex_lock(&g_scan.lock);
  if (g_scan.relative[0] == '\0')
    {
      result = snprintf(next, sizeof(next), "%s", file->name);
    }
  else
    {
      result = snprintf(next, sizeof(next), "%s/%s", g_scan.relative,
                        file->name);
    }
  if (result >= 0 && (size_t)result < sizeof(next))
    {
      snprintf(g_scan.relative, sizeof(g_scan.relative), "%s", next);
    }
  pthread_mutex_unlock(&g_scan.lock);
  if (result < 0 || (size_t)result >= sizeof(next))
    {
      syslog(LOG_ERR, "dolphin-ui: file path too long\n");
    }
  dolphin_files(NULL);
}

static void dolphin_files_up(lv_event_t *event)
{
  char *separator;

  (void)event;
  pthread_mutex_lock(&g_scan.lock);
  if (!g_scan.running)
    {
      separator = strrchr(g_scan.relative, '/');
      if (separator != NULL)
        {
          *separator = '\0';
        }
      else
        {
          g_scan.relative[0] = '\0';
        }
    }
  pthread_mutex_unlock(&g_scan.lock);
  dolphin_files(NULL);
}

static void dolphin_files(lv_event_t *event)
{
  pthread_t thread;
  pthread_attr_t attr;
  char relative[DOLPHIN_PATH_SIZE];
  char location[DOLPHIN_PATH_SIZE + 16];
  int result;

  (void)event;
  dolphin_page("FILES");
  g_files_page = g_page;
  pthread_mutex_lock(&g_scan.lock);
  snprintf(relative, sizeof(relative), "%s", g_scan.relative);
  if (!g_scan.running)
    {
      g_scan.count = 0;
      g_scan.error = 0;
      g_scan.more = false;
      g_scan.truncated = false;
      g_scan.capacity_total = 0;
      g_scan.capacity_free = 0;
      g_scan.capacity_error = -EAGAIN;
      g_scan.complete = false;
      g_scan.running = true;
      result = pthread_attr_init(&attr);
      if (result == 0)
        {
          result = pthread_attr_setstacksize(&attr, 16384);
          if (result == 0)
            {
              result = pthread_create(&thread, &attr, dolphin_scan_worker,
                                      NULL);
            }

          pthread_attr_destroy(&attr);
        }
      if (result == 0)
        {
          pthread_detach(thread);
        }
      else
        {
          g_scan.running = false;
          g_scan.complete = true;
          g_scan.error = -result;
        }
    }
  pthread_mutex_unlock(&g_scan.lock);
  snprintf(location, sizeof(location), "Read-only: /%s", relative);
  dolphin_label(g_page, location, LV_PCT(94));
  if (relative[0] != '\0')
    {
      dolphin_button(g_page, "UP", dolphin_files_up, NULL);
    }
  dolphin_label(g_page, "Loading...", LV_PCT(94));
}

#ifdef CONFIG_BK7258_WIFI_VNET
static void dolphin_wireless_stop(lv_event_t *event)
{
  (void)event;
  if (g_wifi_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_wifi_ticket);
  if (g_channels_ticket != 0)
    (void)bk7258_wifi_diagnostic_cancel(g_channels_ticket);
}

static void dolphin_channels(lv_event_t *event)
{
  char text[96];
  struct bk7258_wifi_result_s link;
  int ret = 0;
  (void)event;
  dolphin_page("CHANNEL STATS");
  g_channels_page = g_page;
  if (g_channels_ticket != 0)
    {
      dolphin_label(g_page, "Previous sampling is still stopping; wait for completion",
                    LV_PCT(94));
      dolphin_button(g_page, "STOP", dolphin_wireless_stop, NULL);
      return;
    }
  memset(&link, 0, sizeof(link));
  if (bk7258_wifi_read_link(&link) == 0 &&
      link.link_state == BK7258_WIFI_LINK_CONNECTED)
    {
      dolphin_label(g_page,
                    "Sampling requires disconnecting Wi-Fi; reconnect manually afterwards",
                    LV_PCT(94));
      dolphin_button(g_page, "DISCONNECT & SAMPLE",
                     dolphin_channels_switch, NULL);
      dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
      return;
    }
  if (g_channels_ticket == 0)
    {
      ret = bk7258_wifi_channels_async(300, &g_channels_ticket);
      if (ret < 0) g_channels_ticket = 0;
    }
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Radio unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      dolphin_button(g_page, "RETRY", dolphin_channels, NULL);
    }
  else
    {
      dolphin_label(g_page, "Passive sampling: channels 1-13\n300 ms per channel",
                     LV_PCT(94));
      dolphin_button(g_page, "STOP", dolphin_wireless_stop, NULL);
  }
}

static void dolphin_channels_switch(lv_event_t *event)
{
  char text[96];
  int ret;

  (void)event;
  dolphin_page("CHANNEL STATS");
  g_channels_page = g_page;
  ret = bk7258_wifi_channels_switch_async(300, &g_channels_ticket);
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Disconnect/sample unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      dolphin_button(g_page, "BACK", dolphin_network, NULL);
      return;
    }
  dolphin_label(g_page,
                "Disconnecting Wi-Fi, then sampling channels...\nReconnect manually when complete",
                LV_PCT(94));
  dolphin_button(g_page, "STOP", dolphin_wireless_stop, NULL);
}

static void dolphin_channels_timer(lv_timer_t *timer)
{
  struct bk7258_wifi_channel_stats_s result;
  char text[112];
  int ret;
  (void)timer;
  if (g_channels_ticket == 0) return;
  memset(&result, 0, sizeof(result));
  ret = bk7258_wifi_channels_poll(g_channels_ticket, &result);
  if (ret == -EAGAIN) return;
  g_channels_ticket = 0;
  if (g_channels_page == NULL || g_channels_page != g_page) return;
  dolphin_page("CHANNEL STATS");
  if (ret == 0) ret = result.status;
  if (ret < 0)
    {
      struct bk7258_wifi_result_s link;
      memset(&link, 0, sizeof(link));
      if (ret == -EBUSY && bk7258_wifi_read_link(&link) == 0 &&
          link.link_state == BK7258_WIFI_LINK_CONNECTED)
        {
          dolphin_label(g_page,
                        "Sampling requires disconnecting Wi-Fi; reconnect manually afterwards",
                        LV_PCT(94));
          dolphin_button(g_page, "DISCONNECT & SAMPLE",
                         dolphin_channels_switch, NULL);
          dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
          return;
        }
      snprintf(text, sizeof(text), ret == -EBUSY ?
               "Radio is busy (%d)" : "Sampling stopped (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_label(g_page, "Observed frames, not channel utilization", LV_PCT(94));
  for (uint32_t i = 0; i < result.returned &&
                       i < BK7258_WIFI_CHANNEL_STATS_MAX; i++)
    {
      snprintf(text, sizeof(text), "CH %lu: %lu frames / %lu bytes",
               (unsigned long)result.channels[i].channel,
               (unsigned long)result.channels[i].frame_count,
               (unsigned long)result.channels[i].byte_count);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_button(g_page, "START AGAIN", dolphin_channels, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}

static void dolphin_gateway_check(lv_event_t *event)
{
  struct bk7258_wifi_result_s link;
  char text[112];
  int ret;

  (void)event;
  if (g_gateway_ticket != 0)
    {
      dolphin_label(g_page, "Gateway check is already running; wait or cancel",
                    LV_PCT(94));
      return;
    }
  memset(&link, 0, sizeof(link));
  ret = bk7258_wifi_read_link(&link);
  if (ret < 0 || link.link_state != BK7258_WIFI_LINK_CONNECTED ||
      link.router == 0)
    {
      dolphin_label(g_page, "Connect Wi-Fi before checking the gateway",
                    LV_PCT(94));
      return;
    }
  dolphin_page("GATEWAY CHECK");
  g_gateway_page = g_page;
  ret = bk7258_wifi_ping_async(3000, &g_gateway_ticket);
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Gateway check unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      g_gateway_ticket = 0;
      g_gateway_page = NULL;
      dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
      return;
    }
  dolphin_label(g_page,
                "Checking gateway response...\nCancellation waits for the bounded probe to close",
                LV_PCT(94));
  dolphin_button(g_page, "CANCEL", dolphin_network, NULL);
}

static void dolphin_gateway_timer(lv_timer_t *timer)
{
  struct bk7258_wifi_result_s result;
  struct in_addr address;
  char gateway[INET_ADDRSTRLEN] = "-";
  char text[192];
  int ret;

  (void)timer;
  if (g_gateway_ticket == 0) return;
  memset(&result, 0, sizeof(result));
  ret = bk7258_wifi_ping_poll(g_gateway_ticket, &result);
  if (ret == -EAGAIN) return;
  g_gateway_ticket = 0;
  syslog(LOG_INFO, "dolphin-gateway: status=%d\n",
         ret == 0 ? result.status : ret);
  if (g_gateway_page == NULL || g_gateway_page != g_page)
    {
      g_gateway_page = NULL;
      return;
    }
  g_gateway_page = NULL;
  if (ret == 0)
    ret = result.status;
  address.s_addr = result.router;
  if (result.router != 0)
    snprintf(gateway, sizeof(gateway), "%s", inet_ntoa(address));
  dolphin_page("GATEWAY CHECK");
  if (ret == 0)
    {
      snprintf(text, sizeof(text),
               "Gateway %s responded\nThis proves gateway response only; it does not prove Internet access",
               gateway);
    }
  else
    {
      snprintf(text, sizeof(text), "Gateway did not respond or check failed (%d)", ret);
    }
  dolphin_label(g_page, text, LV_PCT(94));
  pthread_mutex_lock(&g_report.lock);
  snprintf(g_report.text, sizeof(g_report.text),
           "Dolphin gateway check report\nGateway: %s\nStatus: %d\n"
           "A response proves gateway reachability only; Internet access was not tested.\n",
           gateway, ret);
  pthread_mutex_unlock(&g_report.lock);
  dolphin_button(g_page, "SAVE REPORT", dolphin_report_save, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}

static void dolphin_wifi_scan(lv_event_t *event)
{
  char text[80];
  int ret = 0;
  (void)event;
  dolphin_page("WI-FI SCAN");
  g_wifi_page = g_page;
  if (g_wifi_ticket == 0)
    {
      ret = bk7258_wifi_scan_async(BK7258_WIFI_SCAN_DEFAULT_MS,
                                   &g_wifi_ticket);
      if (ret < 0) g_wifi_ticket = 0;
    }
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Scan unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      dolphin_button(g_page, "RETRY", dolphin_wifi_scan, NULL);
    }
  else
    {
      dolphin_label(g_page, "Scanning... You can return HOME", LV_PCT(94));
      dolphin_button(g_page, "STOP", dolphin_wireless_stop, NULL);
    }
}

static void dolphin_wifi_results(lv_event_t *event)
{
  char text[112];
  char report[DOLPHIN_REPORT_BYTES];
  size_t report_used = 0;
  uint32_t omitted = 0;

  (void)event;
  if (!g_wifi_snapshot_valid)
    {
      dolphin_wifi_scan(NULL);
      return;
    }
  dolphin_page("WI-FI SCAN");
  g_wifi_page = g_page;
  snprintf(text, sizeof(text), "Found %lu; showing strongest %lu",
           (unsigned long)g_wifi_snapshot.found,
           (unsigned long)g_wifi_snapshot.returned);
  dolphin_label(g_page, text, LV_PCT(94));
  dolphin_report_append_line(report, sizeof(report), &report_used,
                             "Dolphin Wi-Fi scan report", 64);
  {
    char header[112];
    snprintf(header, sizeof(header),
             "Found %lu; showing %lu; truncated %lu",
             (unsigned long)g_wifi_snapshot.found,
             (unsigned long)g_wifi_snapshot.returned,
             (unsigned long)g_wifi_snapshot.truncated);
    dolphin_report_append_line(report, sizeof(report), &report_used,
                               header, 64);
  }
  for (uint32_t i = 0; i < g_wifi_snapshot.returned &&
                       i < BK7258_WIFI_AP_SCAN_MAX_RESULTS; i++)
    {
      char name[BK7258_WIFI_SSID_MAX_LEN + 1];
      memcpy(name, g_wifi_snapshot.aps[i].ssid, sizeof(name));
      name[sizeof(name) - 1] = '\0';
      for (size_t j = 0; name[j] != '\0'; j++)
        if ((unsigned char)name[j] < 32 || (unsigned char)name[j] > 126) name[j] = '?';
      snprintf(text, sizeof(text), "%s\nChannel %u | %ld dBm",
               name[0] ? name : "Hidden network", g_wifi_snapshot.aps[i].channel,
               (long)g_wifi_snapshot.aps[i].rssi);
      dolphin_button(g_page, text, dolphin_wifi_detail, (void *)(uintptr_t)i);
      {
        char line[160];
        snprintf(line, sizeof(line), "%s | Channel %u | %ld dBm",
                 name[0] ? name : "Hidden network",
                 g_wifi_snapshot.aps[i].channel,
                 (long)g_wifi_snapshot.aps[i].rssi);
        if (!dolphin_report_append_line(report, sizeof(report), &report_used,
                                        line, 64))
          omitted++;
      }
    }
  if (g_wifi_snapshot.truncated != 0)
    {
      snprintf(text, sizeof(text), "%lu additional networks truncated",
               (unsigned long)g_wifi_snapshot.truncated);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  if (omitted != 0)
    {
      char line[80];
      snprintf(line, sizeof(line), "Report truncated: %lu entries omitted",
               (unsigned long)omitted);
      dolphin_report_append_line(report, sizeof(report), &report_used, line, 0);
    }
  pthread_mutex_lock(&g_report.lock);
  snprintf(g_report.text, sizeof(g_report.text), "%s", report);
  pthread_mutex_unlock(&g_report.lock);
  dolphin_button(g_page, "SAVE REPORT", dolphin_report_save, NULL);
  dolphin_button(g_page, "SCAN AGAIN", dolphin_wifi_scan, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}

static void dolphin_wifi_detail(lv_event_t *event)
{
  uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
  const struct bk7258_wifi_scan_ap_s *ap;
  const char *security;
  char name[BK7258_WIFI_SSID_MAX_LEN + 1];
  char text[512];

  if (!g_wifi_snapshot_valid || index >= g_wifi_snapshot.returned ||
      index >= BK7258_WIFI_AP_SCAN_MAX_RESULTS)
    {
      return;
    }
  ap = &g_wifi_snapshot.aps[index];
  memcpy(g_wifi_connect_ssid, ap->ssid, sizeof(g_wifi_connect_ssid));
  g_wifi_connect_ssid[sizeof(g_wifi_connect_ssid) - 1] = '\0';
  g_wifi_connect_security = ap->security;
  security = bk7258_wifi_security_name(ap->security);
  memcpy(name, ap->ssid, sizeof(name));
  name[sizeof(name) - 1] = '\0';
  for (size_t i = 0; name[i] != '\0'; i++)
    if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126) name[i] = '?';

  dolphin_page("WI-FI DETAIL");
  snprintf(text, sizeof(text),
           "SSID: %s\nRSSI: %ld dBm\nChannel: %u\nSecurity: %s (%u)\n"
           "Scan: %lu found, %lu shown, %lu truncated\n"
           "BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
           name[0] ? name : "Hidden network", (long)ap->rssi, ap->channel,
           security != NULL ? security : "Unknown", ap->security,
           (unsigned long)g_wifi_snapshot.found,
           (unsigned long)g_wifi_snapshot.returned,
           (unsigned long)g_wifi_snapshot.truncated,
           ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4],
           ap->bssid[5]);
  dolphin_label(g_page, text, LV_PCT(94));
  pthread_mutex_lock(&g_report.lock);
  snprintf(g_report.text, sizeof(g_report.text), "Dolphin Wi-Fi scan report\n%s",
           text);
  pthread_mutex_unlock(&g_report.lock);
  dolphin_button(g_page, "SAVE REPORT", dolphin_report_save, NULL);
  dolphin_button(g_page, "CONNECT", dolphin_wifi_connect, NULL);
  dolphin_button(g_page, "BACK", dolphin_wifi_results, NULL);
}

static bool dolphin_wifi_security_supported(uint8_t security)
{
  switch (security)
    {
      case BK7258_WIFI_SECURITY_NONE:
      case BK7258_WIFI_SECURITY_WPA_TKIP:
      case BK7258_WIFI_SECURITY_WPA_AES:
      case BK7258_WIFI_SECURITY_WPA_MIXED:
      case BK7258_WIFI_SECURITY_WPA2_TKIP:
      case BK7258_WIFI_SECURITY_WPA2_AES:
      case BK7258_WIFI_SECURITY_WPA2_MIXED:
      case BK7258_WIFI_SECURITY_WPA3_SAE:
      case BK7258_WIFI_SECURITY_WPA3_WPA2_MIXED:
        return true;
      default:
        return false;
    }
}

static void dolphin_wifi_clear_password(bool destroy_text)
{
  if (g_wifi_connect_keyboard != NULL)
    {
      lv_obj_add_flag(g_wifi_connect_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
  if (g_wifi_connect_password != NULL)
    {
      if (destroy_text)
        {
          lv_textarea_set_text(g_wifi_connect_password, "");
        }
      lv_obj_add_state(g_wifi_connect_password, LV_STATE_DISABLED);
    }
  g_wifi_connect_password = NULL;
  g_wifi_connect_keyboard = NULL;
}

static void dolphin_wifi_connect_start(lv_event_t *event)
{
  const char *password;
  char password_copy[64];
  uint32_t ticket = 0;
  int ret;
  (void)event;

  if (g_wifi_connect_ticket != 0)
    {
      return;
    }
  password = g_wifi_connect_password != NULL ?
             lv_textarea_get_text(g_wifi_connect_password) : "";
  if (strlen(password) != 0 &&
      (strlen(password) < 8 || strlen(password) > 63))
    {
      dolphin_label(g_page, "Password must be empty or 8-63 characters",
                     LV_PCT(94));
      return;
    }
  if (g_wifi_connect_security != BK7258_WIFI_SECURITY_NONE &&
      password[0] == '\0')
    {
      dolphin_label(g_page, "Password required for secured network",
                     LV_PCT(94));
      return;
    }
  snprintf(password_copy, sizeof(password_copy), "%s", password);
  ret = bk7258_wifi_connect_async(g_wifi_connect_ssid, password_copy, 30000,
                                  &ticket);
  memset(password_copy, 0, sizeof(password_copy));
  if (ret < 0)
    {
      char text[80];
      snprintf(text, sizeof(text), "Connect unavailable (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
      return;
    }
  dolphin_wifi_clear_password(true);
  dolphin_page("WI-FI CONNECTING");
  {
    char status[160];
    snprintf(status, sizeof(status), "SSID: %s\nConnecting...\nTimeout: 30 seconds",
             g_wifi_connect_ssid);
    dolphin_label(g_page, status, LV_PCT(94));
  }
  g_wifi_connect_ticket = ticket;
  g_wifi_connect_generation = g_page_generation;
  g_wifi_connect_page = g_page;
  g_wifi_connect_status = NULL;
  dolphin_button(g_page, "CANCEL", dolphin_home, NULL);
}

static void dolphin_wifi_connect(lv_event_t *event)
{
  const char *security;
  lv_obj_t *label;
  (void)event;

  if (g_wifi_connect_ssid[0] == '\0')
    {
      dolphin_page("WI-FI CONNECT");
      dolphin_label(g_page, "Cannot connect: empty SSID", LV_PCT(94));
      dolphin_button(g_page, "BACK", dolphin_wifi_results, NULL);
      return;
    }
  if (!dolphin_wifi_security_supported(g_wifi_connect_security))
    {
      dolphin_page("WI-FI CONNECT");
      dolphin_label(g_page, "Cannot connect: unsupported security type",
                    LV_PCT(94));
      dolphin_button(g_page, "BACK", dolphin_wifi_results, NULL);
      return;
    }
  dolphin_page("WI-FI CONNECT");
  security = bk7258_wifi_security_name(g_wifi_connect_security);
  label = lv_label_create(g_page);
  lv_label_set_text_fmt(label, "SSID: %s\nSecurity: %s",
                        g_wifi_connect_ssid,
                        security != NULL ? security : "Supported");
  lv_obj_set_width(label, LV_PCT(94));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  if (g_wifi_connect_ticket != 0)
    {
      dolphin_label(g_page, "Previous connection is still stopping...",
                    LV_PCT(94));
      return;
    }
  g_wifi_connect_password = lv_textarea_create(g_page);
  lv_obj_set_width(g_wifi_connect_password, LV_PCT(94));
  lv_textarea_set_one_line(g_wifi_connect_password, true);
  lv_textarea_set_password_mode(g_wifi_connect_password, true);
  lv_textarea_set_max_length(g_wifi_connect_password, 63);
  lv_textarea_set_placeholder_text(g_wifi_connect_password,
                                   "Password (empty or 8-63 chars)");
  g_wifi_connect_keyboard = lv_keyboard_create(g_page);
  lv_obj_set_width(g_wifi_connect_keyboard, LV_PCT(94));
  lv_keyboard_set_textarea(g_wifi_connect_keyboard, g_wifi_connect_password);
  dolphin_button(g_page, "CONNECT", dolphin_wifi_connect_start, NULL);
  dolphin_button(g_page, "BACK", dolphin_wifi_results, NULL);
}

static void dolphin_wifi_timer(lv_timer_t *timer)
{
  struct bk7258_wifi_scan_snapshot_s result;
  char text[112];
  int ret;
  (void)timer;
  if (g_wifi_ticket == 0) return;
  memset(&result, 0, sizeof(result));
  ret = bk7258_wifi_scan_snapshot_poll(g_wifi_ticket, &result);
  if (ret == -EAGAIN) return;
  g_wifi_ticket = 0;
  /* Always consume completion, but never repaint a page the user left. */
  if (ret == 0 && result.status == OK)
    {
      g_wifi_snapshot = result;
      g_wifi_snapshot_valid = true;
    }
  if (g_wifi_page == NULL || g_wifi_page != g_page) return;
  dolphin_page("WI-FI SCAN");
  if (ret == 0) ret = result.status;
  if (ret < 0)
    {
      snprintf(text, sizeof(text), "Scan failed (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  else
    {
      dolphin_wifi_results(NULL);
      return;
    }
  dolphin_button(g_page, "SCAN AGAIN", dolphin_wifi_scan, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}

static void dolphin_wifi_connect_timer(lv_timer_t *timer)
{
  struct bk7258_wifi_result_s result;
  struct bk7258_wifi_result_s link;
  char ip[INET_ADDRSTRLEN];
  char mask[INET_ADDRSTRLEN];
  char router[INET_ADDRSTRLEN];
  char text[240];
  bool link_valid = false;
  int ret;
  (void)timer;
  if (g_wifi_connect_ticket == 0) return;
  memset(&result, 0, sizeof(result));
  ret = bk7258_wifi_connect_poll(g_wifi_connect_ticket, &result);
  if (ret == -EAGAIN) return;
  g_wifi_connect_ticket = 0;
  dolphin_wifi_clear_password(true);
  if (ret == 0) ret = result.status;
  if (ret == 0)
    {
      memset(&link, 0, sizeof(link));
      if (bk7258_wifi_read_link(&link) == 0)
        {
          result = link;
          link_valid = true;
        }
      struct in_addr address = { .s_addr = result.ipaddr };
      snprintf(ip, sizeof(ip), "%s", inet_ntoa(address));
      address.s_addr = result.netmask;
      snprintf(mask, sizeof(mask), "%s", inet_ntoa(address));
      address.s_addr = result.router;
      snprintf(router, sizeof(router), "%s", inet_ntoa(address));
      snprintf(text, sizeof(text), "Connected temporarily\nIP %s\nMask %s\n"
               "Gateway %s\nRSSI %ld dBm\nConnection is not saved across reboot",
               ip, mask, router, (long)result.rssi);
    }
  else
    {
      snprintf(text, sizeof(text), "Connect failed (%d)", ret);
    }
  if (g_wifi_connect_page == NULL || g_wifi_connect_page != g_page ||
      g_wifi_connect_generation != g_page_generation)
    {
      g_wifi_connect_page = NULL;
      g_wifi_connect_generation = 0;
      return;
    }
  g_wifi_connect_page = NULL;
  g_wifi_connect_generation = 0;
  dolphin_page("WI-FI CONNECT");
  dolphin_label(g_page, text, LV_PCT(94));
  if (ret == 0 && link_valid)
    {
      pthread_mutex_lock(&g_report.lock);
      snprintf(g_report.text, sizeof(g_report.text),
               "Dolphin Wi-Fi connection report\n"
               "Connection status: %ld\nLink state: %lu\n"
               "IP: %s\nMask: %s\nGateway: %s\nRSSI: %ld dBm\n"
               "Connection is temporary; not saved across reboot\n",
               (long)link.status, (unsigned long)link.link_state,
               ip, mask, router, (long)link.rssi);
      pthread_mutex_unlock(&g_report.lock);
      dolphin_button(g_page, "SAVE REPORT", dolphin_report_save, NULL);
    }
  if (ret != 0)
    {
      dolphin_button(g_page, "RETRY", dolphin_wifi_connect, NULL);
    }
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}
#endif

#ifdef DOLPHIN_HAS_BLE_SCAN
static void dolphin_ble_results(lv_event_t *event);
static void dolphin_ble_detail(lv_event_t *event);

static void dolphin_ble_summary(const struct bk7258_ble_scan_result_s *item,
                                char *text, size_t length)
{
  char name[32] = "Unnamed";
  char uuid[32] = "none";
  unsigned int manufacturer = 0;

  for (size_t offset = 0; offset < item->payload_length;)
    {
      size_t field = item->payload[offset];
      const uint8_t *data;

      if (field == 0 || field + 1 > item->payload_length - offset)
        {
          break;
        }
      data = &item->payload[offset + 1];
      if ((data[0] == 8 || data[0] == 9) && field > 1)
        {
          size_t count = field - 1;
          if (count >= sizeof(name)) count = sizeof(name) - 1;
          for (size_t i = 0; i < count; i++)
            name[i] = data[i + 1] >= 32 && data[i + 1] <= 126 ? data[i + 1] : '?';
          name[count] = '\0';
        }
      else if ((data[0] == 2 || data[0] == 3) && field >= 3)
        {
          snprintf(uuid, sizeof(uuid), "%02X%02X", data[2], data[1]);
        }
      else if (data[0] == 0xff)
        {
          manufacturer = (unsigned int)(field - 1);
        }
      offset += field + 1;
    }
  snprintf(text, length, "%s | %d dBm | UUID %s | mfg %u bytes", name,
           item->rssi, uuid, manufacturer);
}

static void dolphin_ble_results(lv_event_t *event)
{
  char text[128];
  char report[DOLPHIN_REPORT_BYTES];
  size_t report_used = 0;
  uint32_t omitted = 0;

  (void)event;
  if (!g_ble_snapshot_valid)
    {
      dolphin_ble(NULL);
      return;
    }
  dolphin_page("BLE BROADCASTS");
  dolphin_label(g_page, "Saved scan snapshot", LV_PCT(94));
  dolphin_report_append_line(report, sizeof(report), &report_used,
                             "Dolphin BLE scan report", 64);
  snprintf(text, sizeof(text), "Found %lu; showing %lu; truncated %lu",
           (unsigned long)g_ble_snapshot.count,
           (unsigned long)g_ble_snapshot.count < BK7258_BLE_SCAN_MAX_RESULTS ?
             (unsigned long)g_ble_snapshot.count :
             (unsigned long)BK7258_BLE_SCAN_MAX_RESULTS,
           (unsigned long)g_ble_snapshot.dropped);
  dolphin_report_append_line(report, sizeof(report), &report_used, text, 64);
  for (uint32_t i = 0; i < g_ble_snapshot.count &&
                       i < BK7258_BLE_SCAN_MAX_RESULTS; i++)
    {
      dolphin_ble_summary(&g_ble_snapshot.results[i], text, sizeof(text));
      dolphin_button(g_page, text, dolphin_ble_detail, (void *)(uintptr_t)i);
      if (!dolphin_report_append_line(report, sizeof(report), &report_used,
                                      text, 64))
        omitted++;
    }
  if (g_ble_snapshot.count == 0)
    dolphin_label(g_page, "No advertisements received", LV_PCT(94));
  if (omitted != 0)
    {
      char line[80];
      snprintf(line, sizeof(line), "Report truncated: %lu entries omitted",
               (unsigned long)omitted);
      dolphin_report_append_line(report, sizeof(report), &report_used, line, 0);
    }
  pthread_mutex_lock(&g_report.lock);
  snprintf(g_report.text, sizeof(g_report.text), "%s", report);
  pthread_mutex_unlock(&g_report.lock);
  dolphin_button(g_page, "SAVE REPORT", dolphin_report_save, NULL);
  dolphin_button(g_page, "START AGAIN", dolphin_ble, NULL);
  dolphin_button(g_page, "NETWORK", dolphin_network, NULL);
}

static void dolphin_ble_detail(lv_event_t *event)
{
  uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
  const struct bk7258_ble_scan_result_s *item;
  char summary[128];
  char text[512];

  if (!g_ble_snapshot_valid || index >= g_ble_snapshot.count ||
      index >= BK7258_BLE_SCAN_MAX_RESULTS)
    {
      return;
    }
  item = &g_ble_snapshot.results[index];
  dolphin_ble_summary(item, summary, sizeof(summary));
  dolphin_page("BLE DETAIL");
  snprintf(text, sizeof(text),
           "%s\nAddress: %02X:%02X:%02X:%02X:%02X:%02X\nType: %u\n"
           "Advertisement type: %u\nPayload summary only; raw payload is not saved.\n",
           summary, item->address[5], item->address[4], item->address[3],
           item->address[2], item->address[1], item->address[0], item->address_type,
           item->advertising_type);
  dolphin_label(g_page, text, LV_PCT(94));
  pthread_mutex_lock(&g_report.lock);
  snprintf(g_report.text, sizeof(g_report.text), "Dolphin BLE scan report\n%s",
           text);
  pthread_mutex_unlock(&g_report.lock);
  dolphin_button(g_page, "SAVE REPORT", dolphin_report_save, NULL);
  dolphin_button(g_page, "BACK", dolphin_ble_results, NULL);
}

static void dolphin_ble_stop(lv_event_t *event)
{
  (void)event;
  if (g_ble_owned)
    {
      int ret = bk7258_ble_scan_stop();
      g_ble_stop_sent = ret == 0;
    }
}

static void dolphin_ble(lv_event_t *event)
{
  int ret;
  (void)event;
  dolphin_page("BLE BROADCASTS");
  g_ble_page = g_page;
  dolphin_button(g_page, "STOP", dolphin_ble_stop, NULL);
  dolphin_button(g_page, "START AGAIN", dolphin_ble, NULL);
  g_ble_status = lv_label_create(g_page);
  lv_obj_set_width(g_ble_status, LV_PCT(94));
  lv_obj_set_style_text_color(g_ble_status, lv_color_hex(0xf2ead8), 0);
  g_ble_results = lv_obj_create(g_page);
  lv_obj_set_width(g_ble_results, LV_PCT(94));
  lv_obj_set_style_bg_opa(g_ble_results, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_ble_results, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_ble_results, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(g_ble_results, LV_FLEX_FLOW_COLUMN);
  ret = bk7258_ble_scan_start();
  if (ret == 0)
    {
      g_ble_owned = true;
      g_ble_stop_sent = false;
      g_ble_started = lv_tick_get();
      lv_label_set_text(g_ble_status, "Scanning for 10 seconds...");
    }
  else
    {
      char error[64];
      snprintf(error, sizeof(error), "Radio unavailable (%d); stop/retry", ret);
      lv_label_set_text(g_ble_status, error);
    }
}

static void dolphin_ble_timer(lv_timer_t *timer)
{
  struct bk7258_ble_scan_snapshot_s result;
  char status[96];
  (void)timer;
  if (!g_ble_owned) return;
  if (bk7258_ble_scan_poll(&result) < 0) return;
  g_ble_snapshot = result;
  g_ble_snapshot_valid = true;
  if (result.active && !g_ble_stop_sent &&
      lv_tick_elaps(g_ble_started) >= 10000)
    dolphin_ble_stop(NULL);
  if (!result.active) g_ble_owned = false;
  if (!result.active && g_ble_page != NULL && g_ble_page == g_page)
    {
      dolphin_ble_results(NULL);
      return;
    }
  if (g_ble_page == NULL || g_ble_page != g_page) return;
  snprintf(status, sizeof(status), "%s | %lu reports | error %ld",
           result.state == BK7258_BLE_SCAN_FAULTED ? "Stop failed: retry STOP" :
           result.active ? (g_ble_stop_sent ? "Stopping" : "Scanning") : "Stopped",
           (unsigned long)result.count, (long)result.last_error);
  lv_label_set_text(g_ble_status, status);
  lv_obj_clean(g_ble_results);
  for (uint32_t i = 0; i < result.count && i < BK7258_BLE_SCAN_MAX_RESULTS; i++)
    {
      const struct bk7258_ble_scan_result_s *item = &result.results[i];
      char text[128];
      dolphin_ble_summary(item, text, sizeof(text));
      dolphin_button(g_ble_results, text, dolphin_ble_detail, (void *)(uintptr_t)i);
    }
  if (result.count == 0)
    dolphin_label(g_ble_results, "No advertisements received", LV_PCT(94));
}
#endif

static void dolphin_network(lv_event_t *event)
{
  struct ifaddrs *addresses;
  struct ifaddrs *item;
  char line[112];
  bool found = false;

  (void)event;
  dolphin_page("NETWORK");
  if (getifaddrs(&addresses) != 0)
    {
      snprintf(line, sizeof(line), "Network info unavailable: %d", errno);
      dolphin_label(g_page, line, LV_PCT(94));
    }
  else
    {
      for (item = addresses; item != NULL; item = item->ifa_next)
        {
          char address[INET6_ADDRSTRLEN] = "-";

          if (item->ifa_name == NULL)
            {
              continue;
            }

          found = true;


          if (item->ifa_addr != NULL && item->ifa_addr->sa_family == AF_INET)
            {
              struct sockaddr_in *ipv4 = (struct sockaddr_in *)item->ifa_addr;
              (void)inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address));
            }
          else if (item->ifa_addr != NULL && item->ifa_addr->sa_family == AF_INET6)
            {
              struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)item->ifa_addr;
              (void)inet_ntop(AF_INET6, &ipv6->sin6_addr, address, sizeof(address));
            }

          snprintf(line, sizeof(line), "%s: %s\n%s", item->ifa_name,
                   (item->ifa_flags & IFF_UP) ? "Enabled" : "Disabled", address);
          dolphin_label(g_page, line, LV_PCT(94));
        }
      freeifaddrs(addresses);
      if (!found)
        {
          dolphin_label(g_page, "No network interfaces reported", LV_PCT(94));
        }
    }
  dolphin_button(g_page, "REFRESH", dolphin_network, NULL);
#ifdef CONFIG_BK7258_WIFI_VNET
  {
    struct bk7258_wifi_result_s link;
    memset(&link, 0, sizeof(link));
    if (bk7258_wifi_read_link(&link) == 0 &&
        link.link_state == BK7258_WIFI_LINK_CONNECTED && link.router != 0)
      dolphin_button(g_page, "CHECK GATEWAY", dolphin_gateway_check, NULL);
    else
      dolphin_label(g_page, "Connect Wi-Fi before checking the gateway",
                    LV_PCT(94));
  }
#endif
#ifdef DOLPHIN_HAS_BLE_SCAN
  dolphin_button(g_page, "BLE BROADCASTS", dolphin_ble, NULL);
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  dolphin_button(g_page, "SCAN WI-FI", dolphin_wifi_scan, NULL);
  dolphin_button(g_page, "CHANNEL STATS", dolphin_channels, NULL);
#endif
}

static void dolphin_device(lv_event_t *event)
{
  struct utsname system;
  char text[144];
  int32_t width = lv_display_get_horizontal_resolution(g_display);
  int32_t height = lv_display_get_vertical_resolution(g_display);

  (void)event;
  dolphin_page("DEVICE");
  if (uname(&system) == 0)
    {
      snprintf(text, sizeof(text), "%.40s %.40s\nDisplay: %ld x %ld", system.sysname,
               system.release, (long)width, (long)height);
    }
  else
    {
      snprintf(text, sizeof(text), "System info unavailable: %d\nDisplay: %ld x %ld",
               errno, (long)width, (long)height);
    }
  dolphin_label(g_page, text, LV_PCT(94));
}

#ifdef CONFIG_BK7258_USBHOST
static const char *dolphin_usbhost_enum_name(
  enum bk7258_usbhost_enumeration_state_e state)
{
  switch (state)
    {
      case BK7258_USBHOST_ENUMERATION_IDLE: return "idle";
      case BK7258_USBHOST_ENUMERATION_RUNNING: return "running";
      case BK7258_USBHOST_ENUMERATION_COMPLETE: return "complete";
      case BK7258_USBHOST_ENUMERATION_FAILED: return "failed";
      default: return "unknown";
    }
}

static void dolphin_usbhost(lv_event_t *event)
{
  struct bk7258_usbhost_snapshot_s snapshot;
  char text[256];
  int ret;

  (void)event;
  dolphin_page("USB HOST");
  memset(&snapshot, 0, sizeof(snapshot));
  ret = bk7258_usbhost_snapshot(&snapshot);
  if (ret == -EAGAIN)
    {
      dolphin_label(g_page, "USB status busy (-11); retry", LV_PCT(94));
    }
  else if (ret < 0)
    {
      snprintf(text, sizeof(text), "USB snapshot failed (%d)", ret);
      dolphin_label(g_page, text, LV_PCT(94));
    }
  else
    {
      unsigned int vid = 0;
      unsigned int pid = 0;
      unsigned int device_class = 0;
      unsigned int config_total = 0;
      bool config_valid = snapshot.configuration_descriptor_valid &&
                          snapshot.configuration_length >= 4;

      if (snapshot.device_descriptor_valid)
        {
          vid = snapshot.device_descriptor[8] |
                ((unsigned int)snapshot.device_descriptor[9] << 8);
          pid = snapshot.device_descriptor[10] |
                ((unsigned int)snapshot.device_descriptor[11] << 8);
          device_class = snapshot.device_descriptor[4];
        }
      if (config_valid)
        {
          config_total = snapshot.configuration_descriptor[2] |
                         ((unsigned int)snapshot.configuration_descriptor[3] << 8);
        }
      snprintf(text, sizeof(text),
               "Initialized: %s\nConnected: %s\nEnumeration: %s (%ld)\n"
               "VID:PID %04X:%04X\nDevice class: %02X\nSpeed: %u\n"
               "Config: %s, total %u, %s\n"
               "Names/classes are not inferred; no HID/MSC mount claimed",
               snapshot.initialized ? "yes" : "no",
               snapshot.connected ? "yes" : "no",
               dolphin_usbhost_enum_name(snapshot.enumeration_state),
               (long)snapshot.enumeration_result, vid, pid, device_class,
               snapshot.speed, config_valid ? "valid" : "unavailable",
               config_total, snapshot.configuration_truncated ? "truncated" : "complete");
      dolphin_label(g_page, text, LV_PCT(94));
    }
  dolphin_button(g_page, "REFRESH", dolphin_usbhost, NULL);
  dolphin_button(g_page, "HOME", dolphin_home, NULL);
}

#endif

#ifdef CONFIG_DOLPHIN_RECORDER
static void dolphin_recording_files(lv_event_t *event)
{
  (void)event;
  dolphin_files(NULL);
}

static void dolphin_recording_start(lv_event_t *event)
{
  char path[DOLPHIN_PATH_SIZE];
  struct dolphin_recording_snapshot_s snapshot;
  int fd = -1;
  int error;

  (void)event;
  g_recording_ui_error = 0;
  error = dolphin_recording_snapshot(&snapshot);
  if (error == OK && (snapshot.state == DOLPHIN_RECORDING_STARTING ||
                      snapshot.state == DOLPHIN_RECORDING_ACTIVE ||
                      snapshot.state == DOLPHIN_RECORDING_STOPPING))
    error = -EBUSY;
  if (error == OK)
    error = dolphin_verify_storage_path("");
  if (error == OK)
    error = dolphin_storage_directory("dolphin");
  if (error == OK)
    error = dolphin_storage_directory("dolphin/recordings");
  for (unsigned int attempt = 0; error == OK && attempt < DOLPHIN_REPORT_ATTEMPTS;
       attempt++)
    {
      unsigned int sequence = ++g_recording_sequence;
      int result = snprintf(path, sizeof(path),
                            "%s/dolphin/recordings/record-%08u.wav",
                            CONFIG_DOLPHIN_STORAGE_ROOT, sequence);

      if (result < 0 || (size_t)result >= sizeof(path))
        {
          error = -ENAMETOOLONG;
          break;
        }
      fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
      if (fd >= 0)
        break;
      if (errno != EEXIST)
        {
          error = -errno;
          break;
        }
    }

  if (error == OK && fd < 0)
    error = -EEXIST;
  if (error == OK)
    {
      error = dolphin_recording_begin(fd);
      if (error == OK)
        {
          snprintf(g_recording_path, sizeof(g_recording_path), "%s", path);
          fd = -1;
        }
    }
  if (fd >= 0)
    close(fd);
  if (g_recording_status != NULL && g_recording_page == g_page &&
      g_recording_generation == g_page_generation)
    {
      char text[80];

      if (error == OK)
        lv_label_set_text(g_recording_status, "Starting recorder...");
      else
        {
          g_recording_ui_error = error;
          snprintf(text, sizeof(text), "Recorder unavailable: %d", -error);
          lv_label_set_text(g_recording_status, text);
        }
    }
}

static void dolphin_recording_stop_ui(lv_event_t *event)
{
  int error;

  (void)event;
  error = dolphin_recording_stop();
  if (g_recording_status != NULL && g_recording_page == g_page &&
      g_recording_generation == g_page_generation)
    {
      lv_label_set_text(g_recording_status, error == OK ? "Stopping and saving..." :
                        "No active recording");
    }
}

static void dolphin_recording_page(lv_event_t *event)
{
  (void)event;
  dolphin_page("RECORDER");
  g_recording_page = g_page;
  g_recording_generation = g_page_generation;
  g_recording_ui_error = 0;
  dolphin_label(g_page, "16 kHz mono WAV. Starts only when you press START.",
                LV_PCT(94));
  g_recording_status = lv_label_create(g_page);
  lv_label_set_text(g_recording_status, "Ready to record to TF storage");
  lv_obj_set_width(g_recording_status, LV_PCT(94));
  lv_label_set_long_mode(g_recording_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(g_recording_status, lv_color_hex(0xf2ead8),
                              LV_PART_MAIN);
  dolphin_button(g_page, "START", dolphin_recording_start, NULL);
  dolphin_button(g_page, "STOP AND SAVE", dolphin_recording_stop_ui, NULL);
  dolphin_button(g_page, "FILES", dolphin_recording_files, NULL);
}

static void dolphin_recording_timer(lv_timer_t *timer)
{
  struct dolphin_recording_snapshot_s snapshot;
  char text[144];

  (void)timer;
  if (g_recording_page == NULL || g_recording_status == NULL ||
      g_recording_page != g_page || g_recording_generation != g_page_generation ||
      dolphin_recording_snapshot(&snapshot) < 0)
    return;

  if (g_recording_ui_error != 0)
    {
      snprintf(text, sizeof(text), "Recorder unavailable: %d", -g_recording_ui_error);
      lv_label_set_text(g_recording_status, text);
      return;
    }

  switch (snapshot.state)
    {
      case DOLPHIN_RECORDING_STARTING:
        snprintf(text, sizeof(text), "Starting recorder...");
        break;
      case DOLPHIN_RECORDING_ACTIVE:
      case DOLPHIN_RECORDING_STOPPING:
        snprintf(text, sizeof(text), "%s %lu ms  Session peak: %u (not SPL)  Clips: %lu",
                 snapshot.state == DOLPHIN_RECORDING_STOPPING ? "Saving:" : "Recording:",
                 (unsigned long)snapshot.milliseconds, (unsigned)snapshot.peak,
                 (unsigned long)snapshot.clipped);
        break;
      case DOLPHIN_RECORDING_SAVED:
        snprintf(text, sizeof(text), "Saved WAV: %s", g_recording_path);
        break;
      case DOLPHIN_RECORDING_CLEANUP_FAILED:
        snprintf(text, sizeof(text), "Recorder cleanup failed: %d", -snapshot.error);
        break;
      case DOLPHIN_RECORDING_FAILED:
        snprintf(text, sizeof(text), "Recording failed: %d", -snapshot.error);
        break;
      default:
        snprintf(text, sizeof(text), "Ready to record to TF storage");
        break;
    }
  lv_label_set_text(g_recording_status, text);
}
#endif

static void dolphin_home(lv_event_t *event)
{
  (void)event;
  pthread_mutex_lock(&g_scan.lock);
  if (!g_scan.running)
    {
      g_scan.relative[0] = '\0';
    }
  pthread_mutex_unlock(&g_scan.lock);
  dolphin_page("DOLPHIN");
  dolphin_home_art(g_page);
  dolphin_feature_card(g_page, "FILES", "Browse TF storage read-only",
                       dolphin_files);
#ifdef CONFIG_DOLPHIN_RECORDER
  dolphin_feature_card(g_page, "RECORDER", "Record a WAV file to TF storage",
                       dolphin_recording_page);
#endif
  dolphin_feature_card(g_page, "NETWORK", "Show live interface addresses",
                       dolphin_network);
#ifdef CONFIG_BK7258_USBHOST
  dolphin_feature_card(g_page, "USB HOST", "Show read-only USB device status",
                       dolphin_usbhost);
#endif
  dolphin_feature_card(g_page, "DEVICE", "System and display information",
                       dolphin_device);
}

#ifdef CONFIG_DOLPHIN_ADC_KEY
static void dolphin_adc_key_timer(lv_timer_t *timer)
{
  enum dolphin_adc_key_event_e event;
  lv_obj_t *focused;
  int error;

  (void)timer;
  while (dolphin_adc_key_poll(&event, &error) == OK)
    {
      if (event == DOLPHIN_ADC_KEY_NEXT)
        {
          lv_group_focus_next(g_adc_key_group);
        }
      else if (event == DOLPHIN_ADC_KEY_CONFIRM)
        {
          focused = lv_group_get_focused(g_adc_key_group);
          if (focused != NULL) lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
        }
      else if (event == DOLPHIN_ADC_KEY_HOME)
        {
          dolphin_home(NULL);
        }
      else
        {
          syslog(LOG_WARNING, "dolphin-ui: ADC key stopped: %d\n", error);
          if (g_page != NULL)
            dolphin_label(g_page, error == -EINVAL ?
                          "ADC key needs board raw calibration" :
                          "ADC key unavailable; touch remains active", LV_PCT(94));
        }
    }
}
#endif

static void dolphin_scan_timer(lv_timer_t *timer)
{
  unsigned int index;
  int error;
  bool more;
  bool truncated;
  uint64_t capacity_total;
  uint64_t capacity_free;
  int capacity_error;
  char line[80];
  char relative[DOLPHIN_PATH_SIZE];
  char location[DOLPHIN_PATH_SIZE + 16];

  (void)timer;
  pthread_mutex_lock(&g_scan.lock);
  if (!g_scan.complete)
    {
      pthread_mutex_unlock(&g_scan.lock);
      return;
    }

  error = g_scan.error;
  more = g_scan.more;
  truncated = g_scan.truncated;
  capacity_total = g_scan.capacity_total;
  capacity_free = g_scan.capacity_free;
  capacity_error = g_scan.capacity_error;
  snprintf(relative, sizeof(relative), "%s", g_scan.relative);
  g_scan.complete = false;
  pthread_mutex_unlock(&g_scan.lock);
  if (g_page == NULL || g_page != g_files_page)
    {
      return;
    }

  lv_obj_clean(g_page);
  snprintf(location, sizeof(location), "Read-only: /%s", relative);
  dolphin_label(g_page, location, LV_PCT(94));
  if (capacity_error == 0)
    {
      snprintf(line, sizeof(line), "File system: total %llu bytes; available %llu bytes",
               (unsigned long long)capacity_total,
               (unsigned long long)capacity_free);
    }
  else
    {
      snprintf(line, sizeof(line), "Storage capacity unavailable (%d)",
               capacity_error);
    }
  dolphin_label(g_page, line, LV_PCT(94));
  if (relative[0] != '\0')
    {
      dolphin_button(g_page, "UP", dolphin_files_up, NULL);
    }
  if (error < 0)
    {
      syslog(LOG_WARNING, "dolphin-ui: storage read failed: %d\n", error);
      snprintf(line, sizeof(line), "%s",
               error == -ENOENT ? "TF card is not ready" :
                                 "Cannot read this folder");
      dolphin_label(g_page, line, LV_PCT(94));
    }
  else
    {
      pthread_mutex_lock(&g_scan.lock);
      for (index = 0; index < g_scan.count; index++)
        {
          snprintf(line, sizeof(line), "%s%s", g_scan.files[index].name,
                   g_scan.files[index].directory ? "/" : "");
          dolphin_button(g_page, line, dolphin_open_directory, &g_scan.files[index]);
        }
      pthread_mutex_unlock(&g_scan.lock);
      if (index == 0)
        {
          dolphin_label(g_page, "No entries", LV_PCT(94));
        }
      if (more)
        {
          dolphin_label(g_page, "Showing first 64 entries", LV_PCT(94));
        }
      if (truncated)
        {
          dolphin_label(g_page, "Directory truncated after 256 entries",
                        LV_PCT(94));
        }
    }
  dolphin_button(g_page, error < 0 ? "RETRY" : "REFRESH", dolphin_files, NULL);
}

static void dolphin_preview_timer(lv_timer_t *timer)
{
  char name[DOLPHIN_NAME_SIZE];
  char text[DOLPHIN_PREVIEW_BYTES + 1];
  off_t size;
  size_t length;
  unsigned int request;
  unsigned int current_request;
  int error;
  bool truncated;
  char line[96];

  (void)timer;
  pthread_mutex_lock(&g_preview.lock);
  if (!g_preview.complete)
    {
      pthread_mutex_unlock(&g_preview.lock);
      return;
    }

  request = g_preview.completed_request;
  current_request = g_preview.request;
  memcpy(name, g_preview.name, sizeof(name));
  memcpy(text, g_preview.text, sizeof(text));
  size = g_preview.size;
  length = g_preview.length;
  error = g_preview.error;
  truncated = g_preview.truncated;
  g_preview.complete = false;
  pthread_mutex_unlock(&g_preview.lock);
  if (g_page == NULL || g_page != g_preview_page || request != current_request)
    {
      return;
    }

  lv_obj_clean(g_page);
  snprintf(line, sizeof(line), "%s\nSize: %lld bytes", name, (long long)size);
  dolphin_label(g_page, line, LV_PCT(94));
  if (error == -EILSEQ)
    {
      dolphin_label(g_page, "Preview unavailable: non-text data", LV_PCT(94));
    }
  else if (error == -ENOTSUP)
    {
      dolphin_label(g_page, "Preview unavailable: ASCII text only", LV_PCT(94));
    }
  else if (error < 0)
    {
      snprintf(line, sizeof(line), "Preview unavailable: %d", -error);
      dolphin_label(g_page, line, LV_PCT(94));
    }
  else if (length == 0)
    {
      dolphin_label(g_page, "Empty text file", LV_PCT(94));
    }
  else
    {
      dolphin_label(g_page, text, LV_PCT(94));
      lv_obj_set_style_text_align(lv_obj_get_child(g_page, -1),
                                  LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
      if (truncated)
        {
          dolphin_label(g_page, "Preview truncated at 2048 bytes", LV_PCT(94));
        }
    }
  dolphin_button(g_page, "BACK", dolphin_files, NULL);
}

static int dolphin_ui_task(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t descriptor;
  lv_nuttx_result_t result = {0};
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  lv_display_t *accelerated;
#endif

  (void)argc;
  (void)argv;
  if (lv_is_initialized())
    {
      syslog(LOG_ERR, "dolphin-ui: LVGL already has an owner\n");
      g_ui_started = false;
      return -EBUSY;
    }

  lv_init();
  lv_nuttx_dsc_init(&descriptor);
  descriptor.fb_path = "/dev/fb0";
  descriptor.input_path = "/dev/input0";
#ifdef CONFIG_BK7258_GT1151
  descriptor.input_path = NULL;
#endif
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  accelerated = bk7258_lvgl_fb_create(descriptor.fb_path);
  if (accelerated != NULL)
    {
      descriptor.fb_path = NULL;
    }
#endif
  lv_nuttx_init(&descriptor, &result);
#ifdef CONFIG_BK7258_GT1151
  result.indev = dolphin_touch_create("/dev/input0");
#endif
#ifdef CONFIG_BK7258_LVGL_FB_ACCEL
  if (accelerated != NULL)
    {
      result.disp = accelerated;
      if (result.indev != NULL &&
          bk7258_lvgl_fb_bind_touch(accelerated, result.indev) < 0)
        {
          lv_display_delete(accelerated);
          result.disp = NULL;
        }
    }
  syslog(LOG_INFO, "dolphin-ui: render=%s\n",
         accelerated != NULL ? "sram-dma2d" : "psram-direct-fallback");
#endif
  if (result.disp == NULL || result.indev == NULL)
    {
      syslog(LOG_ERR, "dolphin-ui: LVGL display or touch initialization failed\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      g_ui_started = false;
      return -ENODEV;
    }

  g_display = result.disp;
#ifdef CONFIG_DOLPHIN_ADC_KEY
  g_adc_key_group = lv_group_create();
  if (g_adc_key_group != NULL)
    {
      (void)dolphin_adc_key_start();
    }
#endif
  dolphin_home(NULL);
  syslog(LOG_INFO, "dolphin-ui: display and touch initialized\n");
  lv_timer_create(dolphin_report_timer, 100, NULL);
  lv_timer_create(dolphin_scan_timer, 100, NULL);
  lv_timer_create(dolphin_preview_timer, 100, NULL);
#ifdef CONFIG_DOLPHIN_ADC_KEY
  lv_timer_create(dolphin_adc_key_timer, 50, NULL);
#endif
#ifdef CONFIG_DOLPHIN_RECORDER
  lv_timer_create(dolphin_recording_timer, 100, NULL);
#endif
#ifdef DOLPHIN_HAS_BLE_SCAN
  lv_timer_create(dolphin_ble_timer, 300, NULL);
#endif
#ifdef CONFIG_BK7258_WIFI_VNET
  lv_timer_create(dolphin_wifi_timer, 100, NULL);
  lv_timer_create(dolphin_wifi_connect_timer, 100, NULL);
  lv_timer_create(dolphin_channels_timer, 100, NULL);
  lv_timer_create(dolphin_gateway_timer, 100, NULL);
#endif
  for (;;)
    {
      uint32_t delay = lv_timer_handler();
      usleep((delay > 20 ? 20 : (delay == 0 ? 1 : delay)) * 1000);
    }
}

int dolphin_ui_start(void)
{
  pid_t pid;

  if (g_ui_started)
    {
      return -EALREADY;
    }

  g_ui_started = true;
  pid = task_create("dolphin-ui", DOLPHIN_UI_PRIORITY, DOLPHIN_UI_STACKSIZE,
                    dolphin_ui_task, NULL);
  if (pid < 0)
    {
      g_ui_started = false;
      syslog(LOG_ERR, "dolphin-ui: task creation failed: %d\n", (int)pid);
      return (int)pid;
    }

  syslog(LOG_INFO, "dolphin-ui: task started\n");
  return OK;
}
