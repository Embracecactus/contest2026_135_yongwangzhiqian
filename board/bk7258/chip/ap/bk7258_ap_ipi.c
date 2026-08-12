/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/
 * bk7258_ap_ipi.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N8-B2 AP-local bidirectional IPI wrapper.  NuttX owns the IRQ contract;
 * Beken SDK cross-core mailbox APIs own the hardware data path.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_IPI

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
#  include <nuttx/atomic.h>
#  include "sched/sched.h"
#endif

#include <arch/irq.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/irq.h>

#include <driver/mailbox_types.h>

#include "bk7258_sdk_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_AP_IPI_PRIMARY_CPU          0
#define BK7258_AP_IPI_SECONDARY_CPU        1
#define BK7258_AP_IPI_PRIMARY_ENDPOINT     MAILBOX_CPU1
#define BK7258_AP_IPI_SECONDARY_ENDPOINT   MAILBOX_CPU2

#define BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY  0
#define BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY  1
#define BK7258_AP_IPI_DIRECTION_COUNT           2

#define BK7258_AP_IPI_COMMAND_MAGIC        0xb2u
#define BK7258_AP_IPI_COMMAND_MAGIC_SHIFT  24
#define BK7258_AP_IPI_COMMAND_TYPE_SHIFT   20
#define BK7258_AP_IPI_COMMAND_TYPE_MASK    0x0fu
#define BK7258_AP_IPI_COMMAND_GEN_SHIFT    12
#define BK7258_AP_IPI_COMMAND_GEN_MASK     0xffu
#define BK7258_AP_IPI_COMMAND_SEQ_MASK     0x0fffu

#define BK7258_AP_IPI_COMMAND_PING         1u
#define BK7258_AP_IPI_COMMAND_PONG         2u
#define BK7258_AP_IPI_COMMAND_WAKE         3u
#define BK7258_AP_IPI_COMMAND_SMP          4u

/****************************************************************************
 * External Function Prototypes
 ****************************************************************************/

extern bk_err_t bk_mailbox_cc_init(void);
extern bk_err_t bk_mailbox_cc_init_on_current_core(int id);
extern bk_err_t bk_mailbox_master_send(mailbox_data_t *data, uint8_t src,
                                        uint8_t dst);
extern bool bk7258_pm_ap_ipi_kick(int cpu) weak_function;

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static atomic_t g_bk7258_ap_smp_pending[CONFIG_SMP_NCPUS];
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_ap_ipi_state_valid(
  volatile struct bk7258_ap_ipi_state_s *state)
{
  return state->magic == BK7258_AP_IPI_STATE_MAGIC &&
         state->version == BK7258_AP_IPI_STATE_VERSION &&
         state->size == sizeof(struct bk7258_ap_ipi_state_s) &&
         state->generation == bk7258_ap_boot_state()->generation;
}

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
static bool bk7258_ap_smp_state_valid(
  volatile struct bk7258_ap_smp_state_s *state)
{
  return state->magic == BK7258_AP_SMP_STATE_MAGIC &&
         state->version == BK7258_AP_SMP_STATE_VERSION &&
         state->size == sizeof(struct bk7258_ap_smp_state_s) &&
         state->generation == bk7258_ap_boot_state()->generation;
}

static void bk7258_ap_smp_fail(uint32_t error)
{
  volatile struct bk7258_ap_smp_state_s *state = bk7258_ap_smp_state();

  if (bk7258_ap_smp_state_valid(state))
    {
      state->error = error;
      __asm volatile ("dmb sy" ::: "memory");
      state->state = BK7258_AP_SMP_STATE_FAILED;
      __asm volatile ("dmb sy; sev" ::: "memory");
    }
}

static void bk7258_ap_smp_initialize(uint32_t generation)
{
  volatile struct bk7258_ap_smp_state_s *state = bk7258_ap_smp_state();
  int cpu;

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_SMP_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = generation;
  state->state = BK7258_AP_SMP_STATE_INITIALIZING;
  state->last_callback_cpu = UINT32_MAX;

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      atomic_set(&g_bk7258_ap_smp_pending[cpu], 0);
    }

  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_SMP_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");
}
#endif

static void bk7258_ap_ipi_fail(uint32_t error)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();

  state->error = error;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_IPI_STATE_FAILED;
  __asm volatile ("dmb sy; sev" ::: "memory");
}

static uint32_t bk7258_ap_ipi_encode(uint32_t type, uint32_t sequence)
{
  uint32_t generation = bk7258_ap_boot_state()->generation;

  return (BK7258_AP_IPI_COMMAND_MAGIC <<
          BK7258_AP_IPI_COMMAND_MAGIC_SHIFT) |
         ((type & BK7258_AP_IPI_COMMAND_TYPE_MASK) <<
          BK7258_AP_IPI_COMMAND_TYPE_SHIFT) |
         ((generation & BK7258_AP_IPI_COMMAND_GEN_MASK) <<
          BK7258_AP_IPI_COMMAND_GEN_SHIFT) |
         (sequence & BK7258_AP_IPI_COMMAND_SEQ_MASK);
}

static int bk7258_ap_ipi_send(uint32_t type, uint32_t sequence,
                              int target_cpu, bool fatal)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();
  mailbox_data_t data;
  mailbox_endpoint_t src;
  mailbox_endpoint_t dst;
  int direction = -1;
  int self = up_cpu_index();
  bk_err_t ret;

  if (!bk7258_ap_ipi_state_valid(state))
    {
      return -EIO;
    }

  if (self == BK7258_AP_IPI_PRIMARY_CPU &&
      target_cpu == BK7258_AP_IPI_SECONDARY_CPU)
    {
      src = BK7258_AP_IPI_PRIMARY_ENDPOINT;
      dst = BK7258_AP_IPI_SECONDARY_ENDPOINT;
      if (type == BK7258_AP_IPI_COMMAND_PING)
        {
          direction = BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY;
        }
    }
  else if (self == BK7258_AP_IPI_SECONDARY_CPU &&
           target_cpu == BK7258_AP_IPI_PRIMARY_CPU)
    {
      src = BK7258_AP_IPI_SECONDARY_ENDPOINT;
      dst = BK7258_AP_IPI_PRIMARY_ENDPOINT;
      if (type == BK7258_AP_IPI_COMMAND_PONG)
        {
          direction = BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY;
        }
    }
  else
    {
      if (fatal)
        {
          bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_ENDPOINT);
        }

      return -EINVAL;
    }

  memset(&data, 0, sizeof(data));
  data.param2 = bk7258_ap_ipi_encode(type, sequence);

  if (direction >= 0)
    {
      state->tx_count[direction]++;
      state->last_tx_sequence[direction] = sequence;
    }

  __asm volatile ("dmb sy" ::: "memory");
  ret = bk_mailbox_master_send(&data, (uint8_t)src, (uint8_t)dst);
  if (ret != BK_OK)
    {
      if (direction >= 0)
        {
          state->send_failures[direction]++;
        }

      if (fatal)
        {
          bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_SEND);
        }

      return -EIO;
    }

  __asm volatile ("dsb sy; sev" ::: "memory");
  return OK;
}

static int bk7258_ap_ipi_record_receive(int direction, uint32_t sequence)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();
  uint32_t expected = state->last_rx_sequence[direction] + 1u;

  state->rx_count[direction]++;

  if (sequence < expected)
    {
      state->duplicate_count[direction]++;
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_DUPLICATE);
      return -EIO;
    }

  if (sequence > expected)
    {
      state->lost_count[direction] += sequence - expected;
      state->last_rx_sequence[direction] = sequence;
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_LOST);
      return -EIO;
    }

  state->last_rx_sequence[direction] = sequence;
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}

static void bk7258_ap_ipi_reset_test(
  volatile struct bk7258_ap_ipi_state_s *state, uint32_t count,
  uint32_t timeout_ms)
{
  int direction;
  int cpu;

  state->error = BK7258_AP_IPI_ERROR_NONE;
  state->requested_count = count;
  state->completed_count = 0;
  state->timeout_ms = timeout_ms;
  state->test_runs++;

  for (direction = 0;
       direction < BK7258_AP_IPI_DIRECTION_COUNT;
       direction++)
    {
      state->tx_count[direction] = 0;
      state->rx_count[direction] = 0;
      state->last_tx_sequence[direction] = 0;
      state->last_rx_sequence[direction] = 0;
      state->duplicate_count[direction] = 0;
      state->lost_count[direction] = 0;
      state->send_failures[direction] = 0;
    }

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      state->irq_count[cpu] = 0;
      state->wake_count[cpu] = 0;
      state->last_command[cpu] = 0;
    }

  state->spurious_count = 0;
  state->stale_count = 0;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_IPI_STATE_RUNNING;
  __asm volatile ("dmb sy; sev" ::: "memory");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_ipi_primary_initialize(void)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();
  uint32_t generation = bk7258_ap_boot_state()->generation;
  bk_err_t ret;

  memset((void *)(uintptr_t)state, 0, sizeof(*state));
  state->version = BK7258_AP_IPI_STATE_VERSION;
  state->size = sizeof(*state);
  state->generation = generation;
  state->state = BK7258_AP_IPI_STATE_INITIALIZING;
  __asm volatile ("dmb sy" ::: "memory");
  state->magic = BK7258_AP_IPI_STATE_MAGIC;
  __asm volatile ("dmb sy" ::: "memory");

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  bk7258_ap_smp_initialize(generation);
#endif

  ret = bk_mailbox_cc_init();
  if (ret != BK_OK)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_SDK_IRQ);
      return -EIO;
    }

  /* Keep the SDK's complete MBOX0 initialization sequence on the AP primary
   * physical core.  AP logical CPU0 is BK7258 physical CPU1, so the mailbox
   * endpoint must be MAILBOX_CPU1.  Passing MAILBOX_CPU0 here would execute
   * the CP-only global device/FIFO reset after CP has already initialized its
   * logical channels, which can strand a CP-to-AP response during cold boot.
   * The mailbox-channel wrapper's shared ISR continues to route zero-length
   * frames to the SMP handler and nonzero frames to logical channels.
   */

  ret = bk_mailbox_cc_init_on_current_core(MAILBOX_CPU1);
  if (ret != BK_OK)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_SDK_MAILBOX);
      return -EIO;
    }

  state->error = BK7258_AP_IPI_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_IPI_STATE_READY;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

int bk7258_ap_ipi_secondary_initialize(void)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();
  int ret;

  if (!bk7258_ap_ipi_state_valid(state) ||
      state->state != BK7258_AP_IPI_STATE_READY)
    {
      return -EIO;
    }

  ret = up_prioritize_irq(BK7258_IRQ_MAILBOX,
                          NVIC_SYSH_PRIORITY_DEFAULT);
  if (ret < 0)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_SDK_IRQ);
      return ret;
    }

  bk7258_clear_pending_irq(BK7258_IRQ_MAILBOX);
  up_enable_irq(BK7258_IRQ_MAILBOX);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  return OK;
}

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
int bk7258_ap_ipi_send_smp(int target_cpu)
{
  volatile struct bk7258_ap_smp_state_s *state = bk7258_ap_smp_state();
  uint32_t command;
  int self = up_cpu_index();
  int ret;

  if (!bk7258_ap_smp_state_valid(state) ||
      (state->state != BK7258_AP_SMP_STATE_ONLINE &&
       state->state != BK7258_AP_SMP_STATE_TESTING &&
       state->state != BK7258_AP_SMP_STATE_PASSED))
    {
      return -EAGAIN;
    }

  if (self < BK7258_AP_IPI_PRIMARY_CPU ||
      self > BK7258_AP_IPI_SECONDARY_CPU ||
      target_cpu < BK7258_AP_IPI_PRIMARY_CPU ||
      target_cpu > BK7258_AP_IPI_SECONDARY_CPU ||
      target_cpu == self)
    {
      bk7258_ap_smp_fail(BK7258_AP_SMP_ERROR_BAD_CPU);
      return -EINVAL;
    }

  if (atomic_xchg(&g_bk7258_ap_smp_pending[target_cpu], 1) != 0)
    {
      state->coalesced_count[self]++;
      __asm volatile ("dmb sy" ::: "memory");
      return OK;
    }

  command = bk7258_ap_ipi_encode(BK7258_AP_IPI_COMMAND_SMP, 0);
  ret = bk7258_ap_ipi_send(BK7258_AP_IPI_COMMAND_SMP, 0,
                           target_cpu, false);
  if (ret < 0)
    {
      atomic_set_release(&g_bk7258_ap_smp_pending[target_cpu], 0);
      state->send_failures[self]++;
      bk7258_ap_smp_fail(BK7258_AP_SMP_ERROR_SEND);
      return ret;
    }

  state->tx_count[self]++;
  state->last_command[self] = command;
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}

void bk7258_ap_ipi_mark_scheduler_online(void)
{
  volatile struct bk7258_ap_smp_state_s *state = bk7258_ap_smp_state();

  if (bk7258_ap_smp_state_valid(state))
    {
      state->error = BK7258_AP_SMP_ERROR_NONE;
      state->online_mask = (1u << BK7258_AP_IPI_PRIMARY_CPU) |
                           (1u << BK7258_AP_IPI_SECONDARY_CPU);
      state->boot_count++;
      __asm volatile ("dmb sy" ::: "memory");
      state->state = BK7258_AP_SMP_STATE_ONLINE;
      __asm volatile ("dmb sy; sev" ::: "memory");
    }
}
#endif

void crosscore_mb_rx_isr(mailbox_data_t *data)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();
  uint32_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t type;
  int local_cpu;
  int ret;

  if (!bk7258_ap_ipi_state_valid(state) || data == NULL)
    {
      return;
    }

  local_cpu = up_cpu_index();
  if (local_cpu < BK7258_AP_IPI_PRIMARY_CPU ||
      local_cpu > BK7258_AP_IPI_SECONDARY_CPU)
    {
      state->spurious_count++;
      return;
    }

  state->irq_count[local_cpu]++;
  state->wake_count[local_cpu]++;
  command = data->param2;
  state->last_command[local_cpu] = command;

  if ((command >> BK7258_AP_IPI_COMMAND_MAGIC_SHIFT) !=
      BK7258_AP_IPI_COMMAND_MAGIC)
    {
      state->spurious_count++;
      if (state->state == BK7258_AP_IPI_STATE_RUNNING)
        {
          bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_COMMAND);
        }

      return;
    }

  generation = (command >> BK7258_AP_IPI_COMMAND_GEN_SHIFT) &
               BK7258_AP_IPI_COMMAND_GEN_MASK;
  if (generation !=
      (state->generation & BK7258_AP_IPI_COMMAND_GEN_MASK))
    {
      state->stale_count++;
      return;
    }

  type = (command >> BK7258_AP_IPI_COMMAND_TYPE_SHIFT) &
         BK7258_AP_IPI_COMMAND_TYPE_MASK;
  sequence = command & BK7258_AP_IPI_COMMAND_SEQ_MASK;

  if (type == BK7258_AP_IPI_COMMAND_WAKE)
    {
      if (local_cpu != BK7258_AP_IPI_SECONDARY_CPU ||
          data->param0 != BK7258_AP_IPI_PRIMARY_ENDPOINT)
        {
          state->spurious_count++;
        }

      return;
    }

#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
  if (type == BK7258_AP_IPI_COMMAND_SMP)
    {
      volatile struct bk7258_ap_smp_state_s *smp =
        bk7258_ap_smp_state();
      volatile struct bk7258_cpu2_probe_state_s *cpu2 =
        bk7258_cpu2_probe_state();
      uint32_t expected_endpoint =
        local_cpu == BK7258_AP_IPI_PRIMARY_CPU ?
        BK7258_AP_IPI_SECONDARY_ENDPOINT :
        BK7258_AP_IPI_PRIMARY_ENDPOINT;

      if (!bk7258_ap_smp_state_valid(smp) ||
          data->param0 != expected_endpoint ||
          data->param1 != (uint32_t)local_cpu)
        {
          state->spurious_count++;
          bk7258_ap_smp_fail(BK7258_AP_SMP_ERROR_BAD_CPU);
          return;
        }

      atomic_set_release(&g_bk7258_ap_smp_pending[local_cpu], 0);
      smp->rx_count[local_cpu]++;
      smp->last_command[local_cpu] = command;

      if (local_cpu == BK7258_AP_IPI_SECONDARY_CPU &&
          cpu2->magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
          cpu2->generation == smp->generation)
        {
          cpu2->heartbeat++;
        }

      __asm volatile ("dmb sy" ::: "memory");

      /* AP1 has no local SysTick.  When this scheduler edge was sent only to
       * forward an active CP sleep vote, running NuttX's delivered-task path
       * first can replace the startup idle exception frame and AP1 never
       * returns to up_idle() inside CP's bounded acknowledgement window.
       * Let the PM wrapper consume that otherwise empty edge before scheduler
       * delivery; an actual scheduler edge is still handled normally whenever
       * there is no uncached CP vote.
       */

      if (local_cpu == BK7258_AP_IPI_SECONDARY_CPU &&
          bk7258_pm_ap_ipi_kick != NULL &&
          bk7258_pm_ap_ipi_kick(local_cpu))
        {
          __asm volatile ("dmb sy" ::: "memory");
          return;
        }

      smp->call_handler_count[local_cpu]++;
      (void)nxsched_smp_call_handler(BK7258_IRQ_MAILBOX, NULL, NULL);
      smp->delivered_handler_count[local_cpu]++;
      nxsched_process_delivered(local_cpu);

      __asm volatile ("dmb sy" ::: "memory");
      return;
    }
#endif

  if (state->state != BK7258_AP_IPI_STATE_RUNNING)
    {
      state->spurious_count++;
      return;
    }

  if (type == BK7258_AP_IPI_COMMAND_PING)
    {
      if (local_cpu != BK7258_AP_IPI_SECONDARY_CPU ||
          data->param0 != BK7258_AP_IPI_PRIMARY_ENDPOINT ||
          data->param1 != BK7258_AP_IPI_SECONDARY_CPU)
        {
          bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_ENDPOINT);
          return;
        }

      ret = bk7258_ap_ipi_record_receive(
        BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY, sequence);
      if (ret >= 0)
        {
          (void)bk7258_ap_ipi_send(BK7258_AP_IPI_COMMAND_PONG,
                                   sequence,
                                   BK7258_AP_IPI_PRIMARY_CPU, true);
        }

      return;
    }

  if (type == BK7258_AP_IPI_COMMAND_PONG)
    {
      if (local_cpu != BK7258_AP_IPI_PRIMARY_CPU ||
          data->param0 != BK7258_AP_IPI_SECONDARY_ENDPOINT ||
          data->param1 != BK7258_AP_IPI_PRIMARY_CPU)
        {
          bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_ENDPOINT);
          return;
        }

      (void)bk7258_ap_ipi_record_receive(
        BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY, sequence);
      return;
    }

  bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_COMMAND);
}

int bk7258_ap_ipi_selftest(uint32_t count, uint32_t timeout_ms)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  uint32_t elapsed = 0;
  uint32_t heartbeat_before;
  uint32_t sequence;
  int ret;

  if (count == 0)
    {
      count = BK7258_AP_IPI_DEFAULT_COUNT;
    }

  if (timeout_ms == 0)
    {
      timeout_ms = BK7258_AP_IPI_DEFAULT_TIMEOUT_MS;
    }

  if (count > BK7258_AP_IPI_MAX_COUNT)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_COMMAND);
      return -ERANGE;
    }

  if (!bk7258_ap_ipi_state_valid(state) ||
      (state->state != BK7258_AP_IPI_STATE_READY &&
       state->state != BK7258_AP_IPI_STATE_REQUESTED &&
       state->state != BK7258_AP_IPI_STATE_PASSED &&
       state->state != BK7258_AP_IPI_STATE_FAILED) ||
      cpu2->state != BK7258_CPU2_PROBE_STATE_SECONDARY_READY ||
      cpu2->secondary_ready != 1 || cpu2->online_mask != 1)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_BAD_STATE);
      return -EAGAIN;
    }

  bk7258_ap_ipi_reset_test(state, count, timeout_ms);

  for (sequence = 1; sequence <= count; sequence++)
    {
      /* timeout_ms bounds one request/response transaction.  Do not carry
       * time already spent by an earlier successful message into the next
       * ping, or a long but healthy multi-message self-test can report a
       * false timeout.  Keep the PONG and heartbeat waits on the same budget
       * for this sequence. */

      elapsed = 0;
      heartbeat_before = cpu2->heartbeat;
      __asm volatile ("dmb sy" ::: "memory");
      ret = bk7258_ap_ipi_send(BK7258_AP_IPI_COMMAND_PING, sequence,
                               BK7258_AP_IPI_SECONDARY_CPU, true);
      if (ret < 0)
        {
          return ret;
        }

      while (state->last_rx_sequence[
             BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY] < sequence)
        {
          if (state->state == BK7258_AP_IPI_STATE_FAILED)
            {
              return -EIO;
            }

          if (elapsed >= timeout_ms)
            {
              bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_TIMEOUT);
              return -ETIMEDOUT;
            }

          up_mdelay(1);
          elapsed++;
        }

      while (cpu2->heartbeat == heartbeat_before)
        {
          if (state->state == BK7258_AP_IPI_STATE_FAILED)
            {
              return -EIO;
            }

          if (elapsed >= timeout_ms)
            {
              bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_TIMEOUT);
              return -ETIMEDOUT;
            }

          up_mdelay(1);
          elapsed++;
        }

      state->completed_count = sequence;
      __asm volatile ("dmb sy" ::: "memory");
    }

  if (state->tx_count[BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY] !=
        count ||
      state->rx_count[BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY] !=
        count ||
      state->tx_count[BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY] !=
        count ||
      state->rx_count[BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY] !=
        count ||
      state->last_tx_sequence[
        BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY] != count ||
      state->last_rx_sequence[
        BK7258_AP_IPI_DIR_PRIMARY_TO_SECONDARY] != count ||
      state->last_tx_sequence[
        BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY] != count ||
      state->last_rx_sequence[
        BK7258_AP_IPI_DIR_SECONDARY_TO_PRIMARY] != count ||
      state->irq_count[BK7258_AP_IPI_PRIMARY_CPU] != count ||
      state->irq_count[BK7258_AP_IPI_SECONDARY_CPU] != count ||
      state->wake_count[BK7258_AP_IPI_PRIMARY_CPU] != count ||
      state->wake_count[BK7258_AP_IPI_SECONDARY_CPU] != count ||
      state->stale_count != 0 || state->spurious_count != 0)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_COUNT_MISMATCH);
      return -EIO;
    }

  if (state->duplicate_count[0] != 0 ||
      state->duplicate_count[1] != 0)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_DUPLICATE);
      return -EIO;
    }

  if (state->lost_count[0] != 0 || state->lost_count[1] != 0)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_LOST);
      return -EIO;
    }

  if (state->send_failures[0] != 0 || state->send_failures[1] != 0)
    {
      bk7258_ap_ipi_fail(BK7258_AP_IPI_ERROR_SEND);
      return -EIO;
    }

  state->error = BK7258_AP_IPI_ERROR_NONE;
  __asm volatile ("dmb sy" ::: "memory");
  state->state = BK7258_AP_IPI_STATE_PASSED;
  __asm volatile ("dmb sy; sev" ::: "memory");
  return OK;
}

int bk7258_ap_ipi_wake_secondary(void)
{
  return bk7258_ap_ipi_send(BK7258_AP_IPI_COMMAND_WAKE, 0,
                            BK7258_AP_IPI_SECONDARY_CPU, true);
}

void bk7258_ap_ipi_mark_stopped(void)
{
  volatile struct bk7258_ap_ipi_state_s *state = bk7258_ap_ipi_state();

  if (bk7258_ap_ipi_state_valid(state))
    {
      state->state = BK7258_AP_IPI_STATE_STOPPED;
      __asm volatile ("dmb sy; sev" ::: "memory");
    }
}

#endif /* CONFIG_BK7258_AP_IPI */
