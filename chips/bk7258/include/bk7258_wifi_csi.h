/****************************************************************************
 * chips/bk7258/include/bk7258_wifi_csi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_CSI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_CSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_CSI_MAGIC          0x49534342u
#define BK7258_CSI_VERSION        1u
#define BK7258_CSI_HEADER_SIZE    64u
#define BK7258_CSI_MAX_SAMPLES    52u
#define BK7258_CSI_QUEUE_SIZE     16u
#define BK7258_CSI_DEFAULT_MS     40u
#define BK7258_CSI_MAX_DURATION   600000u

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_wifi_csi_config_s
{
  uint32_t address;                /* Network byte order. */
  uint32_t duration_ms;
  uint16_t port;                   /* Host byte order. */
  uint16_t interval_ms;
  uint8_t format;                  /* 1: NON_HT, 2: HT_MM, 3: both. */
  uint8_t reserved[3];
};

struct bk7258_wifi_csi_result_s
{
  int32_t status;
  uint32_t active;
  uint32_t session;
  uint32_t callbacks;
  uint32_t invalid;
  uint32_t queue_drops;
  uint32_t sent;
  uint32_t send_errors;
  uint32_t last_len;
  uint32_t last_type;
  uint32_t queued;
  int32_t last_error;
};

/* BCSI v1 uses a 64-byte little-endian header, followed by count raw uint32
 * words. kind=1 carries CSI; kind=2 is a start/stop snapshot (count=0).
 * Raw words are preserved; vendor len is a count, never a byte count.
 * sent is the ordinal accepted by the local UDP stack, not a delivery ACK.
 */

struct bk7258_wifi_csi_packet_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t kind;
  uint32_t session;
  uint32_t sequence;
  uint32_t timestamp_us;
  uint32_t callbacks;
  uint32_t invalid;
  uint32_t queue_drops;
  uint32_t sent;
  uint32_t send_errors;
  uint16_t count;
  uint16_t data_type;
  int8_t rssi;
  uint8_t channel;
  uint8_t format;
  uint8_t bandwidth;
  uint32_t active;
  uint8_t mac[6];
  uint8_t mcs;
  uint8_t rate;
  uint32_t uptime_ms;
  uint32_t words[BK7258_CSI_MAX_SAMPLES];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_WIFI_CSI
#  ifdef CONFIG_BK7258_AP_CORE
/* Called only by the existing AP logical-CPU0 Wi-Fi control worker. */

int bk7258_wifi_csi_start(const struct bk7258_wifi_csi_config_s *config);
int bk7258_wifi_csi_stop(void);
void bk7258_wifi_csi_poll(void);
uint32_t bk7258_wifi_csi_active(void);
void bk7258_wifi_csi_snapshot(struct bk7258_wifi_csi_result_s *result);
#  endif
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_WIFI_CSI_H */
