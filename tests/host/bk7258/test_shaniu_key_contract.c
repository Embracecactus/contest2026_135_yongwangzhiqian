/* SPDX-License-Identifier: Apache-2.0 */
/* User contract v2: debounced edges in one trusted session, T_off=3000ms.
 * The oracle counts outward intents; no private latch is inspected.
 */
#include "bk7258_product_keys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct bkvoice_product_keys_s keys;
static unsigned int intents;
static void edge(uint32_t epoch, uint32_t mask, uint64_t ms, bool expected)
{
  bool request = false;
  (void)bkvoice_product_keys_step(&keys, epoch, mask, ms, &request);
  assert(request == expected);
  if (request) intents++;
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  edge(1, 0, 1, false);
  if (!strncmp(argv[1], "release-", 8) && strcmp(argv[1], "release-rollback"))
    {
      unsigned long duration = strtoul(argv[1] + 8, NULL, 10);
      assert(duration >= 2999 && duration <= 3001);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 100, false);
      edge(1, 0, 100 + duration, duration >= 3000);
      edge(1, 0, 4000, false);
      assert(intents == (duration >= 3000 ? 1u : 0u));
    }
  else if (!strcmp(argv[1], "held"))
    {
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 100, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 3100, false);
      edge(1, 0, 3101, true);
      edge(1, 0, 3102, false);
      assert(intents == 1);
    }
  else if (!strcmp(argv[1], "epoch"))
    {
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 100, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 3100, false);
      edge(2, BKVOICE_PRODUCT_KEY_POWER, 3200, false);
      edge(2, 0, 3300, false);
      assert(intents == 0);
      /* A fresh press after rearming is independent of the old hold. */
      edge(2, BKVOICE_PRODUCT_KEY_POWER, 3400, false);
      edge(2, BKVOICE_PRODUCT_KEY_POWER, 6400, false);
      edge(2, 0, 6401, true);
    }
  else if (!strcmp(argv[1], "rollback"))
    {
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 5000, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 4000, false);
      edge(1, 0, 4001, false);
      assert(intents == 0);
    }
  else if (!strcmp(argv[1], "release-rollback"))
    {
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 5000, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 8000, false);
      edge(1, 0, 4000, false);
      edge(1, 0, 9000, false);
      assert(intents == 0);
    }
  else if (!strcmp(argv[1], "combination"))
    {
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 100, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 3100, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER | BKVOICE_PRODUCT_KEY_VOLUME_UP, 3200, false);
      edge(1, 0, 4000, false);
      edge(1, BKVOICE_PRODUCT_KEY_POWER, 5000, false);
      edge(1, 0, 8000, true);
      edge(1, 0, 8001, false);
      assert(intents == 1);
    }
  else if (!strcmp(argv[1], "volume"))
    {
      for (uint32_t mask = 1; mask <= 4; mask *= 4)
        {
          edge(1, mask, 100, false);
          edge(1, mask, 10100, false);
          edge(1, 0, 10101, false);
        }
      assert(intents == 0);
    }
  else return 2;
  puts("CONTRACT_PASS");
  return 0;
}
