/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_MEDIA_H
#define __APP_BK7258_VOICE_MEDIA_H
#include <stdbool.h>
int bkvoice_media_start(void);
int bkvoice_media_volume(bool apply, unsigned int requested, unsigned int *volume);
/* Called only by the serialized product audio owner after preparing a public
 * recorder, or after stopping it. Board policy maps the source to its route. */
int bkvoice_media_source_set_active(const char *source, bool active);
#endif
