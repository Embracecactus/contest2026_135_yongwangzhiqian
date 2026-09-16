/****************************************************************************
 * app/bk7258/bk7258_voice_volume_store.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_VOLUME_STORE_H
#define __APP_BK7258_BK7258_VOICE_VOLUME_STORE_H

#define BKVOICE_VOLUME_STORE_ROOT "/cpdata/shaniu/voice-volume"

/* Configure one AP-owned, device-local volume store.  The implementation
 * opens the CP LittleFS path lazily so product startup never waits for
 * RPMsgFS.  It neither mounts nor formats storage.
 */

int bkvoice_volume_store_start(const char *root);
int bkvoice_volume_store_get(unsigned int *volume_percent);
int bkvoice_volume_store_set(unsigned int volume_percent);

/* Drop only the in-memory snapshot and reconcile it from the active file on
 * the next access.  This is also the recovery path after an uncertain rename.
 */

int bkvoice_volume_store_reload(void);

#endif /* __APP_BK7258_BK7258_VOICE_VOLUME_STORE_H */
