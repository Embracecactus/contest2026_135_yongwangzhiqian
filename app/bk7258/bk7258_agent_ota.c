/****************************************************************************
 * app/bk7258/bk7258_agent_ota.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authenticated App OTA source and protected-intent adapter. No dialogue,
 * audio or update engine lives here. The existing AP manager owns Flash and
 * the existing board trial service owns acceptance of the installed pair.
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <netinet/in.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_ota_manager.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#include <arch/chip/bk7258_ota_source_http.h>

#include "bk7258_agent_ota.h"
#include "bk7258_control_ota_request.h"
#include "bk7258_voice_ota_flow.h"

/* One outstanding App request. The control owner owns its allocation and
 * join; the worker owns source/CA/intent writes until done is published.
 * Only cancel, applying and staged cross threads while the worker is live. */
struct app_ota_request_s
{
  struct bkcontrol_ota_request_s request;
  struct bk7258_ota_http_source_s source;
  struct bkvoice_ota_intent_s intent;
  pthread_t thread;
  sem_t completion;
  atomic_bool cancel;
  atomic_bool applying;
  atomic_bool staged;
  atomic_bool done;
  bool intent_present;
  bool intent_owned;
  atomic_bool commit_sensitive;
  int result;
};

static struct app_ota_request_s *g_request;
static struct bkcontrol_ota_status_s g_result;
static bool g_result_valid;
static bool g_staged;

static void status_unknown(struct bkcontrol_ota_status_s *status)
{
  memset(status, 0xff, sizeof(*status));
  status->state = BKCONTROL_OTA_IDLE;
  status->result = 0;
}

static bool intent_equal(const struct bkvoice_ota_intent_s *a,
                         const struct bkvoice_ota_intent_s *b)
{
  return a->state == b->state &&
    !memcmp(a->manifest_sha256, b->manifest_sha256, 32) &&
    bk7258_mcuboot_version_equal(&a->source_version, &b->source_version) &&
    bk7258_mcuboot_version_equal(&a->target_version, &b->target_version) &&
    a->source_security_counter == b->source_security_counter &&
    a->target_security_counter == b->target_security_counter &&
    a->source_boot_generation == b->source_boot_generation;
}

static int intent_commit(const struct bkvoice_ota_intent_s *intent)
{
  int ret = bkvoice_ota_store_commit(intent);
  if (ret == -EINPROGRESS)
    {
      struct bkvoice_ota_intent_s observed;
      uint64_t revision;
      ret = bkvoice_ota_store_reload();
      if (!ret) ret = bkvoice_ota_store_load(&observed, &revision);
      if (!ret && !intent_equal(intent, &observed)) ret = -EAGAIN;
    }
  return ret;
}

static int intent_clear(const uint8_t digest[32])
{
  int ret = bkvoice_ota_store_clear(digest);
  if (ret == -EINPROGRESS)
    {
      struct bkvoice_ota_intent_s observed;
      uint64_t revision;
      ret = bkvoice_ota_store_reload();
      if (!ret) ret = bkvoice_ota_store_load(&observed, &revision);
      if (!ret) ret = -EAGAIN;
    }
  return ret == -ENOENT ? 0 : ret;
}

static bool superseded(const struct bkvoice_ota_intent_s *intent,
                       const struct bk7258_ota_pair_snapshot_s *pair)
{
  return pair->state == BK7258_OTA_PAIR_CONFIRMED &&
    pair->security_counter_present &&
    pair->security_counter > intent->source_security_counter &&
    pair->security_counter > intent->target_security_counter &&
    bk7258_mcuboot_version_compare(&pair->version, &intent->source_version) > 0 &&
    bk7258_mcuboot_version_compare(&pair->version, &intent->target_version) > 0;
}

static uint32_t boot_generation(void)
{
  return __atomic_load_n(&bk7258_ap_boot_state()->generation, __ATOMIC_ACQUIRE);
}

static int resolve_flow_action(
  const struct bkvoice_ota_intent_s *intent, uint32_t generation,
  const struct bk7258_ota_pair_snapshot_s *pair,
  enum bkvoice_ota_flow_action_e *action)
{
  struct bk7258_ota_manager_status_s manager;
  int ret = bkvoice_ota_flow_decide(intent, generation, pair, action);
  if (ret || *action != BKVOICE_OTA_FLOW_REBOOT) return ret;

  /* A boot generation can repeat after a whole-chip reset.  Only the current
   * AP manager's READY_TO_REBOOT state proves that this runtime still owns the
   * verified staged pair.  Without that lease, a confirmed source pair is a
   * recovered/abandoned attempt and must be restaged instead of rebooted. */
  ret = bk7258_ota_manager_get_status(&manager);
  if (!ret && manager.state != BK7258_OTA_MANAGER_READY_TO_REBOOT)
    *action = BKVOICE_OTA_FLOW_ROLLED_BACK;
  return ret;
}

/* Read-only recovery status. A lost phone URL is never reconstructed from a
 * cloud configuration. RESTAGE requires another explicit App START with the
 * same pinned catalog; confirmed/superseded intents retire on that admission. */
static int recovered_status(struct bkcontrol_ota_status_s *status)
{
  struct bkvoice_ota_intent_s intent;
  struct bk7258_ota_pair_snapshot_s pair;
  enum bkvoice_ota_flow_action_e action;
  uint64_t revision;
  status_unknown(status);
  int ret = bkvoice_ota_store_load(&intent, &revision);
  if (ret == -ENOENT) return 0;
  if (ret) return ret;
  ret = bk7258_ota_rpmsg_pair_status(&pair,
                                    CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
  if (ret) return ret;
  if (superseded(&intent, &pair)) return 0;
  ret = resolve_flow_action(&intent, boot_generation(), &pair, &action);
  if (ret) return ret;
  status->state = BKCONTROL_OTA_ACTIVE;
  status->result = -EINPROGRESS;
  status->progress = status->total = 100;
  switch (action)
    {
      case BKVOICE_OTA_FLOW_RESTAGE:
        status->state = BKCONTROL_OTA_TERMINAL;
        status->phase = BKCONTROL_OTA_FAILED;
        status->progress = status->total = UINT32_MAX;
        status->result = -EINTR;
        break;
      case BKVOICE_OTA_FLOW_REBOOT:
        status->phase = BKCONTROL_OTA_STAGED;
        break;
      case BKVOICE_OTA_FLOW_TRIAL:
        status->phase = BKCONTROL_OTA_TRIAL;
        break;
      case BKVOICE_OTA_FLOW_CONFIRMED:
        status->state = BKCONTROL_OTA_TERMINAL;
        status->phase = BKCONTROL_OTA_CONFIRMED;
        status->result = 0;
        break;
      case BKVOICE_OTA_FLOW_ROLLED_BACK:
        status->state = BKCONTROL_OTA_TERMINAL;
        status->phase = BKCONTROL_OTA_ROLLED_BACK;
        status->result = 0;
        break;
    }
  return 0;
}

static int prepare_intent(struct app_ota_request_s *job, bool *reboot)
{
  struct bk7258_mcuboot_version_s active;
  struct bk7258_ota_pair_snapshot_s pair;
  enum bkvoice_ota_flow_action_e action;
  uint64_t revision;
  uint32_t generation = boot_generation();
  int ret = bk7258_active_ap_image_version(&active);
  if (!ret) ret = bk7258_ota_rpmsg_pair_status(&pair,
                                  CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
  if (ret) return ret;
  if (!generation || generation == UINT32_MAX ||
      pair.state != BK7258_OTA_PAIR_CONFIRMED ||
      !pair.security_counter_present || !pair.security_counter ||
      !bk7258_mcuboot_version_equal(&active, &pair.version)) return -ESTALE;
  ret = bkvoice_ota_store_load(&job->intent, &revision);
  if (!ret)
    {
      job->intent_present = true;
      if (superseded(&job->intent, &pair)) action = BKVOICE_OTA_FLOW_CONFIRMED;
      else ret = resolve_flow_action(&job->intent, generation, &pair, &action);
      if (ret) return ret;
      if (action == BKVOICE_OTA_FLOW_CONFIRMED || action == BKVOICE_OTA_FLOW_ROLLED_BACK)
        {
          ret = intent_clear(job->intent.manifest_sha256);
          if (ret) return ret;
          job->intent_present = false;
        }
      else
        {
          if (memcmp(job->intent.manifest_sha256, job->request.catalog_sha256, 32))
            return -EALREADY;
          job->intent_owned = true;
          atomic_store(&job->commit_sensitive, job->intent.target_security_counter != 0);
          if (action == BKVOICE_OTA_FLOW_REBOOT) { *reboot = true; return 0; }
          return action == BKVOICE_OTA_FLOW_RESTAGE ? 0 : -EALREADY;
        }
    }
  else if (ret != -ENOENT) return ret;
  memset(&job->intent, 0, sizeof(job->intent));
  job->intent.state = BKVOICE_OTA_DOWNLOADING;
  memcpy(job->intent.manifest_sha256, job->request.catalog_sha256, 32);
  job->intent.source_version = pair.version;
  job->intent.source_security_counter = pair.security_counter;
  job->intent.source_boot_generation = generation;
  ret = intent_commit(&job->intent);
  if (!ret) job->intent_present = job->intent_owned = true;
  return ret;
}

static int source_open(void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct app_ota_request_s *job = context;
  atomic_store(&job->applying, true);
  if (atomic_load(&job->cancel)) return -ECANCELED;
  int ret = bk7258_ota_http_source_ops()->open(&job->source, manifest);
  if (ret) return ret;
  if (atomic_load(&job->cancel)) return -ECANCELED;
  job->intent.target_version = manifest->image_version;
  job->intent.target_security_counter = manifest->security_counter;
  /* Retain an uncertain target publication. Never erase it on cancel/error
   * or permit any image write until the existing CAS commit is definite. */
  atomic_store(&job->commit_sensitive, true);
  ret = intent_commit(&job->intent);
  return !ret && atomic_load(&job->cancel) ? -ECANCELED : ret;
}

static int source_read(void *context, enum bk7258_ota_image_e image,
                       uint32_t offset, uint8_t *buffer, size_t size)
{
  struct app_ota_request_s *job = context;
  if (atomic_load(&job->cancel)) return -ECANCELED;
  return bk7258_ota_http_source_ops()->read_at(&job->source, image, offset,
                                               buffer, size);
}

static int source_checkpoint(void *context, const struct bk7258_ota_progress_s *progress)
{
  struct app_ota_request_s *job = context;
  const struct bk7258_ota_source_ops_s *ops = bk7258_ota_http_source_ops();
  return ops->checkpoint ? ops->checkpoint(&job->source, progress) : 0;
}

static int source_cancel(void *context)
{
  struct app_ota_request_s *job = context;
  const struct bk7258_ota_source_ops_s *ops = bk7258_ota_http_source_ops();
  atomic_store(&job->cancel, true);
  return ops->cancel ? ops->cancel(&job->source) : 0;
}

static void source_close(void *context)
{
  struct app_ota_request_s *job = context;
  bk7258_ota_http_source_ops()->close(&job->source);
}

static const struct bk7258_ota_source_ops_s g_source_ops = {
  .open = source_open, .read_at = source_read, .checkpoint = source_checkpoint,
  .cancel = source_cancel, .close = source_close
};

static void *ota_worker(void *context)
{
  struct app_ota_request_s *job = context;
  mbedtls_x509_crt ca;
  struct in_addr peer;
  bool reboot = false;
  const char *stage = "intent";
  mbedtls_x509_crt_init(&ca);
  int ret = prepare_intent(job, &reboot);
  if (ret) goto out;
  if (reboot) goto staged;
  if (atomic_load(&job->cancel)) { ret = -ECANCELED; goto out; }
  stage = "server-ca";
  ret = mbedtls_x509_crt_parse(&ca, (const unsigned char *)job->request.ca_pem,
                              strlen(job->request.ca_pem) + 1);
  if (ret) { ret = -EKEYREJECTED; goto out; }
  memcpy(&peer.s_addr, job->request.ipv4, sizeof(peer.s_addr));
  stage = "source-init";
  ret = bk7258_ota_http_source_initialize_with_server_ca(&job->source,
                                      job->request.url, &peer, &ca);
  if (!ret) ret = bk7258_ota_http_source_expect_catalog(&job->source,
                                                       job->request.catalog_sha256);
  if (ret) goto out;
  if (atomic_load(&job->cancel)) { ret = -ECANCELED; goto out; }
  stage = "manager-apply";
  ret = bk7258_ota_manager_apply(&g_source_ops, job,
                                 CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
  if (ret) goto out;
staged:
  atomic_store(&job->staged, true);
  stage = "staged-intent";
  if (job->intent.state < BKVOICE_OTA_STAGED)
    {
      job->intent.state = BKVOICE_OTA_STAGED;
      ret = intent_commit(&job->intent);
      if (ret) goto out;
    }
  stage = "reboot-intent";
  job->intent.state = BKVOICE_OTA_REBOOTING;
  ret = intent_commit(&job->intent);
  if (ret) goto out;
  stage = "reboot";
  ret = bk7258_ota_rpmsg_reboot(CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
out:
  if (job->source.priv) source_close(job);
  mbedtls_x509_crt_free(&ca);
  if (ret && job->intent_present && job->intent_owned &&
      !atomic_load(&job->commit_sensitive) &&
      !atomic_load(&job->staged))
    {
      int cleared = intent_clear(job->intent.manifest_sha256);
      if (cleared) ret = cleared;
    }
  syslog(ret ? LOG_ERR : LOG_INFO, "BKOTA App stage=%s result=%d staged=%d\n",
         stage, ret, atomic_load(&job->staged));
  job->result = ret;
  atomic_store(&job->done, true);
  sem_post(&job->completion);
  return NULL;
}

bool bkagent_ota_busy(void)
{
  return g_request != NULL || g_staged;
}

void bkagent_ota_poll(void)
{
  struct app_ota_request_s *job = g_request;
  if (!job || !atomic_load(&job->done)) return;
  int ret = pthread_join(job->thread, NULL);
  if (ret) return;
  g_staged = atomic_load(&job->staged);
  status_unknown(&g_result);
  g_result.state = g_staged ? BKCONTROL_OTA_ACTIVE : BKCONTROL_OTA_TERMINAL;
  g_result.phase = g_staged ? BKCONTROL_OTA_STAGED : BKCONTROL_OTA_FAILED;
  if (g_staged) g_result.progress = g_result.total = 100;
  g_result.result = job->result ? job->result : -EINPROGRESS;
  g_result_valid = true;
  sem_destroy(&job->completion);
  mbedtls_platform_zeroize(job, sizeof(*job));
  free(job);
  g_request = NULL;
}

static int control_status(struct bkcontrol_ota_status_s *status)
{
  status_unknown(status);
  if (!g_request)
    {
      if (g_result_valid) { *status = g_result; return 0; }
      return recovered_status(status);
    }
  status->state = BKCONTROL_OTA_ACTIVE;
  status->phase = BKCONTROL_OTA_DOWNLOADING;
  status->progress = 0;
  status->total = 100;
  status->result = -EINPROGRESS;
  if (atomic_load(&g_request->applying))
    {
      struct bk7258_ota_manager_status_s manager;
      int ret = bk7258_ota_manager_get_status(&manager);
      if (ret) return ret;
      if (manager.state == BK7258_OTA_MANAGER_STAGING_AP ||
          manager.state == BK7258_OTA_MANAGER_STAGING_CP)
        {
          uint32_t first = manager.state == BK7258_OTA_MANAGER_STAGING_AP ? 1 : 48;
          status->progress = first + (manager.total ?
            (uint64_t)manager.completed * 46 / manager.total : 0);
          if (status->progress > 94) status->progress = 94;
        }
      else if (manager.state == BK7258_OTA_MANAGER_PAIR_VERIFIED)
        { status->phase = BKCONTROL_OTA_VERIFYING; status->progress = 100; }
      else if (manager.state == BK7258_OTA_MANAGER_READY_TO_REBOOT)
        { status->phase = BKCONTROL_OTA_STAGED; status->progress = 100; }
      /* Failure is terminal only after the worker has closed the source and
       * joined. A manager error alone does not release request resources. */
    }
  return 0;
}

int bkagent_ota_control(void *context, enum bkcontrol_command_e command,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status)
{
  (void)context;
  bkagent_ota_poll();
  if (command == BKCONTROL_OTA_STATUS) return control_status(&status->ota);
  if (command == BKCONTROL_OTA_CANCEL)
    {
      if (g_staged || (g_request && atomic_load(&g_request->staged))) return -EALREADY;
      if (g_request)
        {
          if (atomic_load(&g_request->commit_sensitive)) return -EALREADY;
          atomic_store(&g_request->cancel, true);
          int ret = bk7258_ota_manager_cancel();
          if (ret && ret != -ENOENT) return ret;
          /* SDC1/App interprets error=0 as completed cancellation. Wait for
           * source close and join, bounded by the existing five-second
           * cancellation contract; timeout retains the live request. */
          struct timespec deadline;
          if (clock_gettime(CLOCK_REALTIME, &deadline) < 0) return -errno;
          deadline.tv_sec += 5;
          do { ret = sem_timedwait(&g_request->completion, &deadline); }
          while (ret < 0 && errno == EINTR);
          if (ret < 0) return -errno;
          bool preserve = atomic_load(&g_request->commit_sensitive);
          bkagent_ota_poll();
          if (g_request) return -EINPROGRESS;
          if (preserve || g_staged) return -EALREADY;
        }
      struct bkvoice_ota_intent_s intent;
      uint64_t revision;
      int loaded = bkvoice_ota_store_load(&intent, &revision);
      if (!loaded && (intent.target_security_counter ||
                       intent.state != BKVOICE_OTA_DOWNLOADING)) return -EALREADY;
      if (loaded && loaded != -ENOENT) return loaded;
      int ret = control_status(&status->ota);
      if (!ret && status->ota.state == BKCONTROL_OTA_ACTIVE) ret = -EALREADY;
      return ret;
    }
  if (command != BKCONTROL_OTA_START) return -ENOTSUP;
  if (bkagent_ota_busy()) return -EBUSY;
  struct app_ota_request_s *job = calloc(1, sizeof(*job));
  if (!job) return -ENOMEM;
  int ret = bkcontrol_ota_request_parse(record, size, &job->request);
  bool completion_ready = false;
  if (!ret) {
    if (sem_init(&job->completion, 0, 0) < 0) ret = -errno;
    else completion_ready = true;
  }
  pthread_attr_t attr;
  if (!ret)
    {
      ret = pthread_attr_init(&attr);
      if (!ret)
        {
          ret = pthread_attr_setstacksize(&attr, CONFIG_BK7258_OTA_RPMSG_AP_STACKSIZE);
          if (!ret) ret = pthread_create(&job->thread, &attr, ota_worker, job);
          pthread_attr_destroy(&attr);
        }
      if (ret > 0) ret = -ret;
    }
  if (ret)
    {
      if (completion_ready) sem_destroy(&job->completion);
      mbedtls_platform_zeroize(job, sizeof(*job));
      free(job);
      return ret;
    }
  g_request = job;
  g_result_valid = false;
  syslog(LOG_INFO, "BKOTA App request accepted source=https catalog=pinned\n");
  return control_status(&status->ota);
}
