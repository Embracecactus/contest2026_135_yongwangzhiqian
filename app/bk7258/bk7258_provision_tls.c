/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_tls.h"
#include "bk7258_provision_gatt.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform_util.h>

/* Conservative notification pacing until measured HCI flow-control acceptance.
 * A positive GATT result means queued once, never retry that fragment. TLS
 * record authentication detects stream loss; provisioning uses an absolute
 * deadline while an authenticated control session uses a validated-packet
 * idle deadline.
 */
#define SEND_INTERVAL_MS 10u
#define HANDSHAKE_MS 30000u
#define SESSION_MS 120000u
#define WRITE_MS 5000u

static const int g_suites[] =
{
  MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, 0
};

void bkprov_tls_close(struct bkprov_tls_s *tls)
{
  if (tls == NULL || !tls->initialized)
    {
      return;
    }
  mbedtls_ssl_free(&tls->ssl);
  mbedtls_ssl_config_free(&tls->config);
  mbedtls_ctr_drbg_free(&tls->random);
  mbedtls_entropy_free(&tls->entropy);
  mbedtls_platform_zeroize(tls, sizeof(*tls));
}

static int fail(struct bkprov_tls_s *tls, int error)
{
  bkprov_tls_close(tls);
  return error;
}

static bool expired(const struct bkprov_tls_s *tls, uint64_t now)
{
  return now < tls->last_now ||
         (!tls->control && now - tls->started >= SESSION_MS) ||
         (tls->control && now - tls->control_activity >= SESSION_MS) ||
         (!tls->established && now - tls->started >= HANDSHAKE_MS) ||
         (tls->pending_size && now - tls->write_started >= WRITE_MS);
}

static int check(struct bkprov_tls_s *tls)
{
  uint64_t now;
  if (tls == NULL || !tls->initialized)
    {
      return -ENOTCONN;
    }
  if (bkprov_gatt_generation() != tls->generation)
    {
      return fail(tls, -ESTALE);
    }
  now = tls->now_ms(tls->clock_context);
  if (expired(tls, now))
    {
      return fail(tls, -ETIMEDOUT);
    }
  tls->last_now = now;
  return 0;
}

static int send_cipher(void *context, const unsigned char *data, size_t size)
{
  struct bkprov_tls_s *tls = context;
  uint64_t now = tls->now_ms(tls->clock_context);
  ssize_t ret;
  /* Never free ssl from its own BIO callback. The outer operation performs
   * teardown after the crypto stack unwinds.
   */
  if (expired(tls, now))
    {
      return MBEDTLS_ERR_NET_SEND_FAILED;
    }
  if (now < tls->next_send)
    {
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
  size = size < 20 ? size : 20;
  ret = bkprov_gatt_send(tls->generation, data, size);
  tls->next_send = now + SEND_INTERVAL_MS;
  if (ret > 0)
    {
      return (int)ret;
    }
  return ret == -ENOMEM || ret == -EAGAIN ?
         MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int recv_cipher(void *context, unsigned char *data, size_t size)
{
  struct bkprov_tls_s *tls = context;
  if (expired(tls, tls->now_ms(tls->clock_context)))
    {
      return MBEDTLS_ERR_NET_RECV_FAILED;
    }
  ssize_t ret = bkprov_gatt_read(tls->generation, data, size);
  return ret >= 0 ? (int)ret : ret == -EAGAIN ?
         MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

int bkprov_tls_start(struct bkprov_tls_s *tls, uint32_t generation,
                     mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                     uint64_t (*now_ms)(void *), void *clock_context)
{
  static const unsigned char personalization[] = "shaniu-provision-v1";
  int ret;
  if (tls == NULL || certificate == NULL || key == NULL || now_ms == NULL ||
      generation == 0)
    {
      return -EINVAL;
    }
  if (tls->initialized)
    {
      return -EALREADY;
    }
  if (generation != bkprov_gatt_generation())
    {
      return -ESTALE;
    }
  memset(tls, 0, sizeof(*tls));
  mbedtls_ssl_init(&tls->ssl);
  mbedtls_ssl_config_init(&tls->config);
  mbedtls_ctr_drbg_init(&tls->random);
  mbedtls_entropy_init(&tls->entropy);
  tls->initialized = true;
  tls->generation = generation;
  tls->now_ms = now_ms;
  tls->clock_context = clock_context;
  tls->started = tls->last_now = now_ms(clock_context);
  ret = mbedtls_ctr_drbg_seed(&tls->random, mbedtls_entropy_func,
                             &tls->entropy, personalization,
                             sizeof(personalization) - 1);
  if (ret != 0)
    {
      return fail(tls, -EIO);
    }
  if (!mbedtls_pk_can_do(key, MBEDTLS_PK_ECDSA) ||
      mbedtls_pk_check_pair(&certificate->pk, key, mbedtls_ctr_drbg_random,
                            &tls->random) != 0)
    {
      return fail(tls, -EKEYREJECTED);
    }
  ret = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_SERVER,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0)
    {
      return fail(tls, -EIO);
    }
  mbedtls_ssl_conf_rng(&tls->config, mbedtls_ctr_drbg_random, &tls->random);
  mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_min_tls_version(&tls->config, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&tls->config, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_ciphersuites(&tls->config, g_suites);
#ifdef MBEDTLS_SSL_RENEGOTIATION
  mbedtls_ssl_conf_renegotiation(&tls->config,
                                MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
#ifdef MBEDTLS_SSL_SESSION_TICKETS
  mbedtls_ssl_conf_session_tickets(&tls->config,
                                  MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
#endif
  ret = mbedtls_ssl_conf_own_cert(&tls->config, certificate, key);
  if (ret == 0)
    {
      ret = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    }
  if (ret != 0)
    {
      return fail(tls, -EKEYREJECTED);
    }
  mbedtls_ssl_set_bio(&tls->ssl, tls, send_cipher, recv_cipher, NULL);
  return check(tls);
}

int bkprov_tls_step(struct bkprov_tls_s *tls)
{
  int deadline;
  int ret = check(tls);
  if (ret < 0)
    {
      return ret;
    }
  if (!tls->established)
    {
      ret = mbedtls_ssl_handshake(&tls->ssl);
      /* Crypto can consume time too; test the handshake deadline before
       * promoting the state to established (which has a longer deadline).
       */
      deadline = check(tls);
      if (deadline < 0)
        {
          return deadline;
        }
      if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
          ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          return 0;
        }
      if (ret != 0)
        {
          return fail(tls, -EKEYREJECTED);
        }
      tls->established = true;
    }
  if (tls->pending_size != 0)
    {
      ret = mbedtls_ssl_write(&tls->ssl, tls->pending, tls->pending_size);
      if (ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
          return 0;
        }
      if (ret <= 0 || (size_t)ret != tls->pending_size)
        {
          return fail(tls, -EIO);
        }
      mbedtls_platform_zeroize(tls->pending, sizeof(tls->pending));
      tls->pending_size = 0;
    }
  ret = check(tls);
  return ret < 0 ? ret : 1;
}

int bkprov_tls_queue(struct bkprov_tls_s *tls, const void *data, size_t size)
{
  int ret = check(tls);
  if (ret < 0)
    {
      return ret;
    }
  if (!tls->established || tls->pending_size != 0)
    {
      return -EAGAIN;
    }
  if (data == NULL || size == 0 || size > sizeof(tls->pending))
    {
      return -EINVAL;
    }
  memcpy(tls->pending, data, size);
  tls->pending_size = size;
  tls->write_started = tls->last_now;
  return 0;
}

ssize_t bkprov_tls_read(struct bkprov_tls_s *tls, void *data, size_t size)
{
  int ret = check(tls);
  if (ret < 0)
    {
      return ret;
    }
  if (!tls->established || tls->pending_size != 0)
    {
      return -EAGAIN;
    }
  if (data == NULL || size == 0 || size > 1024)
    {
      return -EINVAL;
    }
  ret = mbedtls_ssl_read(&tls->ssl, data, size);
  if (ret == MBEDTLS_ERR_SSL_WANT_READ)
    {
      return -EAGAIN;
    }
  if (ret <= 0)
    {
      return fail(tls, -ECONNRESET);
    }
  if (check(tls) < 0)
    {
      mbedtls_platform_zeroize(data, (size_t)ret);
      return -ECONNRESET;
    }
  return ret;
}

int bkprov_tls_promote_control(struct bkprov_tls_s *tls)
{
  int ret = check(tls);
  if (ret < 0)
    {
      return ret;
    }
  if (!tls->established)
    {
      return -EAGAIN;
    }
  tls->control = true;
  tls->control_activity = tls->last_now;
  return 0;
}

int bkprov_tls_touch_control(struct bkprov_tls_s *tls)
{
  int ret = check(tls);
  if (ret < 0)
    {
      return ret;
    }
  if (!tls->control)
    {
      return -EPERM;
    }
  tls->control_activity = tls->last_now;
  return 0;
}
