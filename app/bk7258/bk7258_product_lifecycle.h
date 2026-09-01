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

#ifdef CONFIG_BK7258_APP_AGENT
int bk7258_agent_product_prepare(void);
int bk7258_agent_product_start(void);
#endif

#ifdef CONFIG_BK7258_VOICE_SERVICE
int bk7258_voice_service_prepare(void);
int bk7258_voice_service_start(void);
#endif

#endif /* __APP_BK7258_BK7258_PRODUCT_LIFECYCLE_H */
