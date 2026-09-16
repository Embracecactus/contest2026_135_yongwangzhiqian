/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_claim.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/constant_time.h>
#include <mbedtls/platform_util.h>

static void clear_candidate(struct bkprov_claim_s *claim)
{
  mbedtls_platform_zeroize(claim->secret, sizeof(claim->secret));
  mbedtls_platform_zeroize(claim->bundle, sizeof(claim->bundle));
  claim->size = claim->received = 0;
}

static int fail(struct bkprov_claim_s *claim, int error)
{
  if (claim->commit_pending)
    {
      /* An in-flight durable publication cannot be cancelled by disconnect.
       * The storage owner retains its copy and reconciles the receipt later.
       */
      claim->trial = false;
      claim->commit_pending = false;
      clear_candidate(claim);
      claim->state = BKPROV_UNCERTAIN;
      claim->error = error;
      return error;
    }
  if (claim->trial)
    {
      claim->ops->abort(claim->context);
      claim->trial = false;
    }
  clear_candidate(claim);
  claim->state = BKPROV_FAILED;
  claim->error = error;
  return error;
}

void bkprov_claim_close(struct bkprov_claim_s *claim)
{
  if (claim == NULL)
    {
      return;
    }
  if (claim->trial)
    {
      claim->ops->abort(claim->context);
    }
  mbedtls_platform_zeroize(claim, sizeof(*claim));
}

int bkprov_claim_open(struct bkprov_claim_s *claim, uint32_t generation,
                      const uint8_t secret[32], bool local_action,
                      bool already_claimed, uint64_t now_ms,
                      const struct bkprov_claim_ops_s *ops, void *context)
{
  static const uint8_t zero[32];
  if (claim == NULL || secret == NULL || generation == 0 || ops == NULL ||
      ops->begin == NULL || ops->poll == NULL || ops->commit == NULL ||
      ops->abort == NULL)
    {
      return -EINVAL;
    }
  if (claim->state != BKPROV_CLOSED)
    {
      return -EBUSY;
    }
  if (!local_action || already_claimed)
    {
      return -EACCES;
    }
  if (mbedtls_ct_memcmp(secret, zero, sizeof(zero)) == 0)
    {
      return -EINVAL;
    }
  claim->ops = ops;
  claim->context = context;
  claim->generation = generation;
  claim->opened_ms = claim->last_ms = now_ms;
  memcpy(claim->secret, secret, sizeof(claim->secret));
  claim->state = BKPROV_AUTH;
  return 0;
}

int bkprov_claim_auth(struct bkprov_claim_s *claim, uint32_t generation,
                      const uint8_t transaction[16], const uint8_t proof[32])
{
  static const uint8_t zero[16];
  if (claim == NULL || transaction == NULL || proof == NULL)
    {
      return -EINVAL;
    }
  if (claim->state != BKPROV_AUTH || generation != claim->generation)
    {
      return -EACCES;
    }
  if (mbedtls_ct_memcmp(claim->secret, proof, 32) != 0 ||
      mbedtls_ct_memcmp(transaction, zero, 16) == 0)
    {
      return fail(claim, -EACCES);
    }
  memcpy(claim->transaction, transaction, 16);
  mbedtls_platform_zeroize(claim->secret, sizeof(claim->secret));
  claim->state = BKPROV_LOCAL;
  return 0;
}

int bkprov_claim_confirm(struct bkprov_claim_s *claim, uint32_t generation)
{
  if (claim == NULL || claim->state != BKPROV_LOCAL ||
      generation != claim->generation)
    {
      return -EACCES;
    }
  claim->state = BKPROV_READY;
  return 0;
}

static bool next(const struct bkprov_claim_s *claim, uint32_t sequence)
{
  return sequence != 0 && sequence != UINT32_MAX &&
         sequence == claim->sequence + 1;
}

int bkprov_claim_begin(struct bkprov_claim_s *claim, uint32_t sequence,
                       size_t size)
{
  if (claim == NULL || claim->state != BKPROV_READY)
    {
      return -EACCES;
    }
  if (!next(claim, sequence) || size == 0 || size > BKPROV_BUNDLE_MAX)
    {
      return fail(claim, -EPROTO);
    }
  claim->size = size;
  claim->sequence = sequence;
  claim->state = BKPROV_RECEIVING;
  return 0;
}

int bkprov_claim_data(struct bkprov_claim_s *claim, uint32_t sequence,
                      size_t offset, const void *data, size_t size)
{
  if (claim == NULL || claim->state != BKPROV_RECEIVING)
    {
      return -EACCES;
    }
  if (!next(claim, sequence) || data == NULL || size == 0 ||
      size > BKPROV_CHUNK_MAX || offset != claim->received ||
      size > claim->size - claim->received)
    {
      return fail(claim, -EPROTO);
    }
  memcpy(claim->bundle + offset, data, size);
  claim->received += size;
  claim->sequence = sequence;
  return 0;
}

int bkprov_claim_apply(struct bkprov_claim_s *claim, uint32_t sequence)
{
  int ret;
  if (claim == NULL || claim->state != BKPROV_RECEIVING)
    {
      return -EACCES;
    }
  if (!next(claim, sequence) || claim->received != claim->size)
    {
      return fail(claim, -EPROTO);
    }
  claim->sequence = sequence;
  /* begin may change runtime before returning an error; abort owns recovery. */
  claim->trial = true;
  ret = claim->ops->begin(claim->context, claim->bundle, claim->size);
  if (ret != 0)
    {
      return fail(claim, ret < 0 ? ret : -EIO);
    }
  claim->state = BKPROV_CHECKING;
  return 0;
}

int bkprov_claim_step(struct bkprov_claim_s *claim, uint32_t generation,
                      uint64_t now_ms)
{
  int ret;
  if (claim == NULL || claim->state == BKPROV_CLOSED)
    {
      return -ENOTCONN;
    }
  if (claim->state >= BKPROV_COMMITTED)
    {
      return claim->error;
    }
  if (generation != claim->generation)
    {
      return fail(claim, -ESTALE);
    }
  if (now_ms < claim->last_ms || now_ms - claim->opened_ms >= 120000)
    {
      return fail(claim, -ETIMEDOUT);
    }
  claim->last_ms = now_ms;
  if (claim->state != BKPROV_CHECKING)
    {
      return 0;
    }
  if (!claim->commit_pending)
    {
      ret = claim->ops->poll(claim->context);
      if (ret == 0) return 0;
      if (ret != 1) return fail(claim, ret < 0 ? ret : -EIO);
    }
  ret = claim->ops->commit(claim->context, claim->transaction,
                            claim->bundle, claim->size);
  if (ret == -EAGAIN)
    {
      claim->commit_pending = true;
      claim->trial = false;
      return 0;
    }
  claim->commit_pending = false;
  if (ret == -EINPROGRESS)
    {
      /* Do not run rollback over an indeterminate durable publication. The
       * storage owner reconciles selected configuration before future use.
       */
      claim->trial = false;
      claim->state = BKPROV_UNCERTAIN;
      claim->error = ret;
      clear_candidate(claim);
      return ret;
    }
  if (ret != 0)
    {
      claim->trial = true;
      return fail(claim, ret < 0 ? ret : -EIO);
    }
  claim->trial = false;
  claim->state = BKPROV_COMMITTED;
  clear_candidate(claim);
  return 0;
}
