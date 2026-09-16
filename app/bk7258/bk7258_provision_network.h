/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_NETWORK_H
#define __APP_BK7258_PROVISION_NETWORK_H
#include "bk7258_provision_identity.h"
#include "bk7258_provision_claim.h"

struct bkprov_voice_ops_s
{
  bool (*available)(void *context);
  int (*load)(void *context, const void *data, size_t size);
  int (*connect)(void *context);
  int (*ready)(void *context); /* 1 verified, 0 pending, negative failure */
  int (*clear)(void *context);
  /* Optional cloud route; absent means SCB2 is rejected before Wi-Fi trial.
   * Trust/clock input remains device-owned BVC1; CCF1 contains cloud secrets.
   */
  int (*load_cloud)(void *context, const void *trust, size_t trust_size,
                    const void *cloud, size_t cloud_size);
};
/* Same AP owner as pair. Borrowed identity/voice hooks remain live until
 * unbind succeeds. A pending commit or rollback survives BLE disconnection.
 * No claim may reopen, mutate identity, or use voice while busy is true.
 */
int bkprov_network_bind(struct bkprov_identity_s *identity,
                         const struct bkprov_voice_ops_s *voice, void *context);
int bkprov_network_unbind(void);
bool bkprov_network_busy(void);
void bkprov_network_step(void);
/* Restore an already selected bundle: refresh network time before TLS, and
 * release the network lease after HELLO without publishing another revision. */
int bkprov_network_restore(const void *bundle, size_t size);
const struct bkprov_claim_ops_s *bkprov_network_ops(void);
#endif
