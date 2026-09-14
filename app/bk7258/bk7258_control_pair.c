/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_control_pair.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>

static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }

void bkcontrol_pair_close(struct bkcontrol_pair_s *pair)
{
  if (pair == NULL) return;
  bkcontrol_session_close(&pair->session);
  bkprov_tls_close(&pair->tls);
  mbedtls_platform_zeroize(pair, sizeof(*pair));
}

int bkcontrol_pair_start(struct bkcontrol_pair_s *pair, uint32_t generation,
                         mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                         const uint8_t owner_key[32], uint64_t (*now_ms)(void *),
                         void *clock_context, bkcontrol_execute_t execute,
                         void *context)
{
  int ret;
  if (pair == NULL || now_ms == NULL) return -EINVAL;
  if (pair->tls.initialized || pair->session.open) return -EBUSY;
  ret = bkcontrol_session_open(&pair->session, owner_key, execute, context);
  if (ret < 0) return ret;
  ret = bkprov_tls_start(&pair->tls, generation, certificate, key, now_ms, clock_context);
  if (ret < 0) { bkcontrol_pair_close(pair); return ret; }
  pair->expected = 16;
  return 0;
}

int bkcontrol_pair_step(struct bkcontrol_pair_s *pair)
{
  int ret;
  ssize_t size;
  uint64_t now;
  bool authenticated;
  if (pair == NULL || !pair->tls.initialized) return -ENOTCONN;
  now = pair->tls.now_ms(pair->tls.clock_context);
  ret = bkprov_tls_step(&pair->tls);
  if (ret < 0) goto fail;
  if (pair->tls.established && !pair->session.authenticated)
    {
      if (!pair->authenticating)
        {
          pair->authenticating = true;
          pair->authentication_started = now;
        }
      if (now < pair->authentication_started ||
          now - pair->authentication_started >= 10000u)
        { ret = -ETIMEDOUT; goto fail; }
    }
  if (ret == 0) return 0;
  if (pair->report)
    {
      ret = bkprov_tls_queue(&pair->tls, pair->response, sizeof(pair->response));
      if (ret == -EAGAIN) return 0;
      if (ret < 0) goto fail;
      mbedtls_platform_zeroize(pair->response, sizeof(pair->response));
      pair->report = false;
      return 0;
    }
  size = bkprov_tls_read(&pair->tls, pair->input + pair->received,
                         pair->expected - pair->received);
  if (size == -EAGAIN) return 0;
  if (size <= 0) { ret = size < 0 ? (int)size : -ECONNRESET; goto fail; }
  if ((size_t)size > pair->expected - pair->received)
    { ret = -EIO; goto fail; }
  pair->received += (size_t)size;
  if (pair->received != pair->expected) return 0;
  if (pair->expected == 16)
    {
      uint32_t payload = get32(pair->input + 12);
      if (memcmp(pair->input, "SDC1", 4) || payload > 32)
        { ret = -EPROTO; goto fail; }
      pair->expected += payload;
      if (payload != 0) return 0;
    }
  authenticated = pair->session.authenticated;
  ret = bkcontrol_session_packet(&pair->session, pair->input,
                                 pair->received, pair->response);
  mbedtls_platform_zeroize(pair->input, sizeof(pair->input));
  pair->received = 0;
  pair->expected = 16;
  if (ret < 0) goto fail;
  ret = !authenticated && pair->session.authenticated ?
        bkprov_tls_promote_control(&pair->tls) :
        bkprov_tls_touch_control(&pair->tls);
  if (ret < 0) goto fail;
  pair->report = true;
  return 0;
fail:
  bkcontrol_pair_close(pair);
  return ret;
}
