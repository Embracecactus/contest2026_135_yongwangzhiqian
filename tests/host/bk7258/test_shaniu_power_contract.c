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
#include "bk7258_health_core.h"
/* Frozen read-only diagnostic contract: phase in low byte, unresolved CP
 * ownership in bit 8, and a separate signed error. No transport side effects.
 * Until the production getter exists this binding is BLOCKED_INTERFACE.
 */
int bk7258_agent_power_status(void *context, uint32_t *state, int32_t *error);
#ifdef TEST_REAL_OWNER
#include "bk7258_provision_owner.h"
void test_owner_open(void);
bool test_owner_window(void);
unsigned int test_owner_executed(void);
void test_owner_write(void);
int test_owner_reply(void);
#endif
#define CONFIG_BK7258_USBCDC 1
static int usb_error, usb_stops;
static int content_error;
int bkcontent_quiesce(void) { return content_error; }
static bool usb_closed;
static int product_pc_usb_stop(void)
{ usb_stops++; usb_closed=!usb_error; return usb_error; }
static int pack_error, pack_stops;
static bool pack_closed;
int bk7258_display_job_quiesce(bool stop)
{ assert(stop); pack_stops++; pack_closed = !pack_error; return pack_error; }
#define CONFIG_BK7258_PRODUCT_KEYS 1
#define CONFIG_BK7258_PM_SOFT_OFF 1
#define CONFIG_BK7258_VISION_SERVICE 1
#define CONFIG_BK7258_HAPTIC_SERVICE 1
#define CONFIG_BK7258_NFC_SERVICE 1
#define CONFIG_BK7258_MOTION_SERVICE 1
static int motion_error;
static bool motion_closed;
int bk7258_motion_service_quiesce(bool stop)
{ assert(stop); motion_closed = !motion_error; return motion_error; }
#define CONFIG_BK7258_DISPLAY_SERVICE 1
#define CONFIG_BK7258_PROVISION_NATIVE 1
static int nfc_error;
static unsigned int nfc_retries;
static bool nfc_closed;
int bk7258_nfc_service_quiesce(bool stop)
{
  assert(stop);
  if (nfc_error) return nfc_error;
  nfc_closed = true;
  return 0;
}
int bk7258_nfc_service_retry_stop(void) { nfc_retries++; return 0; }
static bool g_trigger_started = true;
static int g_product_error;
static atomic_bool g_trigger_prepare_pending;
static atomic_bool g_voice_initialized = true;
static atomic_bool g_probe_running;
static bool key_power = true;
static int owner_error, storage_error, cp_error, cp_status = 1;
static unsigned int cp_calls, storage_stops, reopens, cancel_calls;
static unsigned int voice_recover_calls;
static unsigned int trigger_stops;
static int trigger_error;
static bool trigger_closed;
static bool transport_closed;
static int transport_error;
static unsigned int config_steps;
static bool drain_owner;
static bool leased, storage_closed, vision_closed, haptic_closed;
#ifndef TEST_REAL_OWNER
static bool owner_closed;
#endif
static uint64_t now;
static bool voice_idle = true;
static int voice_recover_error;
static void bkvoice_keys_take(int *steps, bool *power)
{ *steps = 0; *power = key_power; key_power = false; }
static bool bkvoice_keys_power_held(void) { return false; }
static int display_phase;
static int bk7258_display_power(int mode) { display_phase = mode; return 0; }
static bool voice_channel_is_idle(void) { return voice_idle; }
static void voice_channel_cancel(void) { cancel_calls++; }
static int voice_channel_recover(void)
{ voice_recover_calls++; return voice_recover_error; }
static bool bkprov_network_busy(void) { return false; }
static bool bkprov_config_busy(void) { return false; }
#ifndef TEST_REAL_OWNER
static bool bkprov_scan_busy(void) { return false; }
#endif
static bool bkagent_ota_busy(void) { return false; }
static bool bk7258_agent_trigger_model_pending(void) { return false; }
static void bkprov_bootstrap_cancel(void) {}
static bool bkprov_bootstrap_busy(void) { return false; }
#ifndef TEST_REAL_OWNER
int bkprov_owner_prepare_stop(uint64_t tick)
{
  (void)tick;
  bool stop = true;
  if (!stop) { reopens++; owner_closed = false; return 0; }
  if (owner_error) return owner_error;
  owner_closed = true;
  return 0;
}
static int bkprov_owner_quiesce(bool stop)
{
  assert(stop && storage_closed && trigger_closed && vision_closed && haptic_closed);
  if (transport_error) return transport_error;
  transport_closed = true;
  return 0;
}
#endif
static int bkprov_network_cancel(void) { return 0; }
static void bkprov_network_step(void) {}
static void bkprov_config_step(void)
{
  config_steps++;
  if (drain_owner && config_steps >= 2) owner_error = 0;
}
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
  assert(storage_closed && vision_closed && haptic_closed && leased && trigger_closed && nfc_closed);
#ifdef TEST_REAL_OWNER
  assert(!bkprov_owner_busy() && !test_owner_window());
#else
  assert(owner_closed && transport_closed);
#endif
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
  if (!strcmp(argv[1], "cp-query"))
    {
      uint32_t state;
      int32_t error;
      assert(product_keys_step(now));
      assert(bk7258_agent_power_status(NULL, &state, &error) == 0);
      assert(state == (2u | 256u) && error == -EINPROGRESS);
      now = 30000;
      assert(product_keys_step(now));
      for (unsigned i = 0; i < 100; i++)
        {
          assert(bk7258_agent_power_status(NULL, &state, &error) == 0);
          assert(state == (3u | 256u) && error == -ETIMEDOUT);
        }
      struct bkhealth_rpc_request_s request = {
        .magic = BKHEALTH_RPC_MAGIC, .version = 1, .command = 2,
        .session = 9, .sequence = 1
      };
      struct bkhealth_rpc_response_s response;
      struct bkhealth_source_ops_s ops = {
        .power_status = bk7258_agent_power_status
      };
      for (unsigned i = 0; i < 100; i++)
        {
          request.sequence++;
          assert(bkhealth_rpc_handle_request(&request, &response, &ops, NULL) == 0);
          assert(bkhealth_rpc_response_valid(&response));
          assert(response.command == 0x8001 && response.flags == 0);
          assert(response.session == 9 && response.sequence == request.sequence);
          assert(response.reserved[0] == (3u | 256u));
          assert((int32_t)response.reserved[1] == -ETIMEDOUT);
        }
      response.reserved[0] |= 512u;
      assert(!bkhealth_rpc_response_valid(&response));
      response.reserved[0] = 3;
      response.reserved[1] = 1;
      assert(!bkhealth_rpc_response_valid(&response));
      response.reserved[1] = (uint32_t)-ETIMEDOUT;
      response.flags = 1;
      assert(!bkhealth_rpc_response_valid(&response));
      response.flags = 0;
      response.command = 0x8000; /* old STATUS cannot carry power data */
      assert(!bkhealth_rpc_response_valid(&response));
      ops.power_status = NULL;
      assert(bkhealth_rpc_handle_request(&request, &response, &ops, NULL) == -ENOTSUP);
      assert(bkhealth_rpc_response_valid(&response));
      assert(response.reserved[0] == 0 && response.reserved[1] == 0);
      assert(cp_calls == 1 && reopens == 0 && transport_closed);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "prepare-only"))
    {
      /* Development firmware may exercise the real exit participants but
       * must never submit the final power transition. This is a diagnostic
       * safety contract, not physical K2/deep-sleep acceptance.
       */
      assert(product_keys_step(now));
      assert(cp_calls == 0);
      assert(storage_closed && trigger_closed && transport_closed && leased);
      assert(vision_closed && haptic_closed && nfc_closed && motion_closed);
      now = 40000;
      assert(product_keys_step(now));
      assert(cp_calls == 0 && reopens == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "cp-new-pending") ||
      !strcmp(argv[1], "cp-retry-unknown") ||
      !strcmp(argv[1], "cp-retry-pending") ||
      !strcmp(argv[1], "cp-retry-declined"))
    {
      assert(product_keys_step(now));
      bool during = !strcmp(argv[1], "cp-new-pending");
      now = during ? 1000 : 30000;
      assert(product_keys_step(now));
      cp_status = !strcmp(argv[1], "cp-retry-unknown") ? -ETIMEDOUT :
                  !strcmp(argv[1], "cp-retry-declined") ? 0 : 1;
      key_power = true;
      assert(product_keys_step(++now));
      assert(reopens == 0 && storage_closed && trigger_closed);
      assert(cp_calls == (cp_status == 0 ? 2u : 1u));
      if (cp_status != 0)
        {
          assert(g_product_error == (cp_status < 0 ? -ETIMEDOUT : -EINPROGRESS));
          assert(display_phase == (during ? 2 : 3));
        }
      else assert(display_phase == 2);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "cp-pending-deadline") ||
      !strcmp(argv[1], "cp-unknown-deadline"))
    {
      /* LIFE-02: the existing 30 s overall exit budget includes CP waiting.
       * Acceptance is not completion; timeout must retain stopped resources
       * and report failure, never reset the deadline or reopen admission.
       */
      cp_status = !strcmp(argv[1], "cp-unknown-deadline") ? -ETIMEDOUT : 1;
      assert(product_keys_step(now));
      now = 29999;
      assert(product_keys_step(now));
      assert(display_phase == 2 && cp_calls == 1 && reopens == 0);
      now = 30000;
      assert(product_keys_step(now));
      assert(display_phase == 3 && g_product_error == -ETIMEDOUT);
      cp_status = 1; /* A late CP acceptance cannot undo the timeout. */
      now = 31000;
      assert(product_keys_step(now));
      assert(display_phase == 3 && cp_calls == 1 && reopens == 0);
      assert(storage_closed && trigger_closed && transport_closed);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "content-busy"))
    {
      content_error = -EAGAIN;
      g_shutdown_requested = true;
      g_shutdown_deadline = 30000;
      assert(product_keys_step(1000));
      assert(!g_power_pending && !g_shutdown_failed);
      content_error = 0;
      assert(product_keys_step(1100));
      assert(g_power_pending);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "motion-busy") || !strcmp(argv[1], "motion-failed"))
    {
      bool failed = !strcmp(argv[1], "motion-failed");
      motion_error = failed ? -EIO : -EBUSY;
      assert(product_keys_step(0));
      assert(cp_calls == 0 && storage_stops == 0);
      motion_error = 0;
      assert(product_keys_step(100));
      if (failed) assert(cp_calls == 0 && g_shutdown_failed);
      else assert(cp_calls == 1 && motion_closed);
      puts("CONTRACT_PASS"); return 0;
    }

  if (!strcmp(argv[1], "nfc-busy") || !strcmp(argv[1], "nfc-failed"))
    {
      bool failed = !strcmp(argv[1], "nfc-failed");
      nfc_error = failed ? -EIO : -EBUSY;
      assert(product_keys_step(0));
      assert(cp_calls == 0 && storage_stops == 0);
      nfc_error = 0;
      assert(product_keys_step(100));
      if (failed)
        {
          assert(cp_calls == 0);
          key_power = true;
          assert(product_keys_step(200));
          assert(nfc_retries == 1);
        }
      assert(cp_calls == 1 && nfc_closed);
      puts("CONTRACT_PASS"); return 0;
    }
#ifdef TEST_REAL_OWNER
  if (!strcmp(argv[1], "owner-integration"))
    {
      test_owner_open();
      storage_error = -EIO;
      assert(product_keys_step(100));
      assert(test_owner_window() && cp_calls == 0);
      unsigned int before = test_owner_executed();
      assert(product_keys_step(200));
      assert(test_owner_executed() == before + 1);
      test_owner_write();
      assert(product_keys_step(300));
      assert(test_owner_reply() == -EBUSY && test_owner_executed() == before + 1);
      storage_error = 0;
      key_power = true;
      assert(product_keys_step(400));
      assert(cp_calls == 1 && !test_owner_window() && reopens == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
#endif
  if (!strcmp(argv[1], "failure-display"))
    {
      storage_error = -EIO;
      assert(product_keys_step(0));
      assert(display_phase == 3 && cp_calls == 0);
      assert(product_keys_step(100));
      assert(display_phase == 3 && reopens == 0);
      storage_error = 0;
      key_power = true;
      assert(product_keys_step(200));
      assert(display_phase == 2 && cp_calls == 1);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "voice-cleanup-pending") ||
      !strcmp(argv[1], "voice-cleanup-failure"))
    {
      bool failed = !strcmp(argv[1], "voice-cleanup-failure");
      voice_idle = false;
      voice_recover_error = failed ? -EIO : -EBUSY;
      assert(product_keys_step(0));
      assert(cancel_calls == 1 && voice_recover_calls == 1 && cp_calls == 0);
      if (failed)
        {
          /* A permanent cleanup failure is the observable shutdown failure.
           * It must not be hidden until the generic 30-second deadline. */
          assert(g_shutdown_failed && !g_shutdown_requested);
          assert(g_product_error == -EIO && display_phase == 3);
        }
      else
        {
          assert(!g_shutdown_failed && g_shutdown_requested);
          assert(g_product_error == -EINPROGRESS && display_phase == 2);
        }

      voice_recover_error = 0;
      voice_idle = true;
      assert(product_keys_step(100));
      if (failed)
        {
          /* Polling continues cleanup but cannot turn a failed intent into a
           * CP request. Only a new explicit intent may retry. */
          assert(cp_calls == 0 && g_shutdown_failed && reopens == 0);
          key_power = true;
          assert(product_keys_step(200));
        }
      assert(cp_calls == 1 && reopens == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "voice-cleanup-deadline"))
    {
      voice_idle = false;
      voice_recover_error = -EBUSY;
      assert(product_keys_step(0));
      uint64_t deadline = g_shutdown_deadline;
      assert(deadline == 30000 && g_shutdown_requested);
      assert(!g_shutdown_failed && cp_calls == 0 && display_phase == 2);

      assert(product_keys_step(1000));
      assert(g_shutdown_deadline == deadline && g_shutdown_requested);
      assert(!g_shutdown_failed && cp_calls == 0);
      assert(product_keys_step(29999));
      assert(g_shutdown_deadline == deadline && g_shutdown_requested);
      assert(!g_shutdown_failed && cp_calls == 0);

      /* Repeated cleanup progress cannot renew the original intent deadline.
       * The exact boundary becomes a visible failure, never a CP request. */
      assert(product_keys_step(30000));
      assert(g_shutdown_deadline == deadline && g_shutdown_failed);
      assert(!g_shutdown_requested && g_product_error == -ETIMEDOUT);
      assert(cp_calls == 0 && reopens == 0 && display_phase == 3);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "final-close-drains"))
    {
      transport_error = -EAGAIN;
      assert(product_keys_step(0));
      assert(cp_calls == 0 && storage_closed && !transport_closed);
      transport_error = 0;
      assert(product_keys_step(100));
      assert(cp_calls == 1 && transport_closed && reopens == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "admission-drains"))
    {
      owner_error = -EAGAIN;
      drain_owner = true;
      assert(product_keys_step(0));
      assert(cp_calls == 0 && trigger_closed);
      assert(config_steps == 1);
      assert(product_keys_step(100));
      assert(product_keys_step(200));
      assert(cp_calls == 1 && cancel_calls == 1 && reopens == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "failed-drains"))
    {
      owner_error = -EIO;
      assert(product_keys_step(0));
      unsigned int before = config_steps;
      assert(product_keys_step(100));
      assert(config_steps > before && cp_calls == 0 && reopens == 0);
      puts("CONTRACT_PASS");
      return 0;
    }
  if (!strcmp(argv[1], "pack-busy") || !strcmp(argv[1], "pack-failed"))
    {
      bool failed = !strcmp(argv[1], "pack-failed");
      pack_error = failed ? -EIO : -EAGAIN;
      assert(product_keys_step(100));
      assert(pack_stops == 1 && !pack_closed && cp_calls == 0);
      assert(g_shutdown_failed == failed);
      pack_error = 0;
      assert(product_keys_step(101));
      if (failed) assert(cp_calls == 0);
      else assert(cp_calls == 1 && pack_closed);
      puts("CONTRACT_PASS"); return 0;
    }
  if (!strcmp(argv[1], "usb-failed"))
    {
      (void)product_pc_usb_stop;
      usb_error=-EIO;
      assert(product_keys_step(0));
      assert(usb_stops==1 && !usb_closed && cp_calls==0);
      assert(g_shutdown_failed && trigger_closed && motion_closed);
      puts("CONTRACT_PASS");return 0;
    }
  if (!strcmp(argv[1], "usb-close"))
    {
      assert(product_keys_step(0));
      assert(usb_stops==1 && usb_closed && cp_calls==1);
      puts("CONTRACT_PASS");return 0;
    }
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
