/* SPDX-License-Identifier: Apache-2.0 */
/* The provider must observe cancel before trusted-time, DNS or sockets. */
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "bk7258_voice_tls.h"

static int trusted_time_calls;

static uint64_t now_ms(void *context)
{
  (void)context;
  return 0;
}

static int trusted_time(void *context)
{
  (void)context;
  trusted_time_calls++;
  return -EKEYREJECTED;
}

int main(void)
{
  struct bkvoice_tls_s tls;
  memset(&tls, 0, sizeof(tls));
  assert(pthread_mutex_init(&tls.crypto_lock, NULL) == 0);
  tls.initialized = true;
  tls.fd = -1;
  tls.config.now_ms = now_ms;
  tls.config.trusted_time = trusted_time;
  __atomic_store_n(&tls.interrupted, true, __ATOMIC_RELEASE);
  assert(bkvoice_tls_ops()->open_verified(&tls, "unit.invalid", 443, 100) ==
         -ECANCELED);
  assert(trusted_time_calls == 0);
  assert(bkvoice_tls_prepare_request(&tls) == 0);
  assert(bkvoice_tls_ops()->open_verified(&tls, "unit.invalid", 443, 100) ==
         -EKEYREJECTED);
  assert(trusted_time_calls == 1);
  assert(pthread_mutex_destroy(&tls.crypto_lock) == 0);
  return 0;
}
