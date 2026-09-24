#include <assert.h>
#include <errno.h>
#include "bk7258_product_keys.h"

/* A release edge must not depend on receiving a periodic held sample at
 * exactly the qualification boundary. This is a product contract, not a
 * physical power-off test.
 */

static void test_release_boundary_without_held_heartbeat(void)
{
  struct bkvoice_product_keys_s keys = {0};
  bool requested = false;

  bkvoice_product_keys_step(&keys, 9, 0, 1, &requested);
  bkvoice_product_keys_step(&keys, 9, BKVOICE_PRODUCT_KEY_POWER, 100,
                            &requested);
  bkvoice_product_keys_step(&keys, 9, 0, 3099, &requested);
  assert(!requested);
  bkvoice_product_keys_step(&keys, 9, BKVOICE_PRODUCT_KEY_POWER, 4000,
                            &requested);
  bkvoice_product_keys_step(&keys, 9, 0, 7000, &requested);
  assert(requested);
  bkvoice_product_keys_step(&keys, 9, 0, 7001, &requested);
  assert(!requested);
}

static void test_power_is_one_shot_and_volume_cannot_qualify_it(void)
{
  struct bkvoice_product_keys_s keys = {0};
  bool requested = false;

  bkvoice_product_keys_step(&keys, 7, 0, 1, &requested);
  bkvoice_product_keys_step(&keys, 7, BKVOICE_PRODUCT_KEY_VOLUME_DOWN,
                            100, &requested);
  bkvoice_product_keys_step(&keys, 7, BKVOICE_PRODUCT_KEY_VOLUME_DOWN,
                            6100, &requested);
  bkvoice_product_keys_step(&keys, 7, 0, 6101, &requested);
  assert(!requested);
  bkvoice_product_keys_step(&keys, 7, BKVOICE_PRODUCT_KEY_POWER, 7000,
                            &requested);
  bkvoice_product_keys_step(&keys, 7, BKVOICE_PRODUCT_KEY_POWER, 9999,
                            &requested);
  assert(!requested && !keys.power_request_latched);
  bkvoice_product_keys_step(&keys, 7, BKVOICE_PRODUCT_KEY_POWER, 10000,
                            &requested);
  assert(!requested && keys.power_request_latched);
  bkvoice_product_keys_step(&keys, 7, 0, 10001, &requested);
  assert(requested);
  bkvoice_product_keys_step(&keys, 7, 0, 10002, &requested);
  assert(!requested);
}

int main(void)
{
  struct bkvoice_product_keys_s keys = {0};
  bool power_requested;

  /* Held data after reconnect does not arm or create a volume action. */
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_VOLUME_UP,
                                   1, &power_requested) == 0);
  assert(!keys.armed);
  assert(bkvoice_product_keys_step(&keys, 1, 0, 2, &power_requested) == 0);
  assert(keys.armed);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_VOLUME_UP,
                                   3, &power_requested) ==
         BKVOICE_PRODUCT_KEY_VOLUME_UP);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_VOLUME_UP,
                                   4, &power_requested) == 0);
  assert(bkvoice_product_keys_step(&keys, 1, 0, 5, &power_requested) == 0);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_VOLUME_DOWN |
                                   BKVOICE_PRODUCT_KEY_VOLUME_UP, 6,
                                   &power_requested) ==
         (BKVOICE_PRODUCT_KEY_VOLUME_DOWN | BKVOICE_PRODUCT_KEY_VOLUME_UP));
  assert(bkvoice_product_keys_step(&keys, 1, 0, 7, &power_requested) == 0);

  /* A short K2 press and an uncombined long K2 hold have different results. */
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 8,
                                   &power_requested) == BKVOICE_PRODUCT_KEY_POWER);
  assert(!power_requested);
  assert(bkvoice_product_keys_step(&keys, 1, 0, 9, &power_requested) == 0);
  assert(!power_requested);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 10,
                                   &power_requested) == BKVOICE_PRODUCT_KEY_POWER);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 3010,
                                   &power_requested) == 0);
  assert(!power_requested);
  assert(keys.power_request_latched);
  assert(bkvoice_product_keys_step(&keys, 1, 0, 3011, &power_requested) == 0);
  assert(power_requested);
  assert(!keys.power_request_latched);

  /* A combination cancels even an already-qualified K2 request. */
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 3020,
                                   &power_requested) == BKVOICE_PRODUCT_KEY_POWER);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 6020,
                                   &power_requested) == 0);
  assert(keys.power_request_latched);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER |
                                   BKVOICE_PRODUCT_KEY_VOLUME_UP, 6021,
                                   &power_requested) == BKVOICE_PRODUCT_KEY_VOLUME_UP);
  assert(bkvoice_product_keys_step(&keys, 1, 0, 6022, &power_requested) == 0);
  assert(!power_requested);

  /* A rollback restarts the hold timer instead of expiring it spuriously. */
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 7000,
                                   &power_requested) == BKVOICE_PRODUCT_KEY_POWER);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 6000,
                                   &power_requested) == 0);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 8999,
                                   &power_requested) == 0);
  assert(!keys.power_request_latched);
  assert(bkvoice_product_keys_step(&keys, 1, BKVOICE_PRODUCT_KEY_POWER, 9000,
                                   &power_requested) == 0);
  assert(keys.power_request_latched);

  /* A reconnect discards a latched request and requires release to re-arm. */
  assert(bkvoice_product_keys_step(&keys, 2, BKVOICE_PRODUCT_KEY_VOLUME_UP,
                                   9001, &power_requested) == 0);
  assert(!keys.armed);
  assert(!keys.power_request_latched);
  /* A rollback after qualification also cancels the retained request. */
  bkvoice_product_keys_reset(&keys, 3);
  bkvoice_product_keys_step(&keys, 3, 0, 1, &power_requested);
  bkvoice_product_keys_step(&keys, 3, BKVOICE_PRODUCT_KEY_POWER, 100,
                            &power_requested);
  bkvoice_product_keys_step(&keys, 3, BKVOICE_PRODUCT_KEY_POWER, 3100,
                            &power_requested);
  assert(keys.power_request_latched);
  bkvoice_product_keys_step(&keys, 3, BKVOICE_PRODUCT_KEY_POWER, 50,
                            &power_requested);
  bkvoice_product_keys_step(&keys, 3, 0, 51, &power_requested);
  assert(!power_requested);

  test_power_is_one_shot_and_volume_cannot_qualify_it();
  test_release_boundary_without_held_heartbeat();
  return 0;
}
