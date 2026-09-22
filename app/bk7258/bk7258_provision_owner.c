/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_owner.h"
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_scan.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/platform_util.h>

#define RECOVERY_HOLD_MS 8000u
#define WINDOW_MS 120000u
#define RETRY_MS 5000u

/* One owner, no callback-side TLS or file I/O. Pair allocation is kept only
 * for an open window; the independent storage worker retains pending commits.
 */
static struct
{
  mbedtls_x509_crt *certificate;
  mbedtls_pk_context *key;
  const struct bkprov_claim_ops_s *ops;
  void *context;
  uint8_t secret[32];
  struct bkprov_pair_s *pair;
  struct bkcontrol_pair_s *control;
  uint8_t control_key[32];
  bkcontrol_execute_t execute;
  bkcontrol_ota_t ota;
  bkcontrol_config_t config;
  void *control_context;
  uint64_t retry_at;
  bool recovery_requested;
  bool control_closing;
  uint64_t now;
  uint64_t hold;
  uint64_t opened;
  uint32_t epoch;
  enum bkprov_claim_state_e last_state;
  int error;
  bool sampled;
  bool armed;
  bool down;
  bool confirm_armed;
  bool recovery;
  int (*window_handler)(bool open, unsigned char secret[32], void *context);
  void *window_context;
  bool quiescing;
} g_owner;

static uint64_t owner_now(void *unused)
{
  (void)unused;
  return g_owner.now;
}

bool bkprov_owner_busy(void)
{
  return g_owner.pair != NULL || g_owner.control != NULL || bkprov_scan_busy() ||
         !bkprov_gatt_idle();
}

bool bkprov_owner_pairing(void)
{
  return g_owner.pair != NULL || g_owner.recovery_requested ||
         (g_owner.control == NULL && !g_owner.control_closing && !bkprov_gatt_idle());
}

int bkprov_owner_control(const uint8_t key[32], bkcontrol_execute_t execute,
                         void *context)
{
  uint8_t bits = 0;
  if (bkprov_owner_busy()) return -EBUSY;
  if (key == NULL && execute == NULL)
    {
      mbedtls_platform_zeroize(g_owner.control_key, 32);
      g_owner.execute = NULL;
      g_owner.ota = NULL;
      g_owner.config = NULL;
      g_owner.control_context = NULL;
      return 0;
    }
  if (key == NULL || execute == NULL || g_owner.certificate == NULL) return -EINVAL;
  for (size_t i = 0; i < 32; i++) bits |= key[i];
  if (bits == 0) return -EINVAL;
  memcpy(g_owner.control_key, key, 32);
  g_owner.execute = execute;
  g_owner.ota = NULL;
  g_owner.config = NULL;
  g_owner.control_context = context;
  return 0;
}

int bkprov_owner_control_ota(bkcontrol_ota_t ota)
{
  if (bkprov_owner_busy())
    {
      return -EBUSY;
    }

  if (ota != NULL && g_owner.execute == NULL)
    {
      return -EINVAL;
    }

  g_owner.ota = ota;
  return 0;
}

int bkprov_owner_control_config(bkcontrol_config_t config)
{
  if (bkprov_owner_busy()) return -EBUSY;
  if (config != NULL && g_owner.execute == NULL) return -EINVAL;
  g_owner.config = config;
  return 0;
}

int bkprov_owner_error(void)
{
  return g_owner.error;
}

int bkprov_owner_bind(mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                       const uint8_t secret[32],
                       const struct bkprov_claim_ops_s *ops, void *context)
{
  if (bkprov_owner_busy()) return -EBUSY;
  if (certificate == NULL || key == NULL || secret == NULL ||
      (ops != NULL && (ops->begin == NULL || ops->poll == NULL ||
                       ops->commit == NULL || ops->abort == NULL)))
    return -EINVAL;
  g_owner.certificate = certificate;
  g_owner.key = key;
  g_owner.ops = ops;
  g_owner.context = context;
  memcpy(g_owner.secret, secret, sizeof(g_owner.secret));
  g_owner.armed = false;
  g_owner.down = false;
  mbedtls_platform_zeroize(g_owner.control_key, 32);
  g_owner.execute = NULL;
  g_owner.ota = NULL;
  g_owner.config = NULL;
  g_owner.control_context = NULL;
  g_owner.recovery_requested = false;
  return 0;
}

int bkprov_owner_window_handler(
  int (*handler)(bool, unsigned char[32], void *), void *context)
{
  if (bkprov_owner_busy()) return -EBUSY;
  g_owner.window_handler = handler;
  g_owner.window_context = context;
  return 0;
}

int bkprov_owner_unbind(void)
{
  if (bkprov_owner_busy()) return -EBUSY;
  mbedtls_platform_zeroize(g_owner.secret, sizeof(g_owner.secret));
  g_owner.certificate = NULL;
  g_owner.key = NULL;
  g_owner.window_handler = NULL;
  g_owner.window_context = NULL;
  g_owner.ops = NULL;
  g_owner.context = NULL;
  g_owner.armed = false;
  g_owner.down = false;
  mbedtls_platform_zeroize(g_owner.control_key, 32);
  g_owner.execute = NULL;
  g_owner.ota = NULL;
  g_owner.config = NULL;
  g_owner.control_context = NULL;
  g_owner.recovery_requested = false;
  return 0;
}

static void close_window(int error)
{
  if (g_owner.control != NULL)
    {
      int ret = bkprov_gatt_window(false);
      bkcontrol_pair_close(g_owner.control);
      free(g_owner.control);
      g_owner.control = NULL;
      g_owner.control_closing = true;
      g_owner.retry_at = g_owner.now + RETRY_MS;
      if (error == 0 && ret < 0) error = ret;
    }
  if (g_owner.pair != NULL)
    {
      if (g_owner.window_handler)
        (void)g_owner.window_handler(false, g_owner.secret, g_owner.window_context);
      int ret = bkprov_gatt_window(false);
      bkprov_pair_close(g_owner.pair);
      free(g_owner.pair);
      g_owner.pair = NULL;
      if (!g_owner.recovery) g_owner.retry_at = g_owner.now + RETRY_MS;
      if (error == 0 && ret < 0) error = ret;
    }
  g_owner.error = error;
  g_owner.armed = false;
  g_owner.down = false;
  g_owner.confirm_armed = false;
}

/* The physical eight-second recovery channel is read-only.  A matching SRR1
 * is a public completion receipt; SRV1 remains pending rather than becoming
 * an ordinary committed configuration receipt. */
static int owner_receipt(const uint8_t transaction[16])
{
  int reset = bkprov_storage_reset_receipt(transaction);
  if (reset == BKPROV_STORAGE_RESET_RECEIPT_COMPLETED) return 1;
  if (reset == BKPROV_STORAGE_RESET_RECEIPT_PENDING) return -EAGAIN;
  if (reset != BKPROV_STORAGE_RESET_RECEIPT_ABSENT) return reset;
  return bkprov_storage_receipt(transaction);
}

int bkprov_owner_quiesce(bool enabled)
{
  g_owner.quiescing = enabled;
  if (!enabled) return 0;
  g_owner.recovery_requested = false;
  if (g_owner.pair != NULL || g_owner.control != NULL)
    close_window(-ECANCELED);
  bkprov_scan_drain();
  (void)bkprov_gatt_poll();
  return bkprov_owner_busy() ? -EAGAIN : 0;
}

static int open_window(bool recovery)
{
  uint8_t transaction[16];
  uint64_t revision;
  size_t size;
  int ret;
  if (!recovery && g_owner.ops == NULL) return -ENOSYS;
  if (recovery && g_owner.window_handler) return -EACCES;
  struct bkprov_pair_s *pair = calloc(1, sizeof(*pair));
  if (pair == NULL) return -ENOMEM;
  /* Snapshot is bounded RAM access; no RPMsgFS wait on the voice owner.
   * An unavailable/corrupt store is never interpreted as an unclaimed one.
   */
  ret = bkprov_storage_snapshot(pair->claim.bundle, BKPROV_BUNDLE_MAX,
                                &size, &revision, transaction);
  mbedtls_platform_zeroize(pair->claim.bundle, BKPROV_BUNDLE_MAX);
  if ((!recovery && ret != -ENOENT) || (recovery && ret == -EAGAIN))
    {
      free(pair);
      return ret == 0 ? -EACCES : ret;
    }
  if (g_owner.window_handler)
    {
      ret = g_owner.window_handler(true, g_owner.secret, g_owner.window_context);
      if (ret < 0) { free(pair); return ret; }
    }
  ret = bkprov_gatt_window(true);
  if (ret < 0)
    {
      if (g_owner.window_handler)
        (void)g_owner.window_handler(false, g_owner.secret, g_owner.window_context);
      free(pair);
      return ret;
    }
  g_owner.pair = pair;
  g_owner.recovery = recovery;
  g_owner.opened = g_owner.now;
  g_owner.last_state = BKPROV_CLOSED;
  g_owner.confirm_armed = false;
  return 0;
}

bool bkprov_owner_step(uint64_t now, uint32_t epoch, bool link,
                        bool pressed, bool voice_idle)
{
  bool was_active = g_owner.pair != NULL;
  if (g_owner.quiescing)
    {
      (void)bkprov_owner_quiesce(true);
      return bkprov_owner_busy();
    }
  /* A closed BLE session cannot abandon the shared Wi-Fi worker ticket. */
  bkprov_scan_drain();
  bool rollback = g_owner.sampled && now < g_owner.now;
  int ret = bkprov_gatt_poll();
  g_owner.now = now;
  g_owner.epoch = epoch;
  g_owner.sampled = true;
  /* The initial-owner session is independent of the leased PTT snapshot.
   * Clock rollback and actual GATT/TLS failures still end its window.
   */
  (void)epoch;
  (void)link;
  if (ret < 0 || rollback)
    {
      close_window(ret < 0 ? ret : -ESTALE);
      return was_active;
    }
  if (g_owner.control != NULL)
    {
      /* An eight-second physical hold can still request receipt recovery.
       * Ordinary presses remain PTT input and do not disconnect the phone.
       */
      if (pressed && !g_owner.down)
        { g_owner.down = true; g_owner.hold = now; }
      if (!pressed && g_owner.down)
        {
          bool recovery = now - g_owner.hold >= RECOVERY_HOLD_MS;
          g_owner.down = false;
          if (recovery)
            {
              close_window(0);
              g_owner.recovery_requested = true;
              return true;
            }
        }
      uint32_t generation = bkprov_gatt_generation();
      /* Bound only the unconnected discovery window here. Once a peer is
       * accepted, the pair owns handshake/authentication and validated-control
       * idle deadlines. An absolute owner window must not terminate that
       * authenticated session while requests are still being processed. */
      if (!bkprov_gatt_open() ||
          (!g_owner.control->tls.initialized && generation == 0 &&
           now - g_owner.opened >= WINDOW_MS))
        { close_window(-ETIMEDOUT); return false; }
      if (!g_owner.control->tls.initialized && generation != 0)
        {
          ret = bkcontrol_pair_start(g_owner.control, generation,
                  g_owner.certificate, g_owner.key, g_owner.control_key,
                  owner_now, NULL, g_owner.execute, g_owner.control_context);
          if (ret == 0 && g_owner.window_handler == NULL)
            {
              memcpy(g_owner.control->scan_secret, g_owner.secret, 32);
              g_owner.control->rebind_ops = g_owner.ops;
              g_owner.control->rebind_context = g_owner.context;
            }
          if (ret == 0) g_owner.opened = now;
          if (ret == 0 && g_owner.ota != NULL)
            {
              if (!g_owner.control->session.open ||
                  g_owner.control->session.authenticated)
                {
                  ret = -EPROTO;
                }
              else
                {
                  g_owner.control->session.ota = g_owner.ota;
                }
            }
          if (ret == 0 && g_owner.config != NULL)
            ret = bkcontrol_session_set_config_handler(
                &g_owner.control->session, g_owner.config);
        }
      if (ret == 0 && g_owner.control->tls.initialized)
        ret = bkcontrol_pair_step(g_owner.control);
      if (ret < 0) close_window(ret);
      return false;
    }
  if (!was_active)
    {
      if (!bkprov_gatt_idle())
        {
          g_owner.armed = false;
          g_owner.down = false;
          return !g_owner.control_closing || g_owner.recovery_requested;
        }
      g_owner.control_closing = false;
      if (g_owner.recovery_requested)
        {
          /* Wait for the original audio owner to release its resources. */
          if (!voice_idle) return true;
          g_owner.recovery_requested = false;
          g_owner.error = open_window(true);
          return true;
        }
      if (!voice_idle || g_owner.certificate == NULL)
        {
          g_owner.armed = false;
          g_owner.down = false;
          return false;
        }
      if (!pressed)
        {
          if (g_owner.down && now - g_owner.hold >= RECOVERY_HOLD_MS)
            {
              ret = open_window(true);
              g_owner.error = ret;
            }
          g_owner.down = false;
          g_owner.armed = true;
        }
      else if (g_owner.armed && !g_owner.down)
        {
          g_owner.down = true;
          g_owner.hold = now;
        }
      if (g_owner.pair == NULL && g_owner.execute != NULL &&
          now >= g_owner.retry_at)
        {
          struct bkcontrol_pair_s *control = calloc(1, sizeof(*control));
          if (control == NULL) ret = -ENOMEM;
          else
            {
              ret = bkprov_gatt_window(true);
              if (ret < 0) free(control);
              else { g_owner.control = control; g_owner.opened = now; }
            }
          if (ret < 0) { g_owner.error = ret; g_owner.retry_at = now + RETRY_MS; }
          return false;
        }
      /* Initial ownership has no button, link, or epoch gate.  The storage
       * check in open_window(false) accepts only a proven -ENOENT snapshot.
       */
      if (g_owner.pair == NULL && g_owner.ops != NULL &&
          now >= g_owner.retry_at)
        {
          ret = open_window(false);
          g_owner.error = ret;
          if (ret < 0) g_owner.retry_at = now + RETRY_MS;
        }
      return bkprov_owner_busy();
    }
  if (!bkprov_gatt_open())
    {
      close_window(-ENOTCONN);
      return true;
    }
  if (now - g_owner.opened >= WINDOW_MS)
    {
      close_window(-ETIMEDOUT);
      return true;
    }
  uint32_t generation = bkprov_gatt_generation();
  struct bkprov_pair_s *pair = g_owner.pair;
  if (!pair->tls.initialized && generation != 0)
    {
      if (g_owner.recovery)
        ret = bkprov_pair_start_recovery(pair, generation, g_owner.certificate,
                  g_owner.key, g_owner.secret, true, owner_now, NULL,
                  owner_receipt);
      else
        ret = bkprov_pair_start(pair, generation, g_owner.certificate,
                  g_owner.key, g_owner.secret, true, false, owner_now, NULL,
                  g_owner.ops, g_owner.context);
      if (ret == 0 && !g_owner.recovery)
        {
          pair->receipt = owner_receipt;
        }
      if (ret < 0)
        {
          close_window(ret);
          return true;
        }
    }
  if (pair->tls.initialized)
    {
      ret = bkprov_pair_step(pair);
      if (ret < 0)
        {
          close_window(ret);
          return true;
        }
      if (pair->claim.state != g_owner.last_state)
        {
          g_owner.last_state = pair->claim.state;
          g_owner.confirm_armed = false;
        }
      if (pair->claim.state == BKPROV_LOCAL)
        {
          if (!g_owner.recovery)
            {
              ret = bkprov_pair_confirm(pair, generation);
              if (ret < 0) close_window(ret);
            }
        }
    }
  return true;
}
