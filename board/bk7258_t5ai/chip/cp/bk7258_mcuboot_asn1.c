/* Board build bridge for the upstream minimal ASN.1 parser used to decode
 * the standard DER P-256 public key and signature.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../../../../../apps/boot/mcuboot/mcuboot/ext/mbedtls-asn1/src/asn1parse.c"
#include "../../../../../apps/boot/mcuboot/mcuboot/ext/mbedtls-asn1/src/platform_util.c"
