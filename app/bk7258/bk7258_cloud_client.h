/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_CLIENT_H
#define __APP_BK7258_CLOUD_CLIENT_H
#include "bk7258_cloud_audio.h"
#include "bk7258_cloud_playback.h"
#include "bk7258_cloud_history.h"
/* Active history is owned by the turn state machine. Optional persistence
 * exports/imports this bounded context through the authenticated snapshot. Commit a pair
 * only after successful playback acknowledgement. Failed/cancelled requests
 * never mutate history; clear it on identity/persona change or user reset.
 */
void bkcloud_history_clear(struct bkcloud_history_s *history);
int bkcloud_history_commit(struct bkcloud_history_s *history,
                            const char *user, const char *assistant);
/* A camera is offered only after this turn's explicit product consent. The
 * owner captures on invocation and keeps the fresh JPEG alive until chat
 * returns. Agent tools never borrow a previous turn's image. */
struct bkcloud_camera_s
{
  int (*capture)(void *context, const uint8_t **jpeg, size_t *size);
  void *context;
};
int bkcloud_chat(struct bkcloud_client_s *client,
                 const struct bkcloud_config_s *config,
                 const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                 uint64_t deadline_ms, const char *persona,
                 const struct bkcloud_history_s *history, const char *input,
                 const struct bkcloud_camera_s *camera,
                 char *text, size_t capacity);
/* Use the existing 16 kHz turn audio owner, resampling the MiMo output.
 * Success means TTS is decoded and drain scheduled; runtime must poll the
 * turn to IDLE with last_error == 0 before committing conversation history.
 */
int bkcloud_synthesize_turn(struct bkcloud_client_s *client,
                            struct bkcloud_tts_s *decoder,
                            struct bkcloud_playback_s *play,
                            struct bkvoice_turn_s *turn,
                            const struct bkcloud_config_s *config,
                            const struct bkvoice_wss_tls_ops_s *tls,
                            void *tls_context, uint64_t deadline_ms,
                            uint64_t (*now_ms)(void *), void *clock_context,
                            const char *text);
#endif
