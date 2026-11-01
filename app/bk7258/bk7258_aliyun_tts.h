/****************************************************************************
 * apps/bk7258/bk7258_aliyun_tts.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_ALIYUN_TTS_H
#define __APP_BK7258_BK7258_ALIYUN_TTS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKALIYUN_TTS_HOST \
  "llm-d1hvy2bes12kocx6.cn-beijing.maas.aliyuncs.com"
#define BKALIYUN_TTS_PORT 443u
#define BKALIYUN_TTS_PATH "/api-ws/v1/inference"
#define BKALIYUN_TTS_API_KEY_MAX 256u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Register an inactive experiment-only provider. */

int bkagent_aliyun_tts_register(void);

/* Select the provider using one API key delivered through authenticated
 * BKTEST. Trust and key bytes are copied only into volatile memory; the key
 * is erased immediately after the WebSocket upgrade succeeds.
 */

int bkagent_aliyun_tts_configure(const void *trust, size_t trust_size,
                                 const char *api_key, size_t key_size,
                                 bool preconnect);

/* The selected experimental provider reuses its sole receiver for setup.
 * These hooks never submit text or acquire Media. Cancellation is
 * nonblocking.
 */

int bkagent_aliyun_tts_preconnect(uint64_t request);
void bkagent_aliyun_tts_preconnect_cancel(uint64_t request);

/* Restore the previous provider and erase all volatile authorization bytes.
 */

int bkagent_aliyun_tts_clear(void);

#endif /* __APP_BK7258_BK7258_ALIYUN_TTS_H */
