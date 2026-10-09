/* SPDX-License-Identifier: Apache-2.0 */
/* Real AP key receiver; RPMsg transport and monotonic clock are peers. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/rpmsg/rpmsg.h>

#include "bk7258_voice_button.h"

static uint64_t now_ms;
static rpmsg_ept_cb_t receive_cb;

uint64_t bkvoice_config_now_ms(void *unused)
{
  (void)unused;
  return now_ms;
}

const char *rpmsg_get_cpuname(struct rpmsg_device *device)
{
  return device == NULL ? NULL : device->cpuname;
}

int rpmsg_create_ept(struct rpmsg_endpoint *endpoint,
                     struct rpmsg_device *device, const char *name,
                     uint32_t source, uint32_t destination,
                     rpmsg_ept_cb_t callback,
                     rpmsg_ns_unbind_cb_t unbind_callback)
{
  (void)source;
  endpoint->rdev = device;
  endpoint->dest_addr = destination;
  endpoint->ns_bound_cb = NULL;
  strncpy(endpoint->name, name, sizeof(endpoint->name) - 1);
  receive_cb = callback;
  (void)unbind_callback;
  return 0;
}

void rpmsg_destroy_ept(struct rpmsg_endpoint *endpoint)
{
  endpoint->rdev = NULL;
  receive_cb = NULL;
}

int rpmsg_register_callback(void *priv, rpmsg_dev_cb_t created,
                            rpmsg_dev_cb_t destroyed,
                            rpmsg_match_cb_t match, rpmsg_bind_cb_t bind)
{
  (void)priv;
  (void)created;
  (void)destroyed;
  (void)match;
  (void)bind;
  return 0;
}

void rpmsg_unregister_callback(void *priv, rpmsg_dev_cb_t created,
                               rpmsg_dev_cb_t destroyed,
                               rpmsg_match_cb_t match, rpmsg_bind_cb_t bind)
{
  (void)priv;
  (void)created;
  (void)destroyed;
  (void)match;
  (void)bind;
}

#include "bk7258_agent_keys.c"

static struct rpmsg_device cp = { .cpuname = "cp" };
static uint32_t sequence;

static void connect_keys(void)
{
  keys_bind(&cp, NULL, BKVOICE_BUTTON_ENDPOINT, 1);
  assert(receive_cb != NULL);
}

static void sample(uint32_t pressed, uint64_t at_ms)
{
  struct bkvoice_button_event_s event =
  {
    .magic = BKVOICE_BUTTON_MAGIC,
    .version = BKVOICE_BUTTON_VERSION,
    .sequence = ++sequence,
    .pressed = pressed,
  };

  now_ms = at_ms;
  assert(receive_cb(&g_keys.endpoint, &event, sizeof(event), 0, NULL) == 0);
}

static void complete_power_release(void)
{
  sample(0, 1); /* A fresh link must first observe all keys released. */
  sample(BKVOICE_PRODUCT_KEY_POWER, 100);
  for (uint64_t at = 500; at <= 2900; at += 400)
    {
      sample(BKVOICE_PRODUCT_KEY_POWER, at);
    }

  /* The release itself reaches the boundary; no held sample exists there. */
  sample(0, 3100);
}

int main(int argc, char **argv)
{
  int volume;
  bool power;

  assert(argc == 2);
  connect_keys();
  if (!strcmp(argv[1], "accepted-disconnect"))
    {
      complete_power_release();
      keys_device_destroy(&cp, NULL);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && power);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && !power);
    }
  else if (!strcmp(argv[1], "unfinished-disconnect"))
    {
      sample(0, 1);
      sample(BKVOICE_PRODUCT_KEY_POWER, 100);
      for (uint64_t at = 500; at <= 2900; at += 400)
        {
          sample(BKVOICE_PRODUCT_KEY_POWER, at);
        }

      keys_device_destroy(&cp, NULL);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && !power);

      /* A held key on a new session cannot inherit the old hold. */
      connect_keys();
      sample(BKVOICE_PRODUCT_KEY_POWER, 3300);
      sample(0, 6400);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && !power);
    }
#ifdef CONFIG_BK7258_ENGINEERING_TEST
  else if (!strcmp(argv[1], "engineering-no-held"))
    {
      bool accepted = false;
      /* This source starts after the physical debounce/RPMsg boundary. The
       * same product policy must qualify release without a held heartbeat.
       */
      assert(bkvoice_keys_engineering_begin(17, 100) == 0);
      assert(bkvoice_keys_engineering_event(17, 1,
        BKVOICE_PRODUCT_KEY_POWER, 100, &accepted) == 0 && !accepted);
      assert(bkvoice_keys_engineering_event(17, 2, 0, 3100,
                                             &accepted) == 0 && accepted);
      assert(bkvoice_keys_engineering_end(17) == 0);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && power);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && !power);
    }
  else if (!strcmp(argv[1], "engineering-sequence"))
    {
      bool accepted = false;
      assert(bkvoice_keys_engineering_begin(23, 100) == 0);
      assert(bkvoice_keys_engineering_event(23, 1,
        BKVOICE_PRODUCT_KEY_POWER, 100, &accepted) == 0 && !accepted);
      assert(bkvoice_keys_engineering_event(23, 1, 0, 3100,
                                             &accepted) == -ESTALE);
      assert(bkvoice_keys_engineering_event(24, 2, 0, 3100,
                                             &accepted) == -ESTALE);
      assert(bkvoice_keys_engineering_end(23) == 0);
      assert(bkvoice_keys_engineering_event(23, 2, 0, 3100,
                                             &accepted) == -ESTALE);
      bkvoice_keys_take(&volume, &power);
      assert(volume == 0 && !power);
    }
  else if (!strcmp(argv[1], "engineering-pending-intent"))
    {
      bool accepted = false;
      assert(bkvoice_keys_engineering_begin(31, 100) == 0);
      assert(bkvoice_keys_engineering_event(31, 1,
        BKVOICE_PRODUCT_KEY_POWER, 100, &accepted) == 0 && !accepted);
      assert(bkvoice_keys_engineering_event(31, 2, 0, 3100,
                                             &accepted) == 0 && accepted);
      assert(bkvoice_keys_engineering_end(31) == 0);

      /* The accepted release belongs to the coordinator. A new engineering
       * source cannot merge another session into that unconsumed intent.
       */
      assert(bkvoice_keys_engineering_begin(32, 3200) == -EBUSY);
      bkvoice_keys_take(&volume, &accepted);
      assert(volume == 0 && accepted);
      assert(bkvoice_keys_engineering_begin(32, 3200) == 0);
      assert(bkvoice_keys_engineering_end(32) == 0);
    }
#endif
  else
    {
      return 2;
    }

  puts("CONTRACT_PASS");
  return 0;
}
