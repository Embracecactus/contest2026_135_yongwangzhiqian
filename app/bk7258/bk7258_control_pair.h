/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CONTROL_PAIR_H
#define __APP_BK7258_CONTROL_PAIR_H
#include "bk7258_control_session.h"
#include "bk7258_provision_tls.h"

/* Same GATT ciphertext channel and TLS implementation as provisioning.
 * 每条连接只有一个 TLS owner。首帧 SPV1 可分流到借用该 TLS 的只读扫描；
 * 普通控制仍只接受已保存的控制密钥，不能拿持有证明替代控制认证。
 */
struct bkcontrol_pair_s
{
  struct bkprov_tls_s tls;
  struct bkprov_pair_s *scan;
  uint8_t scan_secret[32];
  const struct bkprov_claim_ops_s *rebind_ops;
  void *rebind_context;
  struct bkcontrol_session_s session;
  uint8_t input[BKCONTROL_REQUEST_MAX];
  uint8_t response[BKCONTROL_RESPONSE_SIZE];
  size_t received;
  size_t expected;
  uint64_t authentication_started;
  bool authenticating;
  bool report;
};
int bkcontrol_pair_start(struct bkcontrol_pair_s *, uint32_t generation,
                         mbedtls_x509_crt *, mbedtls_pk_context *,
                         const uint8_t owner_key[32], uint64_t (*now_ms)(void *),
                         void *clock_context, bkcontrol_execute_t, void *);
/* Serialized AP owner call. Crypto may run here, never in Host callbacks.
 * Negative result is terminal; owner must close/disconnect the GATT window.
 * Existing TLS limits apply: handshake 30s, session 120s, write 5s. Owner-key
 * authentication is additionally limited to 10s after TLS establishes.
 */
int bkcontrol_pair_step(struct bkcontrol_pair_s *);
void bkcontrol_pair_close(struct bkcontrol_pair_s *);
#endif
