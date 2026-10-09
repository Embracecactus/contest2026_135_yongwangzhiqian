/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_TIME_H
#define __APP_BK7258_PROVISION_TIME_H
#include <stdint.h>
/* One process-lifetime worker runs the existing OpenVela NTP client. Calls
 * below never wait for DNS, UDP, task exit or NTP locks. This is network time,
 * not authenticated NTS. TLS certificate/date checks remain enabled.
 */
/* Fresh UTC only from a verified owner/claim transaction in this boot.
 * A persisted UTC remains merely a floor after restart. */
int bkprov_time_owner_utc(uint64_t utc);
int bkprov_time_start(void);
int bkprov_time_get(uint64_t minimum_utc, uint64_t *utc);
#endif
