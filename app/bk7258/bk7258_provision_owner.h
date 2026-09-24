/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_OWNER_H
#define __APP_BK7258_PROVISION_OWNER_H
#include "bk7258_provision_pair.h"
#include "bk7258_control_pair.h"

/* All calls belong to the AP voice service worker. Credential providers lend
 * validated device identity contexts until unbind succeeds; the secret is
 * copied. No network event may call bind or synthesize the physical input.
 * ops may be NULL for a read-only recovery deployment.
 */
int bkprov_owner_bind(mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                       const uint8_t secret[32],
                       const struct bkprov_claim_ops_s *ops, void *context);
int bkprov_owner_unbind(void);
/* Native screen windows replace the permanent supplied possession secret. */
int bkprov_owner_window_handler(
  int (*handler)(bool open, unsigned char secret[32], void *context),
  void *context);
bool bkprov_owner_busy(void);
/* A daily authenticated session is busy for identity replacement, but does
 * not own the physical PTT input or prevent volume/persona commands. */
bool bkprov_owner_pairing(void);
int bkprov_owner_control(const uint8_t key[32], bkcontrol_execute_t execute,
                         void *context);
/* Optional SDC1 OTA handler installed before AUTH on each control session.
 * Set only after control credentials are bound; clear automatically on
 * credential removal or owner unbind.
 */
int bkprov_owner_control_ota(bkcontrol_ota_t ota);
int bkprov_owner_control_config(bkcontrol_config_t config);
int bkprov_owner_error(void);
/* Gate new sessions and drain existing transports without changing ownership. */
int bkprov_owner_quiesce(bool enabled);
/* Power preparation: retain only an existing authenticated read/cancel
 * session and drive it once. Identity replacement still uses busy/quiesce.
 * Final power conversion must call quiesce(true); preparation never reopens
 * a transport after that final close has begun.
 */
int bkprov_owner_prepare_stop(uint64_t now_ms);
/* Feed the current owner-service snapshot. While idle, a bound identity,
 * writable claim operations, and a storage snapshot proven absent make the
 * initial claim discoverable without button, link, or epoch input. The legacy
 * 8s hold/release opens read-only receipt recovery only. A verified
 * non-recovery LOCAL state confirms automatically; every window is bounded
 * to 120s including discovery and confirmation.
 */
bool bkprov_owner_step(uint64_t now_ms, uint32_t button_epoch,
                        bool button_link, bool pressed, bool voice_idle);
#endif
