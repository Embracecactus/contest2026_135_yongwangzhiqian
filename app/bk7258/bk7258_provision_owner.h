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
/* Versioned claim code for the open initial-use window, "SN1:" followed by the
 * unpadded base64url locator, certificate SHA256 and current window secret.
 * -ENOENT when no unconnected initial window is open, -EAGAIN while its BLE
 * locator is not yet available. The secret belongs to this window only; a new
 * window always mints a new one and the code is never reused across windows.
 */
int bkprov_owner_claim_code(char *output, size_t capacity, size_t *size);
/* The 8-byte BLE locator of the open window; discovery hint only. */
int bkprov_owner_locator(uint8_t locator[8]);
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
