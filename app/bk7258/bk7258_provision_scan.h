/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_SCAN_H
#define __APP_BK7258_PROVISION_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#define BKPROV_SCAN_MAX_APS 24u

struct bkprov_scan_ap_s
{
  uint8_t ssid_len;
  int8_t rssi;
  uint8_t channel;
  uint8_t security;
  uint8_t ssid[32];
};

struct bkprov_scan_result_s
{
  int status;
  uint8_t count;
  bool truncated;
  struct bkprov_scan_ap_s aps[BKPROV_SCAN_MAX_APS];
};

/* There is one AP Wi-Fi worker. A completed ticket must be consumed even if
 * its BLE peer has gone away, so close only retires delivery to that peer. */
int bkprov_scan_start(void);
int bkprov_scan_poll(struct bkprov_scan_result_s *result);
void bkprov_scan_close(void);
void bkprov_scan_drain(void);
bool bkprov_scan_busy(void);
#endif
