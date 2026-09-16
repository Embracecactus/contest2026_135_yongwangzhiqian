/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_scan.h"
#include <nuttx/config.h>
#include <errno.h>
#include <string.h>

#if defined(CONFIG_BK7258_WIFI_VNET) && defined(CONFIG_BK7258_AP_CORE)
#include <arch/chip/bk7258_wifi.h>

static struct
{
  uint32_t ticket;
  bool active;
  bool retired;
  struct bk7258_wifi_scan_snapshot_s snapshot;
} g_scan;

static uint8_t ssid_size(const char ssid[33])
{
  uint8_t size = 0;
  while (size < BK7258_WIFI_SSID_MAX_LEN && ssid[size] != '\0')
    {
      size++;
    }
  return size;
}

int bkprov_scan_start(void)
{
  uint32_t ticket;
  int ret;

  if (g_scan.active)
    {
      return -EBUSY;
    }

  ret = bk7258_wifi_scan_async(BK7258_WIFI_SCAN_DEFAULT_MS, &ticket);
  if (ret < 0)
    {
      return ret;
    }

  g_scan.ticket = ticket;
  g_scan.active = true;
  g_scan.retired = false;
  return 0;
}

int bkprov_scan_poll(struct bkprov_scan_result_s *result)
{
  unsigned int i;
  int ret;

  if (!g_scan.active)
    {
      return -ESTALE;
    }

  ret = bk7258_wifi_scan_snapshot_poll(g_scan.ticket, &g_scan.snapshot);
  if (ret == -EAGAIN)
    {
      return ret;
    }

  g_scan.active = false;
  if (ret < 0)
    {
      return ret;
    }

  if (result == NULL || g_scan.retired)
    {
      return -ECANCELED;
    }

  memset(result, 0, sizeof(*result));
  result->status = g_scan.snapshot.status;
  if (result->status < 0)
    {
      return 0;
    }
  result->truncated = g_scan.snapshot.truncated != 0 ||
                      g_scan.snapshot.found > g_scan.snapshot.returned;
  for (i = 0; i < g_scan.snapshot.returned && i < BK7258_WIFI_AP_SCAN_MAX_RESULTS; i++)
    {
      uint8_t size = ssid_size(g_scan.snapshot.aps[i].ssid);
      int32_t rssi = g_scan.snapshot.aps[i].rssi;
      if (size == 0)
        {
          continue;
        }
      if (result->count == BKPROV_SCAN_MAX_APS)
        {
          result->truncated = true;
          continue;
        }
      struct bkprov_scan_ap_s *ap = &result->aps[result->count++];
      ap->ssid_len = size;
      ap->rssi = rssi < -128 ? -128 : rssi > 127 ? 127 : (int8_t)rssi;
      ap->channel = g_scan.snapshot.aps[i].channel;
      ap->security = g_scan.snapshot.aps[i].security;
      memcpy(ap->ssid, g_scan.snapshot.aps[i].ssid, size);
    }
  return 0;
}

void bkprov_scan_close(void)
{
  if (g_scan.active)
    {
      g_scan.retired = true;
    }
}

void bkprov_scan_drain(void)
{
  if (g_scan.active && g_scan.retired)
    {
      (void)bkprov_scan_poll(NULL);
    }
}

bool bkprov_scan_busy(void)
{
  return g_scan.active;
}
#else
int bkprov_scan_start(void) { return -ENOSYS; }
int bkprov_scan_poll(struct bkprov_scan_result_s *result)
{ (void)result; return -ENOSYS; }
void bkprov_scan_close(void) { }
void bkprov_scan_drain(void) { }
bool bkprov_scan_busy(void) { return false; }
#endif
