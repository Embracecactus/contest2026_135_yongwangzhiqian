/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_TRIGGER_MODEL_H
#define __APP_BK7258_VOICE_TRIGGER_MODEL_H

#include "bk7258_voice_wake_window.h"
#include <stdint.h>

int bkvoice_trigger_model_bind(struct bkvoice_wake_window_s *window,
                               uint64_t start_ms);
int bkvoice_trigger_model_unbind(void);
float bkvoice_trigger_model_score(void);

#endif
