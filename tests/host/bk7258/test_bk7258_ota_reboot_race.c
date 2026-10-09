/* SPDX-License-Identifier: Apache-2.0 */
/* Host-only PREPARE/COMMIT interleaving; reset is trapped, never executed. */

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OK 0
#define BK7258_OTA_PAIR_CONFIRMED 3
#define BK7258_RESET_SOURCE_REBOOT 0
#define BK7258_OTA_RPMSG_PAIR_STATUS 14
#define BK7258_OTA_RPMSG_REBOOT_PREPARE 15
#define BK7258_OTA_RPMSG_REBOOT_COMMIT 16
#define TEST_SESSION 42u
#define TEST_GENERATION 7u

typedef int irqstate_t;

struct bk7258_ota_pair_snapshot_s
{
  int state;
  bool security_counter_present;
  unsigned int security_counter;
  int version;
};

struct bk7258_rptun_control_s
{
  unsigned int generation;
};

struct bk7258_ota_rpmsg_message_s
{
  struct
  {
    unsigned int command;
    unsigned int session;
    unsigned int generation;
  } header;
};

struct bk7258_ota_rpmsg_dev_s
{
  int lock;
  bool stage_busy;
  bool staged_candidate_ready;
  bool reboot_prepared;
  bool lifecycle_pending;
  unsigned int staged_candidate_epoch;
  unsigned int reboot_prepared_session;
  unsigned int reboot_prepared_generation;
  unsigned int reboot_prepared_candidate_epoch;
  unsigned int lifecycle_command;
  unsigned int lifecycle_session;
  unsigned int lifecycle_generation;
  struct
  {
    unsigned int security_counter;
    int image_version;
  } manifest;
  int worker_sem;
};

static struct bk7258_ota_rpmsg_dev_s g_bk7258_ota_rpmsg;
static struct bk7258_rptun_control_s g_control;
static struct bk7258_ota_pair_snapshot_s g_snapshot;
static jmp_buf g_reset;
static unsigned int g_posts;
static unsigned int g_resets;
static bool g_endpoint_ready;
static bool g_immediate_commit;
static int g_commit_result;
static int g_duplicate_result;
static int g_post_result;
static int g_reply_result;
static int g_reply_status;
static int g_snapshot_result;

static struct bk7258_rptun_control_s *bk7258_rptun_control(void)
{
  return &g_control;
}

static bool bk7258_ota_rpmsg_ready(void)
{
  return g_endpoint_ready;
}

static int bk7258_ota_get_active_pair(
  struct bk7258_ota_pair_snapshot_s *snapshot)
{
  *snapshot = g_snapshot;
  return g_snapshot_result;
}

static irqstate_t spin_lock_irqsave(int *lock)
{
  assert(*lock == 0);
  *lock = 1;
  return 0;
}

static void spin_unlock_irqrestore(int *lock, irqstate_t flags)
{
  assert(*lock == 1 && flags == 0);
  *lock = 0;
}

static int bk7258_mcuboot_version_compare(const int *left, const int *right)
{
  return *left - *right;
}

static int nxsem_post(int *sem)
{
  assert(sem == &g_bk7258_ota_rpmsg.worker_sem);
  g_posts++;
  return g_post_result;
}

static _Noreturn void bk7258_system_reset(int reason)
{
  assert(reason == BK7258_RESET_SOURCE_REBOOT);
  g_resets++;
  longjmp(g_reset, 1);
}

static int receive(unsigned int command, unsigned int session,
                   unsigned int generation);

static int bk7258_ota_rpmsg_control_reply(unsigned int session, int status,
                                         const void *payload,
                                         unsigned int size, bool wait)
{
  (void)payload;
  (void)size;
  (void)wait;
  assert(g_bk7258_ota_rpmsg.lock == 0);
  g_reply_status = status;
  if (g_immediate_commit && status == OK)
    {
      /* AP can react before the PREPARE send returns to the CP worker. */

      g_immediate_commit = false;
      g_commit_result = receive(BK7258_OTA_RPMSG_REBOOT_COMMIT, session,
                                TEST_GENERATION);
      g_duplicate_result = receive(BK7258_OTA_RPMSG_REBOOT_COMMIT, session,
                                   TEST_GENERATION);
    }
  return g_reply_result;
}

#include "lifecycle_worker.inc"

static int receive(unsigned int command, unsigned int session,
                   unsigned int generation)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  struct bk7258_ota_rpmsg_message_s message =
    {.header = {command, session, generation}};
  struct bk7258_ota_rpmsg_message_s *msg = &message;
  irqstate_t flags;

#include "lifecycle_receive.inc"

  return -ENOMSG;
}

static int run_pending(void)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;

  assert(priv->lifecycle_pending && priv->stage_busy);
  priv->lifecycle_pending = false;
  return bk7258_ota_rpmsg_lifecycle_worker(priv->lifecycle_command,
                                         priv->lifecycle_session,
                                         priv->lifecycle_generation);
}

static void reset_fixture(void)
{
  memset(&g_bk7258_ota_rpmsg, 0, sizeof(g_bk7258_ota_rpmsg));
  g_control.generation = TEST_GENERATION;
  g_bk7258_ota_rpmsg.staged_candidate_ready = true;
  g_bk7258_ota_rpmsg.staged_candidate_epoch = 3;
  g_bk7258_ota_rpmsg.manifest.security_counter = 2;
  g_bk7258_ota_rpmsg.manifest.image_version = 2;
  g_snapshot.state = BK7258_OTA_PAIR_CONFIRMED;
  g_snapshot.security_counter_present = true;
  g_snapshot.security_counter = 1;
  g_snapshot.version = 1;
  g_posts = 0;
  g_resets = 0;
  g_endpoint_ready = true;
  g_immediate_commit = false;
  g_commit_result = 1;
  g_duplicate_result = 1;
  g_post_result = 0;
  g_reply_result = 0;
  g_reply_status = 1;
  g_snapshot_result = 0;
}

static void prepare(void)
{
  assert(receive(BK7258_OTA_RPMSG_REBOOT_PREPARE,
                 TEST_SESSION, TEST_GENERATION) == OK);
  assert(run_pending() == OK);
  assert(g_reply_status == OK);
}

int main(void)
{
  struct bk7258_ota_rpmsg_dev_s *priv = &g_bk7258_ota_rpmsg;
  unsigned int invalid;
  unsigned int session;
  unsigned int generation;

  reset_fixture();
  g_immediate_commit = true;
  prepare();
  assert(g_commit_result == OK && g_duplicate_result == -EBUSY);
  assert(priv->stage_busy && priv->lifecycle_pending && g_posts == 2);
  if (setjmp(g_reset) == 0)
    {
      (void)run_pending();
      assert(false);
    }
  assert(g_resets == 1);
  assert(!priv->reboot_prepared && !priv->staged_candidate_ready);

  /* A late duplicate must not reuse the token even if busy is released. */

  priv->stage_busy = false;
  assert(receive(BK7258_OTA_RPMSG_REBOOT_COMMIT,
                 TEST_SESSION, TEST_GENERATION) == OK);
  assert(run_pending() == -ESTALE);
  assert(g_resets == 1 && !priv->stage_busy);

  for (invalid = 0; invalid < 9; invalid++)
    {
      reset_fixture();
      prepare();
      assert(!priv->stage_busy && priv->reboot_prepared);
      session = TEST_SESSION;
      generation = TEST_GENERATION;
      switch (invalid)
        {
          case 0: session++; break;
          case 1: generation++; break;
          case 2: priv->staged_candidate_epoch++; break;
          case 3: priv->staged_candidate_ready = false; break;
          case 4: g_endpoint_ready = false; break;
          case 5: g_control.generation++; break;
          case 6: priv->manifest.security_counter = 1; break;
          case 7: priv->manifest.image_version = 1; break;
          case 8: g_snapshot.state = 0; break;
        }
      assert(receive(BK7258_OTA_RPMSG_REBOOT_COMMIT,
                     session, generation) == OK);
      assert(run_pending() == (invalid < 6 ? -ESTALE : -EPERM));
      assert(g_resets == 0 && !priv->stage_busy);
    }

  reset_fixture();
  g_reply_result = -EIO;
  assert(receive(BK7258_OTA_RPMSG_REBOOT_PREPARE,
                 TEST_SESSION, TEST_GENERATION) == OK);
  assert(run_pending() == -EIO);
  assert(!priv->stage_busy && !priv->reboot_prepared);

  reset_fixture();
  g_reply_result = -EIO;
  g_immediate_commit = true;
  assert(receive(BK7258_OTA_RPMSG_REBOOT_PREPARE,
                 TEST_SESSION, TEST_GENERATION) == OK);
  assert(run_pending() == -EIO);
  assert(g_commit_result == OK && priv->stage_busy);
  assert(!priv->reboot_prepared);
  assert(run_pending() == -ESTALE);
  assert(!priv->stage_busy && g_resets == 0);

  reset_fixture();
  prepare();
  g_post_result = -EIO;
  assert(receive(BK7258_OTA_RPMSG_REBOOT_COMMIT,
                 TEST_SESSION, TEST_GENERATION) == -EIO);
  assert(!priv->stage_busy && !priv->lifecycle_pending);
  assert(!priv->reboot_prepared && g_resets == 0);

  reset_fixture();
  priv->staged_candidate_ready = false;
  assert(receive(BK7258_OTA_RPMSG_REBOOT_PREPARE,
                 TEST_SESSION, TEST_GENERATION) == OK);
  assert(run_pending() == -EPERM && g_reply_status == -EPERM);
  assert(!priv->stage_busy && !priv->reboot_prepared);
  puts("PASS: immediate COMMIT, busy ownership, stale tokens, "
       "send/post failure");
  return 0;
}
