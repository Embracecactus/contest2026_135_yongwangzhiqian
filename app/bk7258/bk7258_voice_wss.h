/****************************************************************************
 * app/bk7258/bk7258_voice_wss.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * App-private companion-v1 over verified TLS/WebSocket transport adapter.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_WSS_H
#define __APP_BK7258_BK7258_VOICE_WSS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_voice_companion.h"
#include "bk7258_voice_transport.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKVOICE_WSS_HOST_MAX        127u
#define BKVOICE_WSS_PATH_MAX        127u
#define BKVOICE_WSS_SUBPROTOCOL_MAX 31u
#define BKVOICE_WSS_MAX_MESSAGE \
  (BKVOICE_COMPANION_HEADER_BYTES + \
   BKVOICE_COMPANION_AUDIO_FRAME_BYTES)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bkvoice_wss_config_s
{
  const char *host;
  uint16_t port;
  const char *path;
  const char *subprotocol;
};

struct bkvoice_wss_snapshot_s
{
  uint32_t tx_messages;
  uint32_t rx_messages;
  uint32_t ping_count;
  uint32_t pong_count;
  int last_error;
  bool initialized;
  bool tls_open;
  bool upgraded;
  bool interrupted;
  bool peer_closed;
  bool faulted;
};

struct bkvoice_wss_s
{
  struct bkvoice_wss_tls_ops_s tls_ops;
  void *tls_context;
  pthread_mutex_t tx_lock;
  char host[BKVOICE_WSS_HOST_MAX + 1u];
  char path[BKVOICE_WSS_PATH_MAX + 1u];
  char subprotocol[BKVOICE_WSS_SUBPROTOCOL_MAX + 1u];
  uint8_t rx_message[BKVOICE_WSS_MAX_MESSAGE];
  size_t rx_size;
  size_t rx_offset;
  uint32_t tx_messages;
  uint32_t rx_messages;
  uint32_t ping_count;
  uint32_t pong_count;
  volatile int last_error;
  uint16_t port;
  volatile bool tls_open;
  volatile bool upgraded;
  volatile bool interrupted;
  volatile bool peer_closed;
  volatile bool faulted;
  bool initialized;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bkvoice_wss_initialize(
  struct bkvoice_wss_s *wss,
  const struct bkvoice_wss_tls_ops_s *tls_ops, void *tls_context,
  const struct bkvoice_wss_config_s *config);
int bkvoice_wss_uninitialize(struct bkvoice_wss_s *wss);
const struct bkvoice_transport_ops_s *bkvoice_wss_transport_ops(void);
void bkvoice_wss_snapshot(const struct bkvoice_wss_s *wss,
                          struct bkvoice_wss_snapshot_s *snapshot);

#endif /* __APP_BK7258_BK7258_VOICE_WSS_H */
