/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_QR_H
#define __APP_BK7258_PROVISION_QR_H
#include <stddef.h>
#include <stdint.h>

/* Versioned compact claim code carried on the device's first-use screen.
 *
 *   SN1:<locator-11>:<fingerprint-43>:<window-secret-43>
 *
 * Every binary field is unpadded base64url, so the whole code fits byte mode
 * QR version 5 at ECC level L with the complete 32-byte certificate SHA256 and
 * the complete 32-byte claim-window secret. No field may be truncated to fit a
 * smaller symbol: the locator locates a device, the fingerprint authenticates
 * it and the secret proves the holder is looking at this window.
 */

#define BKPROV_QR_PAYLOAD_MAX 128u

/* Encode exactly locator[8], fingerprint[32] and secret[32].
 * Returns 0 and the payload length, or a negative errno. Zero initialize the
 * output; the window secret is caller-owned and wiped by the caller.
 */
int bkprov_qr_payload(char *output, size_t capacity, size_t *size,
                      const uint8_t locator[8], const uint8_t fingerprint[32],
                      const uint8_t secret[32]);

/* Decode a claim code produced by bkprov_qr_payload(). Bounded, strict and
 * version-gated; used by host tooling and by the App-facing documentation
 * tests, never by the device's claim path.
 */
int bkprov_qr_payload_decode(const char *text, size_t size, uint8_t locator[8],
                             uint8_t fingerprint[32], uint8_t secret[32]);

/* Byte-mode QR encoder, ECC level L, versions 1..6. modules receives a
 * dimension*dimension bitmap with 1 for a dark module, dimension <= 41.
 * The caller sizes modules for BKPROV_QR_MAX_DIMENSION.
 */
#define BKPROV_QR_MAX_DIMENSION 41u
int bkprov_qr_encode(const uint8_t *payload, size_t size, uint8_t *modules,
                     size_t capacity, unsigned int *version,
                     unsigned int *dimension);

/* Copy an encoded bitmap into a square RGB565 canvas using an integer module
 * scale and a quiet zone of at least four modules. Returns 0, or -ENOSPC when
 * the requested scale cannot fit the canvas.
 */
int bkprov_qr_render_rgb565(const uint8_t *modules, unsigned int dimension,
                            uint16_t *pixels, unsigned int width,
                            unsigned int height, uint16_t dark, uint16_t light);

#endif /* __APP_BK7258_PROVISION_QR_H */
