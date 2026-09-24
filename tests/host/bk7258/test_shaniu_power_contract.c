/* SPDX-License-Identifier: Apache-2.0 */
/* The product includes the same implementation. Only resource participants,
 * key input, and CP transport are peers; the coordinator is never mocked. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "bk7258_media_volume.h"
#define CONFIG_BK7258_PRODUCT_KEYS 1
#define CONFIG_BK7258_PM_SOFT_OFF 1
#define CONFIG_BK7258_VISION_SERVICE 1
#define CONFIG_BK7258_HAPTIC_SERVICE 1
#define CONFIG_BK7258_DISPLAY_SERVICE 1
#define CONFIG_BK7258_PROVISION_NATIVE 1
static bool g_trigger_started = true;
static int g_product_error;
static atomic_bool g_trigger_prepare_pending;
static atomic_bool g_voice_initialized = true;
static atomic_bool g_probe_running;
static bool key_power = true;
static int owner_error, storage_error, cp_error, cp_status = 1;
static unsigned int cp_calls, storage_stops, reopens, cancel_calls;
static unsigned int trigger_stops;
static int trigger_error;
static bool trigger_closed;
static bool leased, owner_closed, storage_closed, vision_closed, haptic_closed;
static uint64_t now;
static void bkvoice_keys_take(int *steps, bool *power)
{ *steps = 0; *power = key_power; key_power = false; }
static bool bkvoice_keys_power_held(void) { return false; }
static int bk7258_display_power(int mode) { (void)mode; return 0; }
static bool voice_channel_is_idle(void) { return true; }
static void voice_channel_cancel(void) { cancel_calls++; }
static int voice_channel_recover(void) { return 0; }
static bool bkprov_owner_busy(void) { return false; }
static bool bkprov_network_busy(void) { return false; }
static bool bkprov_config_busy(void) { return false; }
static bool bkprov_scan_busy(void) { return false; }
static bool bkagent_ota_busy(void) { return false; }
static bool bk7258_agent_trigger_model_pending(void) { return false; }
static void bkprov_bootstrap_cancel(void) {}
static bool bkprov_bootstrap_busy(void) { return false; }
static int bkprov_owner_quiesce(bool stop)
{
  if (!stop) { reopens++; owner_closed = false; return 0; }
  if (owner_error) return owner_error;
  owner_closed = true;
  return 0;
}
static int bkprov_network_cancel(void) { return 0; }
static void bkprov_network_step(void) {}
static void bkprov_config_step(void) {}
static int bk7258_vision_quiesce(bool stop)
{ vision_closed = stop; if (!stop) reopens++; return 0; }
static int bkhaptic_service_quiesce(bool stop)
{ haptic_closed = stop; if (!stop) reopens++; return 0; }
static int bkprov_storage_stop(void)
{ storage_stops++; if (storage_error) return storage_error; storage_closed = true; return 0; }
int bk7258_media_volume_acquire(enum bk7258_media_volume_owner_e owner)
{ assert(owner == BK7258_MEDIA_VOLUME_POWER); if (leased) return -EBUSY; leased = true; return 0; }
int bk7258_media_volume_release(enum bk7258_media_volume_owner_e owner)
{ assert(owner == BK7258_MEDIA_VOLUME_POWER && leased); leased = false; return 0; }
static int bk7258_agent_trigger_stop(void)
{
  trigger_stops++;
  if (trigger_error) return trigger_error;
  trigger_closed = true;
  return 0;
}
static void sync(void) {}
static int bk7258_pm_soft_off_request(void)
{
  assert(owner_closed && storage_closed && vision_closed && haptic_closed && leased);
  assert(trigger_closed);
  cp_calls++;
  return cp_error;
}
static int bk7258_pm_soft_off_status(void) { return cp_status; }
static uint64_t bkvoice_config_now_ms(void *unused) { (void)unused; return now; }
static int bkvoice_media_volume_step(int steps, unsigned int *volume)
{ (void)steps; *volume = 1; return 0; }
static int bkvoice_volume_store_set(unsigned int volume) { (void)volume; return 0; }
#include "bk7258_agent_product_power.inc"

int main(int argc, char **argv)
{
  assert(argc == 2);
  bool unpublished = !strcmp(argv[1], "unpublished-trigger");
  if (unpublished) g_trigger_started = false;
  bool trigger_fail = !strcmp(argv[1], "trigger-failure");
  bool owner_fail = !strcmp(argv[1], "admission-failure") || !strcmp(argv[1], "admission-stops-trigger");
  bool storage_fail = !strcmp(argv[1], "partial-failure") || !strcmp(argv[1], "storage-stops-trigger");
  bool cp_decline = !strcmp(argv[1], "cp-declined");
  bool cp_unknown = !strcmp(argv[1], "cp-unknown");
  assert(unpublished || trigger_fail || owner_fail || storage_fail || cp_decline || cp_unknown || !strcmp(argv[1], "normal"));
  trigger_error = trigger_fail ? -EIO : 0;
  owner_error = owner_fail ? -EIO : 0;
  storage_error = storage_fail ? -EIO : 0;
  cp_error = cp_decline || cp_unknown ? -ETIMEDOUT : 0;
  cp_status = cp_decline ? 0 : cp_unknown ? -ETIMEDOUT : 1;
  assert(product_keys_step(now));
  assert(reopens == 0 && cancel_calls == 1);
  if (strstr(argv[1], "stops-trigger") || trigger_fail)
    {
      assert(trigger_stops == 1);
      assert(trigger_closed == !trigger_fail);
    }
  if (owner_fail || storage_fail || trigger_fail) assert(cp_calls == 0);
  else assert(cp_calls == 1);
  if (owner_fail) assert(storage_stops == 0);
  now = 1000;
  assert(product_keys_step(now));
  assert(reopens == 0);
  if (owner_fail || storage_fail || cp_decline || trigger_fail)
    {
      assert(g_product_error < 0);
      /* A fresh shutdown intent may retry; ordinary polls may not reopen. */
      owner_error = storage_error = cp_error = trigger_error = 0;
      cp_status = 1;
      key_power = true;
      assert(product_keys_step(++now));
      assert(cp_calls == (cp_decline ? 2u : 1u));
      assert(reopens == 0);
    }
  puts("CONTRACT_PASS");
  return 0;
}
