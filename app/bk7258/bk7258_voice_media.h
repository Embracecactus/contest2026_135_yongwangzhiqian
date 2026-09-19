/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_MEDIA_H
#define __APP_BK7258_VOICE_MEDIA_H
#include <stdbool.h>
int bkvoice_media_start(void);
int bkvoice_media_volume(bool apply, unsigned int requested, unsigned int *volume);
int bkvoice_media_volume_step(int steps, unsigned int *volume);
/* Stage the route without starting the capture device. Trigger uses this
 * before its public recorder has established the actual input format. */
int bkvoice_media_source_stage_active(const char *source);
/* Apply a previously staged capture route after the recorder is ready. */
int bkvoice_media_source_apply_active(void);
/* Called only by the serialized product audio owner after preparing a public
 * recorder, or after stopping it. Board policy maps the source to its route. */
int bkvoice_media_source_set_active(const char *source, bool active);
#endif
