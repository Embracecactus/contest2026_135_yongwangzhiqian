/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/
 * bk7258_rptun_mbox.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N9 CP/AP MBOX0 transport wrapper.  The Beken SDK owns the device/FIFO,
 * physical callbacks and logical-channel ACK machinery.  This wrapper
 * reserves MB_CHNL_LOG for RPTUN edge notifications and defers all protocol
 * work from the SDK logical-channel ISR to a pinned NuttX kthread.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_RPTUN_MBOX

#include <errno.h>
#include <stdbool.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

#include <common/bk_err.h>
#include <driver/mailbox_channel.h>

#include "bk7258_rptun_mbox.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RPTUN_MBOX_TYPE_COUNT       5u
#define BK7258_RPTUN_MBOX_WORKER_NAME      "bk7258-rptun-rx"
#define BK7258_RPTUN_MBOX_CHANNEL          MB_CHNL_LOG
#define BK7258_RPTUN_MBOX_POLL_MS          1u

static_assert(sizeof(mb_chnl_cmd_t) == BK7258_RPTUN_MBOX_DATA_LENGTH,
              "BK7258 SDK logical mailbox command ABI drift");
static_assert(GET_LOG_CHNL_ID(BK7258_RPTUN_MBOX_CHANNEL) ==
              BK7258_RPTUN_MBOX_LOGICAL_INDEX,
              "BK7258 RPTUN logical mailbox channel drift");

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rptun_mbox_message_s
{
  uint32_t type;
  uint32_t generation;
  uint32_t value;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static sem_t g_bk7258_rptun_mbox_sem;
static sem_t g_bk7258_rptun_probe_sem;
static spinlock_t g_bk7258_rptun_mbox_lock = SP_UNLOCKED;
static struct bk7258_rptun_mbox_message_s
  g_bk7258_rptun_mbox_messages[BK7258_RPTUN_MBOX_TYPE_COUNT];
static uint32_t g_bk7258_rptun_mbox_pending;
static uint32_t g_bk7258_rptun_lifecycle_event;
static uint32_t g_bk7258_rptun_probe_reply;
static uint32_t g_bk7258_rptun_probe_sequence;
static uint32_t g_bk7258_rptun_notify_generation;
static uint32_t g_bk7258_rptun_notify_value;
static bool g_bk7258_rptun_notify_pending;
static bk7258_rptun_notify_t g_bk7258_rptun_notify;
static pid_t g_bk7258_rptun_mbox_worker = INVALID_PROCESS_ID;
static bool g_bk7258_rptun_mbox_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_RPTUN)
static inline void bk7258_rptun_mbox_mark(uint32_t flag)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  __atomic_fetch_or((uint32_t *)(uintptr_t)&control->flags,
                    flag, __ATOMIC_RELEASE);
}
#else
#  define bk7258_rptun_mbox_mark(flag) do { } while (0)
#endif

static void bk7258_rptun_mbox_receive(void *arg, mb_chnl_cmd_t *command)
{
  struct bk7258_rptun_mbox_message_s message;
  mb_chnl_ack_t *ack = (mb_chnl_ack_t *)command;
  irqstate_t flags;

  (void)arg;

  if (command == NULL)
    {
      return;
    }

  if (command->hdr.cmd != BK7258_RPTUN_MBOX_COMMAND ||
      command->param1 == BK7258_RPTUN_MBOX_INVALID ||
      command->param1 >= BK7258_RPTUN_MBOX_TYPE_COUNT)
    {
      ack->ack_state = ACK_STATE_FAIL;
      return;
    }

  message.type = command->param1;
  message.generation = command->param2;
  message.value = command->param3;
  ack->ack_state = ACK_STATE_COMPLETE;

  /* The SDK invokes this callback inside its logical-channel MBOX0 ISR.
   * Copy and coalesce only; generation validation and RPTUN/OpenAMP callbacks
   * belong to the worker thread.
   */

  flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
  g_bk7258_rptun_mbox_messages[message.type] = message;
  g_bk7258_rptun_mbox_pending |= 1u << message.type;
  spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
  nxsem_post(&g_bk7258_rptun_mbox_sem);
}

static void bk7258_rptun_mbox_tx_complete(void *arg, mb_chnl_ack_t *ack)
{
  /* Registering a completion callback is part of the SDK channel contract.
   * RPTUN delivery truth lives in shared pending bits, not in this ACK.
   */

  (void)arg;
  (void)ack;
  nxsem_post(&g_bk7258_rptun_mbox_sem);
}

static void bk7258_rptun_mbox_queue_notify(uint32_t generation,
                                           uint32_t value, bool wake)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
  if (!g_bk7258_rptun_notify_pending ||
      g_bk7258_rptun_notify_generation != generation)
    {
      g_bk7258_rptun_notify_generation = generation;
      g_bk7258_rptun_notify_value = value;
    }
  else
    {
      g_bk7258_rptun_notify_value |= value;
    }

  g_bk7258_rptun_notify_pending = true;
  spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
  if (wake)
    {
      nxsem_post(&g_bk7258_rptun_mbox_sem);
    }
}

static void bk7258_rptun_mbox_retry_notify(void)
{
  uint32_t generation;
  uint32_t value;
  irqstate_t flags;
  bool pending;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
  pending = g_bk7258_rptun_notify_pending;
  generation = g_bk7258_rptun_notify_generation;
  value = g_bk7258_rptun_notify_value;
  g_bk7258_rptun_notify_pending = false;
  g_bk7258_rptun_notify_value = 0;
  spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);

  if (!pending)
    {
      return;
    }

  ret = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_NOTIFY,
                                generation, value);
  if (ret == -EAGAIN)
    {
      /* The in-flight SDK slot will produce TX-complete and wake this
       * worker again.  Preserve the coalesced notify truth without spinning.
       */

      bk7258_rptun_mbox_queue_notify(generation, value, false);
    }
}

static void bk7258_rptun_mbox_dispatch(
  const struct bk7258_rptun_mbox_message_s *message)
{
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  bk7258_rptun_notify_t callback;
  irqstate_t flags;

  __asm volatile ("dmb sy" ::: "memory");
  if (message->generation == 0 ||
      message->generation != boot->generation)
    {
      return;
    }

  if (message->type == BK7258_RPTUN_MBOX_LIFECYCLE)
    {
      flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
      g_bk7258_rptun_lifecycle_event = message->value;
      spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
      return;
    }

  if (message->type == BK7258_RPTUN_MBOX_NOTIFY)
    {
      flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
      callback = g_bk7258_rptun_notify;
      spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);

      if (callback != NULL)
        {
          callback(message->generation, message->value);
        }

      return;
    }

  if (message->type == BK7258_RPTUN_MBOX_PROBE)
    {
#ifdef CONFIG_BK7258_AP_CORE
      if ((message->value & BK7258_RPTUN_MBOX_PROBE_REPLY) == 0)
        {
          bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_PROBE,
                                  message->generation,
                                  message->value |
                                  BK7258_RPTUN_MBOX_PROBE_REPLY);
        }
#else
      if ((message->value & BK7258_RPTUN_MBOX_PROBE_REPLY) != 0)
        {
          flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
          g_bk7258_rptun_probe_reply =
            message->value & BK7258_RPTUN_MBOX_PROBE_SEQUENCE;
          spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
          nxsem_post(&g_bk7258_rptun_probe_sem);
        }
#endif
    }
}

static int bk7258_rptun_mbox_worker(int argc, char *argv[])
{
  struct bk7258_rptun_mbox_message_s
    messages[BK7258_RPTUN_MBOX_TYPE_COUNT];
  irqstate_t flags;
  uint32_t pending;
  unsigned int type;

  (void)argc;
  (void)argv;

  for (;;)
    {
      int ret = nxsem_tickwait_uninterruptible(
                  &g_bk7258_rptun_mbox_sem,
                  MSEC2TICK(BK7258_RPTUN_MBOX_POLL_MS));

      if (ret < 0 && ret != -ETIMEDOUT)
        {
          continue;
        }

      flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
      pending = g_bk7258_rptun_mbox_pending;
      g_bk7258_rptun_mbox_pending = 0;
      memcpy(messages, g_bk7258_rptun_mbox_messages, sizeof(messages));
      spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);

      for (type = 1; type < BK7258_RPTUN_MBOX_TYPE_COUNT; type++)
        {
          if ((pending & (1u << type)) != 0)
            {
              bk7258_rptun_mbox_dispatch(&messages[type]);
            }
        }

      /* The SDK mailbox uses an edge notification over a bounded hardware
       * FIFO.  Shared RPTUN pending words are the level-triggered delivery
       * truth.  Polling the registered callback from this pinned worker
       * closes the race in which an accepted mailbox edge is coalesced or
       * missed: a zero notify consumes any shared incoming bits and also
       * gives the RPTUN wrapper a chance to re-arm outstanding outgoing
       * bits.  The callback returns immediately when neither direction is
       * pending.
       */

      if (ret == -ETIMEDOUT)
        {
          bk7258_rptun_notify_t callback;
          uint32_t generation = bk7258_ap_boot_state()->generation;

          flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
          callback = g_bk7258_rptun_notify;
          spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
          if (callback != NULL && generation != 0)
            {
              callback(generation, 0);
            }
        }

      bk7258_rptun_mbox_retry_notify();
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_rptun_mbox_initialize(void)
{
  cpu_set_t cpuset;
  pid_t pid;
  int ret;

  if (g_bk7258_rptun_mbox_initialized)
    {
      return OK;
    }

  nxsem_init(&g_bk7258_rptun_mbox_sem, 0, 0);
  nxsem_init(&g_bk7258_rptun_probe_sem, 0, 0);
  bk7258_rptun_mbox_mark(BK7258_RPTUN_FLAG_AP_MBOX_SEMS);
  pid = kthread_create(BK7258_RPTUN_MBOX_WORKER_NAME,
                       CONFIG_BK7258_RPTUN_RX_PRIORITY,
                       CONFIG_BK7258_RPTUN_RX_STACKSIZE,
                       bk7258_rptun_mbox_worker, NULL);
  if (pid < 0)
    {
      nxsem_destroy(&g_bk7258_rptun_mbox_sem);
      nxsem_destroy(&g_bk7258_rptun_probe_sem);
      return (int)pid;
    }

  bk7258_rptun_mbox_mark(BK7258_RPTUN_FLAG_AP_MBOX_THREAD);

#ifdef CONFIG_SMP
  CPU_ZERO(&cpuset);
  CPU_SET(0, &cpuset);
  ret = sched_setaffinity(pid, sizeof(cpuset), &cpuset);
  if (ret < 0)
    {
      kthread_delete(pid);
      nxsem_destroy(&g_bk7258_rptun_mbox_sem);
      nxsem_destroy(&g_bk7258_rptun_probe_sem);
      return ret;
    }

  bk7258_rptun_mbox_mark(BK7258_RPTUN_FLAG_AP_MBOX_PINNED);
#else
  (void)cpuset;
#endif

  ret = mb_chnl_init();
  if (ret != BK_OK)
    {
      kthread_delete(pid);
      nxsem_destroy(&g_bk7258_rptun_mbox_sem);
      nxsem_destroy(&g_bk7258_rptun_probe_sem);
      return -EIO;
    }

  bk7258_rptun_mbox_mark(BK7258_RPTUN_FLAG_AP_MBOX_INIT);

  ret = mb_chnl_open(BK7258_RPTUN_MBOX_CHANNEL, NULL);
  if (ret != BK_OK)
    {
      kthread_delete(pid);
      nxsem_destroy(&g_bk7258_rptun_mbox_sem);
      nxsem_destroy(&g_bk7258_rptun_probe_sem);
      return -EIO;
    }

  bk7258_rptun_mbox_mark(BK7258_RPTUN_FLAG_AP_MBOX_OPEN);

  ret = mb_chnl_ctrl(BK7258_RPTUN_MBOX_CHANNEL, MB_CHNL_SET_RX_ISR,
                     (void *)bk7258_rptun_mbox_receive);
  if (ret == BK_OK)
    {
      ret = mb_chnl_ctrl(BK7258_RPTUN_MBOX_CHANNEL,
                         MB_CHNL_SET_TX_CMPL_ISR,
                         (void *)bk7258_rptun_mbox_tx_complete);
    }

  if (ret != BK_OK)
    {
      mb_chnl_close(BK7258_RPTUN_MBOX_CHANNEL);
      kthread_delete(pid);
      nxsem_destroy(&g_bk7258_rptun_mbox_sem);
      nxsem_destroy(&g_bk7258_rptun_probe_sem);
      return -EIO;
    }

  bk7258_rptun_mbox_mark(BK7258_RPTUN_FLAG_AP_MBOX_CBS);

  g_bk7258_rptun_mbox_worker = pid;
  g_bk7258_rptun_mbox_initialized = true;
  return OK;
}

int bk7258_rptun_mbox_send(uint32_t type, uint32_t generation,
                           uint32_t value)
{
  mb_chnl_cmd_t command;
  int ret;

  if (!g_bk7258_rptun_mbox_initialized)
    {
      return -ENODEV;
    }

  if (type == BK7258_RPTUN_MBOX_INVALID ||
      type >= BK7258_RPTUN_MBOX_TYPE_COUNT || generation == 0)
    {
      return -EINVAL;
    }

  memset(&command, 0, sizeof(command));
  command.hdr.cmd = BK7258_RPTUN_MBOX_COMMAND;
  command.param1 = type;
  command.param2 = generation;
  command.param3 = value;

  /* mb_chnl_write() owns its critical section and AP SMP spinlock. */

  ret = mb_chnl_write(BK7258_RPTUN_MBOX_CHANNEL, &command);
  if (ret == BK_OK)
    {
      return OK;
    }

  return ret == BK_ERR_BUSY ? -EAGAIN : -EIO;
}

int bk7258_rptun_mbox_notify(uint32_t generation, uint32_t value)
{
  int ret;

  ret = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_NOTIFY,
                                generation, value);
  if (ret == -EAGAIN)
    {
      bk7258_rptun_mbox_queue_notify(generation, value, true);
      return OK;
    }

  return ret;
}

int bk7258_rptun_mbox_probe(uint32_t count, uint32_t timeout_ms)
{
#ifdef CONFIG_BK7258_AP_CORE
  return -ENOSYS;
#else
  volatile struct bk7258_ap_boot_state_s *boot = bk7258_ap_boot_state();
  clock_t timeout;
  irqstate_t flags;
  uint32_t generation;
  uint32_t reply;
  uint32_t sequence;
  uint32_t i;
  int ret;

  if (!g_bk7258_rptun_mbox_initialized)
    {
      return -ENODEV;
    }

  if (count == 0 || count > BK7258_RPTUN_MBOX_PROBE_SEQUENCE ||
      timeout_ms == 0)
    {
      return -EINVAL;
    }

  __asm volatile ("dmb sy" ::: "memory");
  generation = boot->generation;
  if (generation == 0 || boot->state != BK7258_AP_STATE_READY)
    {
      return -EHOSTDOWN;
    }

  timeout = MSEC2TICK(timeout_ms);
  if (timeout == 0)
    {
      timeout = 1;
    }

  while (nxsem_trywait(&g_bk7258_rptun_probe_sem) == OK)
    {
    }

  for (i = 0; i < count; i++)
    {
      sequence = ++g_bk7258_rptun_probe_sequence;
      if (sequence == 0 ||
          sequence > BK7258_RPTUN_MBOX_PROBE_SEQUENCE)
        {
          sequence = 1;
          g_bk7258_rptun_probe_sequence = sequence;
        }

      ret = bk7258_rptun_mbox_send(BK7258_RPTUN_MBOX_PROBE,
                                    generation, sequence);
      if (ret < 0)
        {
          return ret;
        }

      ret = nxsem_tickwait_uninterruptible(&g_bk7258_rptun_probe_sem,
                                            timeout);
      if (ret < 0)
        {
          return ret;
        }

      flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
      reply = g_bk7258_rptun_probe_reply;
      spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
      if (reply != sequence)
        {
          return -EPROTO;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (boot->generation != generation ||
          boot->state != BK7258_AP_STATE_READY)
        {
          return -ESTALE;
        }
    }

  return OK;
#endif
}

uint32_t bk7258_rptun_mbox_take_lifecycle(void)
{
  irqstate_t flags;
  uint32_t event;

  flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
  event = g_bk7258_rptun_lifecycle_event;
  g_bk7258_rptun_lifecycle_event = BK7258_AP_EVENT_NONE;
  spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
  return event;
}

void bk7258_rptun_mbox_set_notify(bk7258_rptun_notify_t callback)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_bk7258_rptun_mbox_lock);
  g_bk7258_rptun_notify = callback;
  spin_unlock_irqrestore(&g_bk7258_rptun_mbox_lock, flags);
}

#endif /* CONFIG_BK7258_RPTUN_MBOX */
