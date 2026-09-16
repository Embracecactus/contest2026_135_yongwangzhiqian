/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_pair.h"
#include "bk7258_provision_gatt.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>

static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3]; }
static void put32(uint8_t *p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }

/* Recovery has no mutation backend. Even accidental state-machine dispatch
 * cannot call Wi-Fi or persist a candidate. */
static int deny_begin(void *p, const uint8_t *b, size_t n)
{ (void)p; (void)b; (void)n; return -EACCES; }
static int wait_receipt(void *p) { (void)p; return 0; }
static int deny_commit(void *p, const uint8_t t[16], const uint8_t *b, size_t n)
{ (void)p; (void)t; (void)b; (void)n; return -EACCES; }
static void no_abort(void *p) { (void)p; }
static const struct bkprov_claim_ops_s recovery_ops =
{ deny_begin, wait_receipt, deny_commit, no_abort };

void bkprov_pair_close(struct bkprov_pair_s *pair)
{
  if (pair == NULL) return;
  bkprov_scan_close();
  bkprov_claim_close(&pair->claim);
  bkprov_tls_close(&pair->tls);
  mbedtls_platform_zeroize(pair, sizeof(*pair));
}

int bkprov_pair_start(struct bkprov_pair_s *pair, uint32_t generation,
                      mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                      const uint8_t secret[32], bool local_action,
                      bool already_claimed, uint64_t (*now_ms)(void *),
                      void *clock_context, const struct bkprov_claim_ops_s *ops,
                      void *context)
{
  int ret;
  if (pair == NULL || now_ms == NULL) return -EINVAL;
  if (pair->tls.initialized || pair->claim.state != BKPROV_CLOSED) return -EBUSY;
  ret = bkprov_claim_open(&pair->claim,generation,secret,local_action,
                          already_claimed,now_ms(clock_context),ops,context);
  if (ret < 0) return ret;
  ret = bkprov_tls_start(&pair->tls,generation,certificate,key,now_ms,clock_context);
  if (ret < 0) { bkprov_pair_close(pair); return ret; }
  pair->expected = 32;
  pair->reported_state = BKPROV_AUTH;
  return 0;
}

int bkprov_pair_start_recovery(struct bkprov_pair_s *pair, uint32_t generation,
                               mbedtls_x509_crt *certificate,
                               mbedtls_pk_context *key,
                               const uint8_t secret[32], bool local_action,
                               uint64_t (*now_ms)(void *), void *clock_context,
                               int (*receipt)(const uint8_t transaction[16]))
{
  if (receipt == NULL) return -EINVAL;
  int ret = bkprov_pair_start(pair, generation, certificate, key, secret,
                              local_action, false, now_ms, clock_context,
                              &recovery_ops, NULL);
  if (ret == 0) { pair->recovery = true; pair->receipt = receipt; }
  return ret;
}

static int packet(struct bkprov_pair_s *pair)
{
  const uint8_t *p = pair->input;
  uint32_t sequence = get32(p+8), size = get32(p+28);
  int ret;
  if (p[4] == 1)
    {
      if (pair->claim.state != BKPROV_AUTH || sequence != 0 || size != 32)
        return -EPROTO;
      memcpy(pair->transaction,p+12,16);
      ret = bkprov_claim_auth(&pair->claim,pair->tls.generation,p+12,p+32);
    }
  else
    {
      if (memcmp(pair->transaction,p+12,16) || pair->claim.state < BKPROV_READY ||
          pair->claim.state >= BKPROV_CHECKING) return -EPROTO;
      if (pair->scan_pending || pair->scan_report || pair->scan_session)
        return -EPROTO;
      if (p[4] == 6)
        {
          if (pair->recovery || sequence != 1 || size != 0 ||
              pair->claim.state != BKPROV_READY) return -EPROTO;
          ret = bkprov_scan_start();
          pair->scan.status = ret;
          pair->scan_pending = ret == 0;
          pair->scan_report = ret != 0;
          pair->scan_session = true;
          pair->request_sequence = sequence;
          pair->report = ret != 0;
          return 0;
        }
      if (pair->recovery || (p[4] == 5 && pair->receipt != NULL))
        {
          if (p[4] != 5 || sequence != 1 || size != 0 ||
              pair->claim.state != BKPROV_READY) return -EPROTO;
          pair->claim.state = BKPROV_CHECKING;
          pair->query_pending = true;
          pair->request_sequence = sequence;
          pair->report = true;
          return 0;
        }
      switch (p[4])
        {
          case 2:
            ret = size == 4 ? bkprov_claim_begin(&pair->claim,sequence,get32(p+32)) : -EPROTO;
            break;
          case 3:
            ret = size > 4 ? bkprov_claim_data(&pair->claim,sequence,get32(p+32),p+36,size-4) : -EPROTO;
            break;
          case 4:
            ret = size == 0 ? bkprov_claim_apply(&pair->claim,sequence) : -EPROTO;
            break;
          default: return -EPROTO;
        }
    }
  pair->request_sequence = sequence;
  pair->report = true;
  /* A terminal state has a status to report before transport closes. A
   * malformed packet without a state transition invalidates the connection.
   */
  return ret < 0 && pair->claim.state != BKPROV_FAILED ? ret : 0;
}

static int report(struct bkprov_pair_s *pair)
{
  uint8_t *response = pair->output;
  size_t size = 40;
  int ret;
  memset(response, 0, 904);
  memcpy(response, "SPV1", 4);
  response[4] = pair->scan_report ? 129 : 128;
  put32(response+8,pair->request_sequence);
  memcpy(response+12,pair->transaction,16);
  if (pair->scan_report)
    {
      put32(response + 28, 8 + pair->scan.count * 36u);
      put32(response + 32, (uint32_t)pair->scan.status);
      response[36] = pair->scan.count;
      response[37] = pair->scan.truncated;
      for (uint8_t i = 0; i < pair->scan.count; i++)
        {
          uint8_t *record = response + 40 + i * 36u;
          const struct bkprov_scan_ap_s *ap = &pair->scan.aps[i];
          record[0] = ap->ssid_len;
          record[1] = (uint8_t)ap->rssi;
          record[2] = ap->channel;
          record[3] = ap->security;
          memcpy(record + 4, ap->ssid, ap->ssid_len);
        }
      size = 40 + pair->scan.count * 36u;
    }
  else
    {
      put32(response+28,8);
      put32(response+32,pair->claim.state);
      put32(response+36,(uint32_t)pair->claim.error);
    }
  ret = bkprov_tls_queue(&pair->tls,response,size);
  if (ret == 0)
    {
      pair->report = false;
      pair->scan_report = false;
      pair->reported_state = pair->claim.state;
    }
  return ret;
}

int bkprov_pair_confirm(struct bkprov_pair_s *pair, uint32_t generation)
{
  int ret;
  if (pair == NULL) return -EINVAL;
  ret = bkprov_claim_confirm(&pair->claim,generation);
  if (ret == 0) pair->report = true;
  return ret;
}

int bkprov_pair_step(struct bkprov_pair_s *pair)
{
  int ret;
  ssize_t received;
  if (pair == NULL || !pair->tls.initialized) return -ENOTCONN;
  /* Snapshot the transport clock before TLS step can free its context. */
  uint64_t now = pair->tls.now_ms(pair->tls.clock_context);
  (void)bkprov_claim_step(&pair->claim,bkprov_gatt_generation(),now);
  ret = bkprov_tls_step(&pair->tls);
  if (ret < 0) goto fail;
  if (ret == 0) return 0;
  if (pair->query_pending && pair->claim.state == BKPROV_CHECKING)
    {
      ret = pair->receipt(pair->transaction);
      if (ret != -EAGAIN)
        {
          pair->query_pending = false;
          pair->claim.state = ret == 1 ? BKPROV_COMMITTED :
                              ret == 0 ? BKPROV_NOT_COMMITTED : BKPROV_UNCERTAIN;
          pair->claim.error = ret == 0 || ret == 1 ? 0 :
                              ret < 0 ? ret : -EIO;
          pair->report = true;
        }
    }
  if (pair->scan_pending)
    {
      ret = bkprov_scan_poll(&pair->scan);
      if (ret != -EAGAIN)
        {
          pair->scan.status = ret < 0 ? ret : pair->scan.status;
          pair->scan_pending = false;
          pair->scan_report = true;
          pair->report = true;
        }
    }
  if (pair->report || pair->reported_state != pair->claim.state)
    {
      ret = report(pair);
      if (ret < 0 && ret != -EAGAIN) goto fail;
      return 0;
    }
  if (pair->claim.state >= BKPROV_COMMITTED)
    {
      /* Output was accepted by TLS/GATT, not necessarily received by phone.
       * Keep terminal state until the owner disconnects or TLS times out.
       */
      return 0;
    }
  received = bkprov_tls_read(&pair->tls,pair->input+pair->input_size,
                              pair->expected-pair->input_size);
  if (received == -EAGAIN) return 0;
  if (received < 0) { ret = (int)received; goto fail; }
  pair->input_size += received;
  if (pair->input_size < pair->expected) return 0;
  if (pair->expected == 32)
    {
      if (memcmp(pair->input,"SPV1",4) || pair->input[5] ||
          pair->input[6] || pair->input[7] || get32(pair->input+28)>1024)
        { ret = -EPROTO; goto fail; }
      pair->expected = 32+get32(pair->input+28);
      if (pair->expected != 32) return 0;
    }
  ret = packet(pair);
  mbedtls_platform_zeroize(pair->input,sizeof(pair->input));
  pair->input_size = 0; pair->expected = 32;
  if (ret < 0) goto fail;
  return 0;
fail:
  bkprov_pair_close(pair);
  return ret;
}
