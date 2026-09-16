/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_GATT_H
#define __APP_BK7258_PROVISION_GATT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Register the complete product table only while Host is idle, before product
 * advertising. No advertising or window opens here. The serialized product
 * owner opens a window only after local confirmation and credential readiness.
 */
int bkprov_gatt_register(void);
/* These two functions are serialized worker calls, never Host callbacks.
 * Poll throughout a window, including before subscription and after closing,
 * until a pending physical disconnection completes. Window timeout and local
 * presence/credential checks are the product owner's responsibility.
 */
#define BKPROV_GATT_LOCATOR_SIZE 8u
/* Ephemeral discovery hint for the NFC worker. Available only while an
 * unconnected physical-presence window advertises. Never an owner credential.
 * Failure clears the supplied buffer.
 */
int bkprov_gatt_locator(uint8_t locator[BKPROV_GATT_LOCATOR_SIZE]);
int bkprov_gatt_window(bool open);
int bkprov_gatt_poll(void);
bool bkprov_gatt_open(void);
bool bkprov_gatt_idle(void);
uint32_t bkprov_gatt_generation(void);
ssize_t bkprov_gatt_read(uint32_t generation, void *data, size_t size);
/* Send at most 20 ciphertext bytes on the retained connection. Positive bytes
 * mean passed to L2CAP, not peer receipt. Worker must pace sends and bound
 * retries of -ENOMEM. Never call this from ATT/CCC or under a product lock.
 */
ssize_t bkprov_gatt_send(uint32_t generation, const void *data, size_t size);
#endif
