/* SPDX-License-Identifier: Apache-2.0 */
/* Real LVGL software rendering of the production UI. NuttX device/task
 * adapters are not exercised here; they must pass target acceptance.
 */
#include <assert.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/vfs.h>
#include <lvgl/lvgl.h>

static char storage_root[192];
#define CONFIG_DOLPHIN_STORAGE_ROOT storage_root
#define FAR
#define OK 0
typedef struct { const char *fb_path; const char *input_path; } lv_nuttx_dsc_t;
typedef struct { lv_display_t *disp; lv_indev_t *indev; } lv_nuttx_result_t;
static void lv_nuttx_dsc_init(lv_nuttx_dsc_t *d) { memset(d, 0, sizeof(*d)); }
static void lv_nuttx_init(lv_nuttx_dsc_t *d, lv_nuttx_result_t *r)
{ (void)d; (void)r; assert(!"host test must not initialize NuttX devices"); }
static void lv_nuttx_deinit(lv_nuttx_result_t *r) { (void)r; }
static pid_t task_create(const char *n, int p, int s,
                         int (*entry)(int, char **), char *const argv[])
{ (void)n; (void)p; (void)s; (void)entry; (void)argv; return -ENOSYS; }

#define CONFIG_BK7258_WIFI_VNET 1
#define CONFIG_BK7258_AP_CORE 1
#define CONFIG_BK7258_USBHOST 1
#define CONFIG_DOLPHIN_RECORDER 1
#include <arch/chip/bk7258_wifi.h>
#include <arch/chip/bk7258_usbhost.h>
static int wifi_submit_error, wifi_poll_error = -EAGAIN;
static struct bk7258_wifi_scan_snapshot_s wifi_result;
static int wifi_connect_submit_error, wifi_connect_poll_error = -EAGAIN;
static struct bk7258_wifi_result_s wifi_connect_result;
static int wifi_connect_cancels;
static char wifi_connect_ssid[64];
static char wifi_connect_password[64];
static struct bk7258_usbhost_snapshot_s usbhost_snapshot;
static int usbhost_snapshot_error;
int bk7258_wifi_scan_async(uint32_t timeout, uint32_t *ticket)
{ assert(timeout == BK7258_WIFI_SCAN_DEFAULT_MS);
  if (wifi_submit_error) return wifi_submit_error;
  *ticket = 1; return 0; }
int bk7258_wifi_scan_snapshot_poll(
  uint32_t ticket, struct bk7258_wifi_scan_snapshot_s *result)
{ assert(ticket == 1); *result = wifi_result; return wifi_poll_error; }
const char *bk7258_wifi_security_name(uint8_t security)
{
  if (security == BK7258_WIFI_SECURITY_NONE) return "Open";
  if (security == BK7258_WIFI_SECURITY_WPA2_AES) return "WPA2-AES";
  return NULL;
}
int bk7258_wifi_connect_async(const char *ssid, const char *password,
                              uint32_t timeout, uint32_t *ticket)
{ assert(timeout == 30000); assert(ssid && password);
  if (wifi_connect_submit_error) return wifi_connect_submit_error;
  snprintf(wifi_connect_ssid, sizeof(wifi_connect_ssid), "%s", ssid);
  snprintf(wifi_connect_password, sizeof(wifi_connect_password), "%s", password);
  *ticket = 3; return 0; }
int bk7258_wifi_connect_poll(uint32_t ticket,
                             struct bk7258_wifi_result_s *result)
{ assert(ticket == 3); *result = wifi_connect_result; return wifi_connect_poll_error; }
int bk7258_wifi_connect_cancel(uint32_t ticket)
{ assert(ticket == 3); wifi_connect_cancels++; return 0; }
int bk7258_wifi_read_link(struct bk7258_wifi_result_s *result)
{ *result = wifi_connect_result; return 0; }
int bk7258_wifi_ping_async(uint32_t timeout, uint32_t *ticket)
{ assert(timeout == 3000); *ticket = 5; return 0; }
int bk7258_wifi_ping_poll(uint32_t ticket,
                          struct bk7258_wifi_result_s *result)
{ assert(ticket == 5); *result = wifi_connect_result; return wifi_connect_poll_error; }
int bk7258_wifi_ping_cancel(uint32_t ticket)
{ assert(ticket == 5); wifi_connect_cancels++; return 0; }
int bk7258_usbhost_snapshot(struct bk7258_usbhost_snapshot_s *snapshot)
{ if (usbhost_snapshot_error) return usbhost_snapshot_error;
  *snapshot = usbhost_snapshot; return 0; }

static int diagnostic_cancels;
int bk7258_wifi_diagnostic_cancel(uint32_t ticket)
{ assert(ticket); diagnostic_cancels++; return 0; }
int bk7258_wifi_channels_async(uint32_t dwell, uint32_t *ticket)
{ assert(dwell == 300); *ticket = 2; return 0; }
int bk7258_wifi_channels_switch_async(uint32_t dwell, uint32_t *ticket)
{ assert(dwell == 300); *ticket = 4; return 0; }
int bk7258_wifi_channels_poll(uint32_t ticket,
                              struct bk7258_wifi_channel_stats_s *result)
{ assert(ticket == 2 || ticket == 4); memset(result, 0, sizeof(*result));
  result->returned = 1; result->channels[0].channel = 1;
  result->channels[0].frame_count = 12; result->channels[0].byte_count = 900;
  return 0; }

#define CONFIG_BK7258_BT_IPC 1
#define CONFIG_WIRELESS_BLUETOOTH_HOST 1
#include <arch/chip/bk7258_ble_scan.h>
static struct bk7258_ble_scan_snapshot_s ble_snapshot;
int bk7258_ble_scan_start(void)
{ if (ble_snapshot.active) return -EBUSY;
  memset(&ble_snapshot, 0, sizeof(ble_snapshot));
  ble_snapshot.active = 1;ble_snapshot.state = BK7258_BLE_SCAN_ACTIVE;return 0; }
int bk7258_ble_scan_stop(void)
{ ble_snapshot.active = 0;ble_snapshot.state = BK7258_BLE_SCAN_IDLE;return 0; }
int bk7258_ble_scan_poll(struct bk7258_ble_scan_snapshot_s *s)
{*s = ble_snapshot;return 0;}

static int test_statfs_error;
static unsigned long long test_statfs_blocks = 3;
static unsigned long long test_statfs_bavail = 2;
static unsigned long long test_statfs_bsize = 4096;
static int dolphin_test_statfs(const char *path, struct statfs *result)
{
  (void)path;
  if (test_statfs_error != 0) return test_statfs_error;
  memset(result, 0, sizeof(*result));
  result->f_blocks = test_statfs_blocks;
  result->f_bavail = test_statfs_bavail;
  result->f_bsize = test_statfs_bsize;
  return 0;
}
#define statfs(path, result) dolphin_test_statfs(path, result)

#include "../../../app/dolphin/dolphin_ui.c"

static struct dolphin_recording_snapshot_s recording_snapshot =
{
  .state = DOLPHIN_RECORDING_IDLE
};

int dolphin_recording_begin(int fd)
{
  (void)fd;
  return 0;
}

int dolphin_recording_stop(void)
{
  return 0;
}

int dolphin_recording_snapshot(struct dolphin_recording_snapshot_s *snapshot)
{
  *snapshot = recording_snapshot;
  return 0;
}

static uint32_t pixels[480 * 480];
static uint32_t frame[480 * 480];
static int width;
static int height;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  uint32_t *src = (uint32_t *)data;
  for (int y = area->y1; y <= area->y2; y++)
    for (int x = area->x1; x <= area->x2; x++)
      frame[y * width + x] = *src++;
  lv_display_flush_ready(display);
}

static void screenshot(const char *path)
{
  FILE *f;
  lv_obj_update_layout(lv_screen_active());
  lv_refr_now(g_display);
  f = fopen(path, "wb");
  assert(f);
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int i = 0; i < width * height; i++)
    {
      uint8_t rgb[] = { frame[i] >> 16, frame[i] >> 8, frame[i] };
      assert(fwrite(rgb, 1, 3, f) == 3);
    }
  fclose(f);
}

static lv_obj_t *find_text(lv_obj_t *obj, const char *text)
{
  if (lv_obj_check_type(obj, &lv_label_class) &&
      strcmp(lv_label_get_text(obj), text) == 0) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    {
      lv_obj_t *found = find_text(lv_obj_get_child(obj, i), text);
      if (found) return found;
    }
  return NULL;
}

static bool contains_text(lv_obj_t *obj, const char *text)
{
  if (lv_obj_check_type(obj, &lv_label_class) &&
      strstr(lv_label_get_text(obj), text) != NULL) return true;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    if (contains_text(lv_obj_get_child(obj, i), text)) return true;
  return false;
}

static void assert_label_widths(lv_obj_t *obj)
{
  if (lv_obj_check_type(obj, &lv_label_class))
    {
      lv_area_t area;
      lv_obj_get_coords(obj, &area);
      assert(area.x1 >= 0 && area.x2 < width);
    }
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
    assert_label_widths(lv_obj_get_child(obj, i));
}

static void click(const char *text)
{
  lv_obj_t *label = find_text(lv_screen_active(), text);
  assert(label);
  assert(lv_obj_check_type(lv_obj_get_parent(label), &lv_button_class));
  lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, NULL);
}

static void await_scan(void)
{
  for (int i = 0; i < 2000; i++)
    {
      bool busy;
      pthread_mutex_lock(&g_scan.lock);
      busy = g_scan.running;
      pthread_mutex_unlock(&g_scan.lock);
      if (!busy) { dolphin_scan_timer(NULL); return; }
      usleep(1000);
    }
  assert(!"file scan did not finish");
}

static void await_preview(void)
{
  for (int i = 0; i < 2000; i++)
    {
      bool busy;
      pthread_mutex_lock(&g_preview.lock);
      busy = g_preview.running;
      pthread_mutex_unlock(&g_preview.lock);
      if (!busy) { dolphin_preview_timer(NULL); return; }
      usleep(1000);
    }
  assert(!"file preview did not finish");
}

static void await_report(void)
{
  for (int i = 0; i < 2000; i++)
    {
      bool busy;
      pthread_mutex_lock(&g_report.lock);
      busy = g_report.running;
      pthread_mutex_unlock(&g_report.lock);
      if (!busy) { dolphin_report_timer(NULL); return; }
      usleep(1000);
    }
  assert(!"report save did not finish");
}

int main(int argc, char **argv)
{
  char base[] = "/tmp/dolphin-ui-XXXXXX";
  char path[256];
  char report_path[256];
  char report_name[64];
  char report_text[768];
  size_t report_length;
  char root[sizeof(storage_root)];
  char shot[512];
  FILE *file;
  assert(argc == 2);
  assert(mkdtemp(base));
  snprintf(storage_root, sizeof(storage_root), "%s/card", base);
  snprintf(root, sizeof(root), "%s", storage_root);
  lv_init();
  width = 320;
  height = 480;
  g_display = lv_display_create(width, height);
  lv_display_set_buffers(g_display, pixels, NULL, width * height * 4,
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(g_display, flush);
  {
    struct
    {
      char buffer[8];
      unsigned char guard;
    } exact = {{0}, 0xa5};
    size_t used = 0;
    assert(dolphin_report_append_line(exact.buffer, sizeof(exact.buffer),
                                      &used, "123456", 0));
    assert(used == sizeof(exact.buffer) - 1 && exact.guard == 0xa5);
    used = 0;
    assert(!dolphin_report_append_line(exact.buffer, sizeof(exact.buffer),
                                       &used, "1234567", 0));
    assert(used == 0 && exact.guard == 0xa5);
  }
  dolphin_home(NULL);
  snprintf(shot, sizeof(shot), "%s/home-portrait.ppm", argv[1]);
  screenshot(shot);
  assert_label_widths(lv_screen_active());
  memset(&usbhost_snapshot, 0, sizeof(usbhost_snapshot));
  usbhost_snapshot.initialized = true;
  usbhost_snapshot.connected = true;
  usbhost_snapshot.speed = 2;
  usbhost_snapshot.enumeration_state = BK7258_USBHOST_ENUMERATION_COMPLETE;
  usbhost_snapshot.device_descriptor_valid = true;
  usbhost_snapshot.device_descriptor[4] = 3;
  usbhost_snapshot.device_descriptor[8] = 0x34;
  usbhost_snapshot.device_descriptor[9] = 0x12;
  usbhost_snapshot.device_descriptor[10] = 0x78;
  usbhost_snapshot.device_descriptor[11] = 0x56;
  usbhost_snapshot.configuration_descriptor_valid = true;
  usbhost_snapshot.configuration_length = 9;
  usbhost_snapshot.configuration_descriptor[2] = 9;
  click("USB HOST");
  dolphin_usbhost(NULL);
  assert(contains_text(lv_screen_active(), "Initialized: yes"));
  assert(contains_text(lv_screen_active(), "VID:PID 1234:5678"));
  assert(contains_text(lv_screen_active(), "Device class: 03"));
  assert(contains_text(lv_screen_active(), "Config: valid, total 9, complete"));
  usbhost_snapshot_error = -EAGAIN;
  click("REFRESH");
  assert(find_text(lv_screen_active(), "USB status busy (-11); retry"));
  usbhost_snapshot_error = 0;
  memset(&usbhost_snapshot, 0, sizeof(usbhost_snapshot));
  usbhost_snapshot.enumeration_state = BK7258_USBHOST_ENUMERATION_IDLE;
  click("REFRESH");
  assert(contains_text(lv_screen_active(), "Initialized: no"));
  assert(contains_text(lv_screen_active(), "Connected: no"));
  usbhost_snapshot.enumeration_state = BK7258_USBHOST_ENUMERATION_FAILED;
  usbhost_snapshot.enumeration_result = -EIO;
  click("REFRESH");
  assert(contains_text(lv_screen_active(), "Enumeration: failed (-5)"));
  click("HOME");
  usbhost_snapshot.initialized = true;
  usbhost_snapshot.connected = true;
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  click("RECORDER"); click("START");
  assert(contains_text(lv_screen_active(), "Recorder unavailable: 2"));
  dolphin_recording_timer(NULL);
  assert(contains_text(lv_screen_active(), "Recorder unavailable: 2"));
  click("HOME");
  click("FILES");
  await_scan();
  assert(g_scan.error == -ENOENT);
  assert(find_text(lv_screen_active(), "RETRY"));
  snprintf(shot, sizeof(shot), "%s/storage-missing.ppm", argv[1]);
  screenshot(shot);
  assert(mkdir(storage_root, 0700) == 0);
  snprintf(path, sizeof(path), "%s/docs", storage_root);
  assert(mkdir(path, 0700) == 0);
  snprintf(path, sizeof(path), "%s/readme.txt", storage_root);
  file = fopen(path, "w"); assert(file); fputs("fixture", file); fclose(file);
  snprintf(path, sizeof(path), "%s/link", storage_root);
  assert(symlink("/", path) == 0);
  click("RETRY"); await_scan();
  assert(g_scan.error == 0 && g_scan.count == 2);
  assert(contains_text(lv_screen_active(),
                       "File system: total 12288 bytes; available 8192 bytes"));
  test_statfs_error = -EIO;
  click("REFRESH"); await_scan();
  assert(g_scan.error == 0);
  assert(contains_text(lv_screen_active(), "Storage capacity unavailable (-5)"));
  test_statfs_error = 0;
  test_statfs_blocks = ULLONG_MAX;
  test_statfs_bsize = 2;
  click("REFRESH"); await_scan();
  assert(contains_text(lv_screen_active(), "Storage capacity unavailable (-75)"));
  test_statfs_blocks = 3;
  test_statfs_bsize = 4096;
  click("REFRESH"); await_scan();
  assert(g_scan.error == 0 && g_scan.count == 2);
  assert(!find_text(lv_screen_active(), "link/"));
  snprintf(shot, sizeof(shot), "%s/files.ppm", argv[1]); screenshot(shot);
  click("docs/"); await_scan(); assert(strcmp(g_scan.relative, "docs") == 0);
  click("UP"); await_scan(); assert(g_scan.relative[0] == '\0');
  click("readme.txt");
  assert(find_text(lv_screen_active(), "FILE DETAIL"));
  await_preview();
  assert(find_text(lv_screen_active(), "fixture"));
  snprintf(shot, sizeof(shot), "%s/text-preview.ppm", argv[1]); screenshot(shot);
  click("BACK"); await_scan();
  click("readme.txt");
  click("HOME"); await_preview();
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  assert(!find_text(lv_screen_active(), "fixture"));
  click("NETWORK");
  assert(find_text(lv_screen_active(), "NETWORK"));
  click("REFRESH");
  assert(find_text(lv_screen_active(), "NETWORK"));
  click("SCAN WI-FI");
  dolphin_wifi_timer(NULL);
  assert(g_wifi_ticket == 1);
  click("HOME");
  wifi_poll_error = 0;
  dolphin_wifi_timer(NULL);
  assert(g_wifi_ticket == 0 && find_text(lv_screen_active(), "DOLPHIN"));
  click("NETWORK"); click("SCAN WI-FI");
  wifi_result.found = 7; wifi_result.returned = 1;
  strcpy(wifi_result.aps[0].ssid, "test network");
  wifi_result.aps[0].rssi = -45; wifi_result.aps[0].channel = 6;
  wifi_result.aps[0].security = BK7258_WIFI_SECURITY_WPA2_AES;
  dolphin_wifi_timer(NULL);
  assert(find_text(lv_screen_active(), "Found 7; showing strongest 1"));
  assert(find_text(lv_screen_active(), "test network\nChannel 6 | -45 dBm"));
  dolphin_wifi_results(NULL);
  click("SAVE REPORT"); click("HOME"); await_report();
  assert(g_report.error == 0);
  file = fopen(g_report.path, "r"); assert(file);
  report_length = fread(report_text, 1, sizeof(report_text) - 1, file);
  report_text[report_length] = '\0';
  assert(strstr(report_text, "Dolphin Wi-Fi scan report"));
  assert(strstr(report_text, "Found 7; showing 1; truncated 0"));
  assert(strstr(report_text, "test network | Channel 6 | -45 dBm"));
  fclose(file);
  click("NETWORK"); click("SCAN WI-FI"); dolphin_wifi_timer(NULL);
  assert(find_text(lv_screen_active(), "Found 7; showing strongest 1"));
  click("test network\nChannel 6 | -45 dBm");
  assert(find_text(lv_screen_active(), "WI-FI DETAIL"));
  assert(contains_text(lv_screen_active(), "Security: WPA2-AES (6)"));
  wifi_connect_result.status = 0;
  wifi_connect_result.ipaddr = inet_addr("192.0.2.10");
  wifi_connect_result.netmask = inet_addr("255.255.255.0");
  wifi_connect_result.router = inet_addr("192.0.2.1");
  wifi_connect_result.rssi = -48;
  wifi_connect_result.link_state = 1;
  dolphin_wifi_connect(NULL);
  assert(g_wifi_connect_password != NULL);
  lv_textarea_set_text(g_wifi_connect_password, "");
  dolphin_wifi_connect_start(NULL);
  assert(g_wifi_connect_password != NULL);
  assert(contains_text(lv_screen_active(), "Password required for secured network"));
  lv_textarea_set_text(g_wifi_connect_password, "secret123");
  wifi_connect_submit_error = -EBUSY;
  dolphin_wifi_connect_start(NULL);
  assert(g_wifi_connect_password != NULL);
  assert(strcmp(lv_textarea_get_text(g_wifi_connect_password), "secret123") == 0);
  assert(contains_text(lv_screen_active(), "Connect unavailable (-16)"));
  wifi_connect_submit_error = 0;
  dolphin_wifi_results(NULL);
  assert(g_wifi_connect_password == NULL);
  click("test network\nChannel 6 | -45 dBm");
  dolphin_wifi_connect(NULL);
  lv_textarea_set_text(g_wifi_connect_password, "secret123");
  dolphin_wifi_connect_start(NULL);
  assert(g_wifi_connect_ticket == 3);
  assert(g_wifi_connect_password == NULL);
  assert(contains_text(lv_screen_active(), "SSID: test network\nConnecting..."));
  dolphin_home(NULL);
  assert(wifi_connect_cancels > 0 && g_wifi_connect_ticket == 3);
  wifi_connect_poll_error = 0;
  dolphin_wifi_connect_timer(NULL);
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  click("NETWORK"); click("SCAN WI-FI"); dolphin_wifi_timer(NULL);
  click("test network\nChannel 6 | -45 dBm");
  dolphin_wifi_connect(NULL);
  lv_textarea_set_text(g_wifi_connect_password, "secret123");
  dolphin_wifi_connect_start(NULL);
  assert(strcmp(wifi_connect_ssid, "test network") == 0);
  assert(strcmp(wifi_connect_password, "secret123") == 0);
  wifi_connect_poll_error = 0;
  dolphin_wifi_connect_timer(NULL);
  assert(contains_text(lv_screen_active(), "Connected temporarily"));
  assert(contains_text(lv_screen_active(), "192.0.2.10"));
  click("SAVE REPORT"); click("HOME"); await_report();
  assert(g_report.error == 0);
  file = fopen(g_report.path, "r"); assert(file);
  report_length = fread(report_text, 1, sizeof(report_text) - 1, file);
  assert(report_length > 0);
  report_text[report_length] = '\0';
  assert(strstr(report_text, "Dolphin Wi-Fi connection report"));
  assert(strstr(report_text, "Connection status: 0"));
  assert(strstr(report_text, "Link state: 1"));
  assert(strstr(report_text, "IP: 192.0.2.10"));
  assert(strstr(report_text, "Mask: 255.255.255.0"));
  assert(strstr(report_text, "Gateway: 192.0.2.1"));
  assert(strstr(report_text, "RSSI: -48 dBm"));
  assert(!strstr(report_text, "secret123"));
  fclose(file);
  click("NETWORK"); click("SCAN WI-FI"); dolphin_wifi_timer(NULL);
  wifi_result.found = 33; wifi_result.returned = 32;
  wifi_result.truncated = 1;
  for (unsigned int i = 0; i < wifi_result.returned; i++)
    {
      snprintf(wifi_result.aps[i].ssid, sizeof(wifi_result.aps[i].ssid),
               "network%02u", i);
      wifi_result.aps[i].rssi = -(int32_t)(20 + i);
      wifi_result.aps[i].channel = (uint8_t)(i % 13 + 1);
    }
  wifi_result.aps[31].security = UINT8_MAX;
  click("SCAN AGAIN"); dolphin_wifi_timer(NULL);
  assert(find_text(lv_screen_active(), "Found 33; showing strongest 32"));
  assert(contains_text(lv_screen_active(), "1 additional networks truncated"));
  click("network31\nChannel 6 | -51 dBm");
  assert(find_text(lv_screen_active(), "WI-FI DETAIL"));
  assert(contains_text(lv_screen_active(), "Security: Unknown (255)"));
  dolphin_wifi_connect(NULL);
  assert(contains_text(lv_screen_active(), "unsupported security type"));
  click("BACK");
  click("network31\nChannel 6 | -51 dBm");
  click("SAVE REPORT"); await_report();
  assert(g_report.error == 0);
  snprintf(report_path, sizeof(report_path), "%s", g_report.path);
  {
    const char *slash = strrchr(report_path, '/');
    assert(slash != NULL);
    snprintf(report_name, sizeof(report_name), "%s", slash + 1);
  }
  file = fopen(g_report.path, "r"); assert(file);
  report_length = fread(report_text, 1, sizeof(report_text) - 1, file);
  assert(report_length > 0);
  report_text[report_length] = '\0';
  assert(strstr(report_text, "Dolphin Wi-Fi scan report"));
  assert(strstr(report_text, "33 found, 32 shown, 1 truncated"));
  assert(strstr(report_text, "Security: Unknown (255)"));
  fclose(file);
  click("HOME"); click("FILES"); await_scan();
  click("dolphin/"); await_scan(); click("reports/"); await_scan();
  click(report_name); await_preview();
  assert(contains_text(lv_screen_active(), "Dolphin Wi-Fi scan report"));
  click("HOME"); click("NETWORK"); click("SCAN WI-FI");
  dolphin_wifi_timer(NULL);
  click("SCAN AGAIN"); wifi_result.status = -ETIMEDOUT;
  dolphin_wifi_timer(NULL);
  assert(find_text(lv_screen_active(), "Scan failed (-110)"));
  wifi_submit_error = -EBUSY; click("SCAN AGAIN");
  assert(g_wifi_ticket == 0);
  assert(find_text(lv_screen_active(), "RETRY"));
  wifi_connect_result.link_state = BK7258_WIFI_LINK_CONNECTED;
  click("HOME"); click("NETWORK"); click("CHANNEL STATS");
  assert(contains_text(lv_screen_active(), "Sampling requires disconnecting Wi-Fi"));
  click("DISCONNECT & SAMPLE");
  click("STOP"); assert(diagnostic_cancels > 0);
  dolphin_channels_timer(NULL);
  assert(find_text(lv_screen_active(), "CH 1: 12 frames / 900 bytes"));
  click("START AGAIN"); click("HOME"); dolphin_channels_timer(NULL);
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  click("NETWORK");
  assert(find_text(lv_screen_active(), "CHECK GATEWAY"));
  click("CHECK GATEWAY");
  assert(contains_text(lv_screen_active(), "Checking gateway response"));
  wifi_connect_poll_error = 0;
  dolphin_gateway_timer(NULL);
  assert(contains_text(lv_screen_active(), "Gateway 192.0.2.1 responded"));
  assert(contains_text(lv_screen_active(), "does not prove Internet access"));
  click("HOME"); click("NETWORK"); click("BLE BROADCASTS");
  ble_snapshot.count = 1;ble_snapshot.results[0].rssi = -42;
  ble_snapshot.results[0].payload_length = 4;
  memcpy(ble_snapshot.results[0].payload, "\3\11Hi", 4);
  dolphin_ble_timer(NULL);
  assert(find_text(lv_screen_active(), "Hi | -42 dBm | UUID none | mfg 0 bytes"));
  dolphin_ble_results(NULL);
  click("SAVE REPORT"); click("HOME"); await_report();
  assert(g_report.error == 0);
  file = fopen(g_report.path, "r"); assert(file);
  report_length = fread(report_text, 1, sizeof(report_text) - 1, file);
  report_text[report_length] = '\0';
  assert(strstr(report_text, "Dolphin BLE scan report"));
  assert(strstr(report_text, "Found 1; showing 1; truncated 0"));
  assert(strstr(report_text, "Hi | -42 dBm | UUID none | mfg 0 bytes"));
  fclose(file);
  click("NETWORK"); click("BLE BROADCASTS");
  ble_snapshot.count = 1;ble_snapshot.results[0].rssi = -42;
  ble_snapshot.results[0].payload_length = 4;
  memcpy(ble_snapshot.results[0].payload, "\3\11Hi", 4);
  dolphin_ble_timer(NULL);
  click("Hi | -42 dBm | UUID none | mfg 0 bytes");
  assert(find_text(lv_screen_active(), "BLE DETAIL"));
  g_report_test_hold = true;
  click("SAVE REPORT"); click("HOME");
  pthread_mutex_lock(&g_report.lock);
  snprintf(g_report.text, sizeof(g_report.text), "changed after submit");
  pthread_mutex_unlock(&g_report.lock);
  g_report_test_hold = false;
  await_report(); assert(g_report.error == 0);
  assert(find_text(lv_screen_active(), "DOLPHIN"));
  file = fopen(g_report.path, "r"); assert(file);
  assert(fgets(path, sizeof(path), file));
  assert(strstr(path, "Dolphin BLE scan report"));
  fclose(file);
  assert(strcmp(report_path, g_report.path) != 0);
  click("NETWORK"); click("BLE BROADCASTS");
  ble_snapshot.count = 1;ble_snapshot.results[0].rssi = -42;
  ble_snapshot.results[0].payload_length = 4;
  memcpy(ble_snapshot.results[0].payload, "\3\11Hi", 4);
  dolphin_ble_timer(NULL);
  click("Hi | -42 dBm | UUID none | mfg 0 bytes");
  snprintf(storage_root, sizeof(storage_root), "/tmp/dolphin-not-mounted");
  click("SAVE REPORT"); await_report();
  assert(g_report.error == -ENOENT);
  assert(contains_text(lv_screen_active(), "storage is not mounted"));
  snprintf(storage_root, sizeof(storage_root), "%s", root);
  click("BACK");
  click("START AGAIN");
  click("STOP"); dolphin_ble_timer(NULL); assert(!g_ble_owned);
  click("START AGAIN"); click("HOME"); dolphin_ble_timer(NULL);
  assert(!g_ble_owned && find_text(lv_screen_active(), "DOLPHIN"));
  click("HOME"); click("DEVICE");
  assert(find_text(lv_screen_active(), "DEVICE"));
  click("HOME");
  width = 480; height = 320;
  lv_display_set_resolution(g_display, width, height);
  dolphin_home(NULL);
  snprintf(shot, sizeof(shot), "%s/home-landscape.ppm", argv[1]); screenshot(shot);
  for (int i = 0; i < 70; i++)
    {
      snprintf(path, sizeof(path), "%s/f%02d", storage_root, i);
      file = fopen(path, "w"); assert(file); fclose(file);
    }
  click("FILES"); await_scan();
  assert(g_scan.count == DOLPHIN_MAX_FILES && g_scan.more);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t *home = find_text(lv_screen_active(), "HOME");
  lv_area_t area; lv_obj_get_coords(lv_obj_get_parent(home), &area);
  assert(area.y1 >= 0 && area.y2 < 60);
  click("HOME"); assert(find_text(lv_screen_active(), "DOLPHIN"));
  lv_display_delete(g_display); lv_deinit();
  for (int i = 0; i < 70; i++)
    { snprintf(path, sizeof(path), "%s/f%02d", storage_root, i); unlink(path); }
  snprintf(path, sizeof(path), "%s/link", storage_root); unlink(path);
  snprintf(path, sizeof(path), "%s/readme.txt", storage_root); unlink(path);
  snprintf(path, sizeof(path), "%s/docs", storage_root); rmdir(path);
  unlink(report_path);
  snprintf(path, sizeof(path), "%s/dolphin/reports", storage_root); rmdir(path);
  snprintf(path, sizeof(path), "%s/dolphin", storage_root); rmdir(path);
  rmdir(storage_root); rmdir(base);
  puts("DOLPHIN_UI_HOST_PASS: real LVGL; host filesystem/network; no board proof");
  return 0;
}
