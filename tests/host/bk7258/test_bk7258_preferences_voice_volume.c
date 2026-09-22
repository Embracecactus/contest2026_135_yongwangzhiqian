/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <nuttx/mutex.h>
#include "bk7258_preferences.h"

static unsigned int stored = 61;
static int store_error, media_error;
int nxmutex_lock(mutex_t *m) {return -pthread_mutex_lock(m);}
int nxmutex_unlock(mutex_t *m) {return -pthread_mutex_unlock(m);}
int bkvoice_volume_store_get(unsigned int *value)
{if (store_error)return store_error;*value = stored;return 0;}
int bkvoice_volume_store_set(unsigned int value)
{if (store_error)return store_error;stored = value;return 0;}
int bk7258_preferences_storage_begin(void) {return media_error;}
int bk7258_preferences_storage_end(int ret) {return ret;}
int property_get_with_err(const char *key, char *value)
{
  /* A stale KVDB volume must never be read in the device-volume profile. */
  assert(!strcmp(key, "persist.shaniu.persona"));
  strcpy(value, "quiet");return 5;
}
int property_set(const char *key, const char *value)
{assert(!strcmp(key, "persist.shaniu.persona"));(void)value;return 0;}
int property_delete(const char *key) {(void)key;return 0;}
int property_commit(void) {return 0;}
int main(void)
{
  struct bk7258_preferences_s p;
  unsigned int volume;
  assert(bk7258_preferences_get(&p) == 0 && p.volume_percent == 61);
  assert(bk7258_preferences_set_volume(73) == 0 && stored == 73);
  assert(bk7258_preferences_get(&p) == 0 && p.volume_percent == 73);
  stored = 24; /* A change through the playback control path. */
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 24);
  media_error = -EBUSY;
  assert(bk7258_preferences_set_volume(42) == 0 && stored == 42);
  assert(bk7258_preferences_playback_volume(&volume) == 0 && volume == 42);
  assert(bk7258_preferences_get(&p) == -EBUSY);
  store_error = -EIO;
  assert(bk7258_preferences_set_volume(77) == -EIO && stored == 42);
  assert(bk7258_preferences_playback_volume(&volume) == -EIO);
  assert(bk7258_preferences_set_volume(101) == -ERANGE);
  assert(bk7258_preferences_playback_volume(NULL) == -EINVAL);
  media_error = 0;store_error = -ENOENT;
  assert(bk7258_preferences_get(&p) == 0 && p.volume_percent == 50 && p.volume_is_default);
  puts("PASS: one device volume store, external update, SD contention, persistence failures");
  return 0;
}
