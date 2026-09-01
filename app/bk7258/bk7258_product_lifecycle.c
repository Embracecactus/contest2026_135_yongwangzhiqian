/****************************************************************************
 * app/bk7258/bk7258_product_lifecycle.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Single product-owned implementation of the chip AP lifecycle extension.
 * Individual applications contribute named prepare/start functions so that
 * adding a second AP product service cannot create duplicate chip callbacks.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_APPLICATION_LIFECYCLE

#include <errno.h>

#include "bk7258_product_lifecycle.h"

int bk7258_ap_application_prepare(void)
{
  int ret;

#ifdef CONFIG_BK7258_APP_AGENT
  ret = bk7258_agent_product_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_VOICE_SERVICE
  ret = bk7258_voice_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

  return 0;
}

int bk7258_ap_application_start(void)
{
  int ret;

#ifdef CONFIG_BK7258_VOICE_SERVICE
  ret = bk7258_voice_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_APP_AGENT
  ret = bk7258_agent_product_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

  return 0;
}

#endif /* CONFIG_BK7258_AP_APPLICATION_LIFECYCLE */
