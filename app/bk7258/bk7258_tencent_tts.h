/****************************************************************************
 * apps/bk7258/bk7258_tencent_tts.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_TENCENT_TTS_H
#define __APP_BK7258_BK7258_TENCENT_TTS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKTENCENT_TTS_HOST "tts.cloud.tencent.com"
#define BKTENCENT_TTS_PORT 443u
#define BKTENCENT_TTS_PATH_MAX 3072u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Register an inactive experiment-only provider. */

int bkagent_tencent_tts_register(void);

/* Install one short-lived signed WebSocket path and select the provider.
 * The trust record and path are copied into volatile memory. The path is
 * single-use and is never persisted or logged.
 */

int bkagent_tencent_tts_configure(const void *trust, size_t trust_size,
                                  const char *signed_path, size_t path_size);

/* Restore the provider selected before configure() and erase all Tencent
 * authorization material. Active synthesis must finish or be canceled
 * first.
 */

int bkagent_tencent_tts_clear(void);

#endif /* __APP_BK7258_BK7258_TENCENT_TTS_H */
