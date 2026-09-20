/****************************************************************************
 * app/bk7258/bk7258_prov_service.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_PROV_SERVICE_H
#define __APP_BK7258_BK7258_PROV_SERVICE_H

/* Start the AP-owned identity-supply endpoint.  Registration performs no
 * filesystem operation; an install still fails closed until the product
 * storage worker owns the on-chip LittleFS.
 */

int bkprov_service_initialize(void);

#endif /* __APP_BK7258_BK7258_PROV_SERVICE_H */
