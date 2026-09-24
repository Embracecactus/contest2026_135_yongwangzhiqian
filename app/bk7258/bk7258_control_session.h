/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CONTROL_SESSION_H
#define __APP_BK7258_CONTROL_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SDC1 plaintext inside pinned TLS only. Header: magic, opcode, sequence,
 * payload length, all BE32. AUTH=1/32 bytes, STATUS=2/empty, CANCEL=3/empty,
 * VOLUME=4/BE32 0..100, PERSONA=5/BE32 0..4, CLEAR_HISTORY=6/empty. AUTH starts at sequence zero;
 * subsequent requests increase by one. One request/response at a time.
 * Response opcode has bit31 set, payload is signed error plus five BE32
 * fields: flags, volume, persona, turn state, last runtime error. Unknown
 * fields are UINT32_MAX. Flags: ready=1, busy=2, turn-known=4, volume-known=8,
 * persona-known=16, memory-known=32, memory-enabled=64, memory-pending=128,
 * memory-failed=256, memory-supported=512. MEMORY_SET=7/BE32 0..1,
 * MEMORY_DELETE=8/empty, INFO=9/empty. INFO response replaces the normal
 * status fields with major, minor, revision, build and security counter. OTA
 * responses replace them with state, phase, progress, total and result.
 * Unknown response fields are UINT32_MAX (and -1 for signed result);
 * BEGIN/APPEND do not invent runtime progress.
 * pending must clear and failed remain false before success is displayed.
 * wifi-known=1024, wifi-ready=2048: coherent link plus usable AP lease only,
 * never proof of Internet/provider reachability. Older firmware omits both. A successful CANCEL acknowledges the request, not drain.
 * No API key, Wi-Fi credential, factory secret or owner key is returned.
 */
#define BKCONTROL_CONFIG_APPEND_MAX 512u
#define BKCONTROL_REQUEST_MAX (16u + BKCONTROL_CONFIG_APPEND_MAX)
#define BKCONTROL_RESPONSE_SIZE 40u
/* Existing SDC1 wire value, independent of the conversational runtime. */
#define BKCONTROL_TURN_IDLE 0u
/* Existing OTA response wire values, independent of a voice runtime. */
enum bkcontrol_ota_state_e
{ BKCONTROL_OTA_IDLE = 0, BKCONTROL_OTA_QUEUED, BKCONTROL_OTA_ACTIVE,
  BKCONTROL_OTA_TERMINAL };
enum bkcontrol_ota_phase_e
{ BKCONTROL_OTA_DOWNLOADING = 1, BKCONTROL_OTA_VERIFYING,
  BKCONTROL_OTA_STAGED, BKCONTROL_OTA_REBOOTING, BKCONTROL_OTA_TRIAL,
  BKCONTROL_OTA_CONFIRMED, BKCONTROL_OTA_ROLLED_BACK, BKCONTROL_OTA_FAILED };
enum bkcontrol_command_e
{ BKCONTROL_AUTH = 1, BKCONTROL_STATUS, BKCONTROL_CANCEL,
  BKCONTROL_VOLUME, BKCONTROL_PERSONA, BKCONTROL_CLEAR_HISTORY, BKCONTROL_MEMORY_SET, BKCONTROL_MEMORY_DELETE, BKCONTROL_INFO,
  BKCONTROL_OTA_BEGIN, BKCONTROL_OTA_APPEND, BKCONTROL_OTA_START,
  BKCONTROL_OTA_STATUS, BKCONTROL_OTA_CANCEL,
  BKCONTROL_CONFIG_READ, BKCONTROL_CONFIG_BEGIN, BKCONTROL_CONFIG_APPEND,
  BKCONTROL_CONFIG_APPLY, BKCONTROL_CONFIG_CANCEL };
/* Public configuration uses the same authenticated, serialized connection.
 * READ: BE32(kind << 16 | byte offset), response error/total/16 data bytes.
 * BEGIN: BE32 kind + BE32 size; APPEND: 1..512 bytes; APPLY/CANCEL: empty.
 * Clients default to 32-byte APPEND until READ kind 0x7fff/offset 0 returns
 * CAP1 + BE32 revision 1 + BE32 maximum APPEND size (12 bytes total).
 * Unsupported capability reads on older firmware retain the 32-byte limit.
 * Other responses retain the normal STATUS layout. APPEND/CANCEL acknowledge
 * the staging operation with unknown snapshot fields; they do not read the
 * product's devices. Bit 16384 advertises
 * support. Kind 1 is MCP1 ASR/chat/TTS names only, never credentials.
 * Kind 9 is the factory-reset transaction. BEGIN/APPEND/APPLY carry SRT1:
 * magic, reserved BE32 zero, expected revision BE64, and a nonzero 16-byte
 * transaction. APPLY returns zero only after SRV1 is durable; -EAGAIN means
 * the client must retry that exact SRT1 transaction. READ normally carries
 * four bytes, but RESET_TRANSFER READ carries those four bytes followed by
 * the 16-byte transaction and returns SRS1 (state/reserved/transaction).
 * It is authenticated and read-only: PENDING is not completion and no query
 * can request or resume an erase.
 * The staging buffer is shared with OTA, so transfers cannot interleave.
 * APPLY acknowledges a worker request; READ must confirm its actual result.
 */
#define BKCONTROL_CONFIG_CLOUD_MODELS 1u
#define BKCONTROL_CONFIG_WAKE_MODEL 2u
#define BKCONTROL_CONFIG_WAKE_RESTORE 3u
/* RSP1 + BE32 thinking (0 = fast, 1 = deep thinking) + BE32 reserved = 0;
 * 12 bytes in total.
 */
#define BKCONTROL_CONFIG_RESPONSE_MODE 4u
/* Writes an EYE2 HTTPS source record; reads EYE1 state, pack identity and
 * render result (108 bytes).
 */
#define BKCONTROL_CONFIG_EYE_PACK 5u
/* KWT1 + BE32 score threshold percent (50..90) + BE32 reserved = 0. */
#define BKCONTROL_CONFIG_WAKE_THRESHOLD 6u
/* SCP1 save-first settings patch; SCS1 public operation/revision readback. */
#define BKCONTROL_CONFIG_SETTINGS 7u
/* Read-only WFS1: 12-byte header, then SSID length/RSSI/channel/security
 * and 32 SSID bytes per result. Reuses the device's single scan worker. */
#define BKCONTROL_CONFIG_WIFI_SCAN 8u
/* SRT1 request / SRS1 public receipt; no user configuration is returned. */
#define BKCONTROL_CONFIG_RESET_TRANSFER 9u
#define BKCONTROL_CONFIG_CAPABILITIES 0x7fffu
#define BKCONTROL_CONFIG_RECORD_MAX (140u + 65536u) /* WKM2 显式前端字段 */
struct bkcontrol_device_info_s
{
  uint32_t major;
  uint32_t minor;
  uint32_t revision;
  uint32_t build;
  uint32_t security_counter;
};
struct bkcontrol_ota_status_s
{
  uint32_t state;
  uint32_t phase;
  uint32_t progress;
  uint32_t total;
  int32_t result;
};
struct bkcontrol_status_s
{
  uint32_t flags;
  uint32_t volume;
  uint32_t persona;
  uint32_t turn;
  int32_t error;
  struct bkcontrol_device_info_s device_info;
  struct bkcontrol_ota_status_s ota;
  uint32_t config_total;
  uint8_t config_chunk[16];
};
/* Serialized AP owner callback, never ATT/CCC callback. No implicit retry.
 * Return 0 only for an accepted operation; populate confirmed fields only.
 */
typedef int (*bkcontrol_execute_t)(void *, enum bkcontrol_command_e,
                                   uint32_t, struct bkcontrol_status_s *);
/* START's record is borrowed only for the duration of the callback. The
 * callback must copy anything it needs before returning an accepted result.
 * STATUS and CANCEL always receive NULL and zero.
 */
typedef int (*bkcontrol_ota_t)(void *, enum bkcontrol_command_e,
                              const uint8_t *, size_t,
                              struct bkcontrol_status_s *);
typedef int (*bkcontrol_config_t)(void *, enum bkcontrol_command_e,
                                 uint32_t, uint32_t, const uint8_t *, size_t,
                                 struct bkcontrol_status_s *);
struct bkcontrol_session_s
{
  uint8_t secret[32];
  uint32_t sequence;
  bkcontrol_execute_t execute;
  void *context;
  bkcontrol_ota_t ota;
  bkcontrol_config_t config;
  union
  {
    uint8_t ota_record[3371];
    uint8_t config_record[BKCONTROL_CONFIG_RECORD_MAX];
  };
  uint32_t ota_total;
  uint32_t ota_received;
  uint32_t record_kind;
  bool open;
  bool authenticated;
  bool quiescing;
};
/* Zero initialize before first use. TLS lifetime/timeout belongs to transport.
 * Negative packet return is terminal: close transport, never continue parsing.
 * Successful packet return fills exactly RESPONSE_SIZE bytes. Caller queues
 * that response before accepting another request and clears request buffers.
 */
int bkcontrol_session_open(struct bkcontrol_session_s *, const uint8_t[32],
                           bkcontrol_execute_t, void *);
int bkcontrol_session_set_ota_handler(struct bkcontrol_session_s *, bkcontrol_ota_t);
int bkcontrol_session_set_config_handler(struct bkcontrol_session_s *, bkcontrol_config_t);
int bkcontrol_session_packet(struct bkcontrol_session_s *, const uint8_t *,
                             size_t, uint8_t[BKCONTROL_RESPONSE_SIZE]);
/* Serialized with packet processing. Authenticated, one-way admission gate:
 * keep STATUS/INFO, explicit cancellation and bounded settings/reset receipt
 * reads; reject new mutations with EBUSY. Authentication and frame validation
 * remain mandatory. A fresh session is required to resume ordinary writes.
 */
int bkcontrol_session_quiesce(struct bkcontrol_session_s *);
void bkcontrol_session_close(struct bkcontrol_session_s *);
#endif
