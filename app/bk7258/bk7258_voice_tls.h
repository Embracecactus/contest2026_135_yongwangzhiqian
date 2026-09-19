/****************************************************************************
 * app/bk7258/bk7258_voice_tls.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Verified mbedTLS byte-stream provider for cloud HTTP and legacy WSS.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_TLS_H
#define __APP_BK7258_BK7258_VOICE_TLS_H

#include "bk7258_voice_transport.h"

#include <pthread.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <netinet/in.h>
#ifdef __NuttX__
#  include <nuttx/net/net.h>
#endif

/* The deployment owns these credentials until uninitialize().  It must not
 * modify them, and the private-key context is exclusive to this provider.
 * They are borrowed handles, never filenames or logged identities.  The
 * dialing address is provisioned separately from the peer certificate name:
 * no unbounded synchronous DNS call is hidden inside open_verified().
 *
 * trusted_time() must reject an unset/untrusted system wall clock, which
 * mbedTLS uses to validate the entire certificate chain.  now_ms() uses the
 * same monotonic clock as the session's absolute I/O deadlines.
 */

struct bkvoice_tls_config_s
{
  struct in_addr peer_address;
  mbedtls_x509_crt *server_ca;
  mbedtls_x509_crt *client_certificate;
  mbedtls_pk_context *client_key;
  int (*trusted_time)(void *context);
  uint64_t (*now_ms)(void *context);
  void *clock_context;
  /* Explicit cloud API mode. Both client identity pointers must be NULL.
   * Server CA, hostname and trusted-time checks remain mandatory.
   * Default false preserves the existing mutual-TLS contract.
   */
  bool server_auth_only;
  /* 可选 TLS 1.2 会话缓存，只用于 server_auth_only，两个回调成对提供。
   * load 在 setup/hostname 后、握手前复制匹配配置的会话，返回 0 或负错误。
   * save 在 CA/主机名/时间验证后复核恢复会话的认证期限；返回 0 或负错误。
   * 仅缓存分配失败时仍返回 0，不得将有效握手改判为失败。
   * 回调不得修改 ssl 的认证配置；缓存 owner 负责端点、信任配置、有效期
   * 隔离及并发复制。不会共享活动连接，也不会自动重试应用请求。
   */
  int (*session_load)(void *context, mbedtls_ssl_context *ssl,
                       const char *host, uint16_t port);
  int (*session_save)(void *context, const mbedtls_ssl_context *ssl,
                       const char *host, uint16_t port);
  void *session_context;
};

struct bkvoice_tls_s
{
  struct bkvoice_tls_config_s config;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config ssl_config;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  pthread_mutex_t crypto_lock;
#ifdef __NuttX__
  struct socket socket;
#else
  int fd;
#endif
  bool socket_open;
  volatile bool interrupted;
  volatile bool faulted;
  bool initialized;
  bool opened;
};

/* As with the byte-stream contract, open/close/uninitialize belong to one owner.
 * One sender and one receiver may run concurrently.  All mbedTLS calls on
 * shared contexts are serialized.  Idle reads release the lock while waiting;
 * writes retain it through WANT_WRITE retries to preserve record identity.
 * A terminal I/O error poisons both directions until close/open.  interrupt only sets a
 * flag; nonblocking I/O notices it within one 20 ms readiness slice.  The
 * owner joins both callers before close; no fd can be closed/reused under
 * a waiting caller.  This provider negotiates TLS 1.2 without renegotiation.
 */

int bkvoice_tls_initialize(struct bkvoice_tls_s *tls,
                           const struct bkvoice_tls_config_s *config);
int bkvoice_tls_uninitialize(struct bkvoice_tls_s *tls);
const struct bkvoice_wss_tls_ops_s *bkvoice_tls_ops(void);

#endif /* __APP_BK7258_BK7258_VOICE_TLS_H */
