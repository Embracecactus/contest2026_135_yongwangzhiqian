/****************************************************************************
 * app/bk7258/bk7258_product_lifecycle.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Product-side contributors to the generic BK7258 AP lifecycle.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_PRODUCT_LIFECYCLE_H
#define __APP_BK7258_BK7258_PRODUCT_LIFECYCLE_H

#include <nuttx/config.h>

#ifdef CONFIG_TESTS_BK7258_RPMSG_EXEC
int bk7258_drivercheck_initialize(void);
#endif

#ifdef CONFIG_BK7258_APP_AGENT
int bk7258_agent_product_prepare(void);
int bk7258_agent_product_start(void);
#endif

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
int bk7258_display_service_prepare(void);
int bk7258_display_service_start(void);
#endif

#ifdef CONFIG_BK7258_HEALTH_SERVICE
int bk7258_health_service_prepare(void);
int bk7258_health_service_start(void);
#endif

#ifdef CONFIG_BK7258_MOTION_SERVICE
int bk7258_motion_service_prepare(void);
int bk7258_motion_service_start(void);
#endif

#ifdef CONFIG_BK7258_NFC_SERVICE
int bk7258_nfc_service_prepare(void);
int bk7258_nfc_service_start(void);
#endif

#ifdef CONFIG_BK7258_VISION_SERVICE
int bk7258_vision_service_prepare(void);
int bk7258_vision_service_start(void);
#endif

#endif /* __APP_BK7258_BK7258_PRODUCT_LIFECYCLE_H */
