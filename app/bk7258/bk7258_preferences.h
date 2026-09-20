/****************************************************************************
 * app/bk7258/bk7258_preferences.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_PREFERENCES_H
#define __APP_BK7258_BK7258_PREFERENCES_H

#include <stdbool.h>
#include <stddef.h>
#include "bk7258_cloud_config.h"

enum bk7258_persona_e
{
  BK7258_PERSONA_GENTLE = 0,
  BK7258_PERSONA_PLAYFUL,
  BK7258_PERSONA_QUIET,
  BK7258_PERSONA_SERIOUS,
  BK7258_PERSONA_TSUNDERE_LITE
};

struct bk7258_preferences_s
{
  unsigned int volume_percent;
  enum bk7258_persona_e persona;
  bool volume_is_default;
  bool persona_is_default;
};

int bk7258_preferences_get(struct bk7258_preferences_s *preferences);
/* Last confirmed volume, lazily loaded on first use. External MSC edits are
 * picked up by an explicit preferences_get refresh or after reboot.
 */
int bk7258_preferences_playback_volume(unsigned int *volume_percent);
int bk7258_preferences_set_volume(unsigned int volume_percent);
int bk7258_preferences_set_persona(const char *persona);
/* Uses the existing device configuration store; when the item is absent the
 * default is fast conversation and the old configuration is not rewritten.
 */
int bk7258_preferences_thinking_get(bool *enabled);
int bk7258_preferences_thinking_set(bool enabled);
int bk7258_preferences_wake_threshold_get(unsigned int *percent);
int bk7258_preferences_wake_threshold_set(unsigned int percent);
/* Run a bounded SD operation under the existing preference owner's mutex and
 * media lease. Callback must close every file before returning and must not
 * call preferences APIs recursively. Cleanup failure is returned to caller.
 */
int bk7258_preferences_with_storage(int (*operation)(void *), void *context);
/* Public cloud model selections are a separate atomic CP-data record.  They
 * never contain, read, or overwrite CCF1 credentials. */
int bk7258_preferences_cloud_models_get(struct bkcloud_models_s *models);
int bk7258_preferences_cloud_models_set(const struct bkcloud_models_s *models);
/* Stable names are shared by the AP store and CP command without linking
 * the CP command to a second KVDB owner.
 */

static inline const char *
bk7258_preferences_persona_name(enum bk7258_persona_e persona)
{
  switch (persona)
    {
      case BK7258_PERSONA_GENTLE:        return "gentle";
      case BK7258_PERSONA_PLAYFUL:       return "playful";
      case BK7258_PERSONA_QUIET:         return "quiet";
      case BK7258_PERSONA_SERIOUS:       return "serious";
      case BK7258_PERSONA_TSUNDERE_LITE: return "tsundere_lite";
      default:                         return NULL;
    }
}

#endif /* __APP_BK7258_BK7258_PREFERENCES_H */
