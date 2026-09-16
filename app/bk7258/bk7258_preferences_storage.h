/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PREFERENCES_STORAGE_H
#define __APP_BK7258_PREFERENCES_STORAGE_H

/* The preference owner must serialize begin through end, including retained
 * cleanup state. Call end only after successful begin, once all DIRECT KVDB
 * handles have closed. No formatting is done.
 */
int bk7258_preferences_storage_begin(void);
int bk7258_preferences_storage_end(int status);
#endif
