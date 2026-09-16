/****************************************************************************
 * Product-key edge policy shared by the AP voice owner and host tests.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_PRODUCT_KEYS_H
#define __APP_BK7258_BK7258_PRODUCT_KEYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BKVOICE_PRODUCT_KEY_VOLUME_DOWN (1u << 0)
#define BKVOICE_PRODUCT_KEY_POWER       (1u << 1)
#define BKVOICE_PRODUCT_KEY_VOLUME_UP   (1u << 2)
#define BKVOICE_PRODUCT_KEY_VALID        (BKVOICE_PRODUCT_KEY_VOLUME_DOWN | \
                                          BKVOICE_PRODUCT_KEY_POWER | \
                                          BKVOICE_PRODUCT_KEY_VOLUME_UP)

struct bkvoice_product_keys_s
{
  uint32_t owner_epoch;
  uint32_t mask;
  uint64_t power_since_ms;
  bool armed;
  bool power_request_latched;
};

static inline void bkvoice_product_keys_reset(
  struct bkvoice_product_keys_s *keys, uint32_t owner_epoch)
{
  keys->owner_epoch = owner_epoch;
  keys->mask = 0;
  keys->power_since_ms = 0;
  keys->armed = false;
  keys->power_request_latched = false;
}

/* A reconnect is unarmed: only a subsequently observed all-released sample
 * arms it.  This prevents a held key and retained heartbeat from becoming a
 * new action.  A power request is qualified by a continuous, uncombined K2
 * hold, then delivered only by its all-released sample.  action is a
 * rising-edge mask; simultaneous volume +/- is returned together for the
 * caller to reject.
 */
static inline uint32_t bkvoice_product_keys_step(
  struct bkvoice_product_keys_s *keys, uint32_t owner_epoch, uint32_t mask,
  uint64_t now_ms,
  bool *power_requested)
{
  uint32_t action = 0;

  if (power_requested != NULL)
    {
      *power_requested = false;
    }

  if (keys->owner_epoch != owner_epoch)
    {
      bkvoice_product_keys_reset(keys, owner_epoch);
    }

  if (!keys->armed)
    {
      keys->mask = mask;
      if (mask == 0)
        {
          keys->armed = true;
        }
      return 0;
    }

  action = mask & ~keys->mask;

  if (mask == 0)
    {
      if (keys->power_request_latched &&
          keys->mask == BKVOICE_PRODUCT_KEY_POWER &&
          power_requested != NULL)
        {
          *power_requested = true;
        }

      keys->power_since_ms = 0;
      keys->power_request_latched = false;
    }
  else if (mask != BKVOICE_PRODUCT_KEY_POWER)
    {
      /* Any simultaneous K1/K3 combination cancels this K2 hold. */

      keys->power_since_ms = 0;
      keys->power_request_latched = false;
    }
  else if (keys->mask != BKVOICE_PRODUCT_KEY_POWER)
    {
      keys->power_since_ms = now_ms;
    }
  else if (now_ms < keys->power_since_ms)
    {
      /* Do not turn a clock rollback into an immediately expired hold. */

      keys->power_since_ms = now_ms;
      keys->power_request_latched = false;
    }
  else if (!keys->power_request_latched &&
           now_ms - keys->power_since_ms >= 3000u)
    {
      keys->power_request_latched = true;
    }

  keys->mask = mask;

  return action;
}

#endif /* __APP_BK7258_BK7258_PRODUCT_KEYS_H */
