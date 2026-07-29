/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/hello_app/bk7258_apctl_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_amp.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const char *apctl_state_name(uint32_t state)
{
  switch (state)
    {
      case BK7258_AP_STATE_OFF:
        return "OFF";
      case BK7258_AP_STATE_STARTING:
        return "STARTING";
      case BK7258_AP_STATE_READY:
        return "READY";
      case BK7258_AP_STATE_STOPPING:
        return "STOPPING";
      case BK7258_AP_STATE_STOPPED:
        return "STOPPED";
      case BK7258_AP_STATE_FAILED:
        return "FAILED";
      default:
        return "UNKNOWN";
    }
}

static const char *apctl_cpu2_state_name(uint32_t state)
{
  switch (state)
    {
      case BK7258_CPU2_PROBE_STATE_OFF:
        return "OFF";
      case BK7258_CPU2_PROBE_STATE_STARTING:
        return "STARTING";
      case BK7258_CPU2_PROBE_STATE_READY:
        return "READY";
      case BK7258_CPU2_PROBE_STATE_STOPPING:
        return "STOPPING";
      case BK7258_CPU2_PROBE_STATE_STOPPED:
        return "STOPPED";
      case BK7258_CPU2_PROBE_STATE_FAILED:
        return "FAILED";
      case BK7258_CPU2_PROBE_STATE_BOOTSTRAP:
        return "BOOTSTRAP";
      case BK7258_CPU2_PROBE_STATE_SECONDARY_READY:
        return "SECONDARY_READY";
      case BK7258_CPU2_PROBE_STATE_SCHEDULER_ONLINE:
        return "SCHEDULER_ONLINE";
      default:
        return "UNKNOWN";
    }
}

static const char *apctl_ipi_state_name(uint32_t state)
{
  switch (state)
    {
      case BK7258_AP_IPI_STATE_OFF:
        return "OFF";
      case BK7258_AP_IPI_STATE_INITIALIZING:
        return "INITIALIZING";
      case BK7258_AP_IPI_STATE_READY:
        return "READY";
      case BK7258_AP_IPI_STATE_REQUESTED:
        return "REQUESTED";
      case BK7258_AP_IPI_STATE_RUNNING:
        return "RUNNING";
      case BK7258_AP_IPI_STATE_PASSED:
        return "PASSED";
      case BK7258_AP_IPI_STATE_FAILED:
        return "FAILED";
      case BK7258_AP_IPI_STATE_STOPPED:
        return "STOPPED";
      default:
        return "UNKNOWN";
    }
}

static const char *apctl_smp_state_name(uint32_t state)
{
  switch (state)
    {
      case BK7258_AP_SMP_STATE_OFF:
        return "OFF";
      case BK7258_AP_SMP_STATE_INITIALIZING:
        return "INITIALIZING";
      case BK7258_AP_SMP_STATE_ONLINE:
        return "ONLINE";
      case BK7258_AP_SMP_STATE_TESTING:
        return "TESTING";
      case BK7258_AP_SMP_STATE_PASSED:
        return "PASSED";
      case BK7258_AP_SMP_STATE_FAILED:
        return "FAILED";
      default:
        return "UNKNOWN";
    }
}

static uint32_t apctl_u32(const char *value, uint32_t fallback)
{
  char *end;
  unsigned long parsed;

  if (value == NULL)
    {
      return fallback;
    }

  parsed = strtoul(value, &end, 0);
  if (*value == '\0' || *end != '\0' || parsed > UINT32_MAX)
    {
      return fallback;
    }

  return (uint32_t)parsed;
}

static void apctl_status(void)
{
  struct bk7258_ap_boot_state_s state;
  struct bk7258_cpu2_probe_state_s cpu2;
  struct bk7258_ap_ipi_state_s ipi;
  struct bk7258_ap_smp_state_s smp;
  volatile struct bk7258_cpu2_probe_state_s *shared_cpu2 =
    bk7258_cpu2_probe_state();
  volatile struct bk7258_ap_ipi_state_s *shared_ipi =
    bk7258_ap_ipi_state();
  volatile struct bk7258_ap_smp_state_s *shared_smp =
    bk7258_ap_smp_state();

  bk7258_ap_get_status(&state);
  __asm volatile ("dmb sy" ::: "memory");
  memcpy(&cpu2, (const void *)(uintptr_t)shared_cpu2, sizeof(cpu2));
  memcpy(&ipi, (const void *)(uintptr_t)shared_ipi, sizeof(ipi));
  memcpy(&smp, (const void *)(uintptr_t)shared_smp, sizeof(smp));
  __asm volatile ("dmb sy" ::: "memory");
  printf("AP state=%s(%" PRIu32 ") error=%" PRIu32
         " generation=%" PRIu32 " heartbeat=%" PRIu32 "\n",
         apctl_state_name(state.state), state.state, state.error,
         state.generation, state.heartbeat);
  printf("AP core local=%" PRIu32 " physical=%" PRIu32
         " VTOR(init/run)=%08" PRIx32 "/%08" PRIx32
         " MSP(init/run)=%08" PRIx32 "/%08" PRIx32 "\n",
         state.local_core_id, state.physical_core_id,
         state.initial_vtor, state.runtime_vtor,
         state.initial_msp, state.runtime_msp);
  printf("AP clock=%" PRIu32 " SysTick ctrl/load/current=%08" PRIx32
         "/%08" PRIx32 "/%08" PRIx32 "\n",
         state.clock_hz, state.systick_ctrl,
         state.systick_reload, state.systick_current);
  printf("AP heap=%08" PRIx32 "..%08" PRIx32
         " test=%08" PRIx32 " doorbells cp/ap=%" PRIu32 "/%" PRIu32
         "\n", state.heap_start, state.heap_end, state.heap_test,
         state.cp_to_ap_doorbells, state.ap_to_cp_doorbells);

  if (cpu2.magic == BK7258_CPU2_PROBE_STATE_MAGIC &&
      cpu2.version == BK7258_CPU2_PROBE_STATE_VERSION &&
      cpu2.size == sizeof(cpu2))
    {
      printf("CPU2 state=%s(%" PRIu32 ") error=%" PRIu32
             " generation=%" PRIu32 " heartbeat=%" PRIu32 "\n",
             apctl_cpu2_state_name(cpu2.state), cpu2.state, cpu2.error,
             cpu2.generation, cpu2.heartbeat);
      printf("CPU2 core local=%" PRIu32 " physical=%" PRIu32
             " vector=%08" PRIx32 " VTOR=%08" PRIx32
             " MSP(init/run)=%08" PRIx32 "/%08" PRIx32 "\n",
             cpu2.local_core_id, cpu2.physical_core_id, cpu2.vector,
             cpu2.runtime_vtor, cpu2.initial_msp, cpu2.runtime_msp);
      printf("CPU2 control=%08" PRIx32
             " SYS(before/after)=%08" PRIx32 "/%08" PRIx32 "\n",
             cpu2.control, cpu2.control_before, cpu2.control_after);

      if (cpu2.secondary_entry != 0 || cpu2.secondary_ready != 0 ||
          cpu2.idle_stack_base != 0 || cpu2.idle_stack_top != 0)
        {
          printf("CPU2 bootstrap entry=%08" PRIx32
                 " idle=%08" PRIx32 "..%08" PRIx32
                 " ready=%" PRIu32 " online=%08" PRIx32
                 " calls=%" PRIu32 " boots=%" PRIu32 "\n",
                 cpu2.secondary_entry, cpu2.idle_stack_base,
                 cpu2.idle_stack_top, cpu2.secondary_ready,
                 cpu2.online_mask, cpu2.smp_call_requests,
                 cpu2.boot_count);
        }

      if (cpu2.fault_exception != 0)
        {
          printf("CPU2 fault exception=%" PRIu32
                 " HFSR/CFSR=%08" PRIx32 "/%08" PRIx32
                 " PC/LR/xPSR=%08" PRIx32 "/%08" PRIx32
                 "/%08" PRIx32 "\n",
                 cpu2.fault_exception, cpu2.fault_hfsr,
                 cpu2.fault_cfsr, cpu2.fault_pc, cpu2.fault_lr,
                 cpu2.fault_xpsr);
        }
    }
  else
    {
      printf("CPU2 state unavailable magic/version/size=%08" PRIx32
             "/%" PRIu32 "/%" PRIu32 "\n",
             cpu2.magic, cpu2.version, cpu2.size);
    }

  if (ipi.magic == BK7258_AP_IPI_STATE_MAGIC &&
      ipi.version == BK7258_AP_IPI_STATE_VERSION &&
      ipi.size == sizeof(ipi))
    {
      int32_t pending01 = (int32_t)(ipi.tx_count[0] - ipi.rx_count[0]);
      int32_t pending10 = (int32_t)(ipi.tx_count[1] - ipi.rx_count[1]);

      printf("AP IPI state=%s(%" PRIu32 ") error=%" PRIu32
             " generation=%" PRIu32 " requested=%" PRIu32
             " completed=%" PRIu32 " runs=%" PRIu32 "\n",
             apctl_ipi_state_name(ipi.state), ipi.state, ipi.error,
             ipi.generation, ipi.requested_count, ipi.completed_count,
             ipi.test_runs);
      printf("IPI 0->1 tx/rx/pending=%" PRIu32 "/%" PRIu32
             "/%" PRId32 " seq=%" PRIu32 "/%" PRIu32
             " dup/lost/fail=%" PRIu32 "/%" PRIu32 "/%" PRIu32
             "\n", ipi.tx_count[0], ipi.rx_count[0], pending01,
             ipi.last_tx_sequence[0], ipi.last_rx_sequence[0],
             ipi.duplicate_count[0], ipi.lost_count[0],
             ipi.send_failures[0]);
      printf("IPI 1->0 tx/rx/pending=%" PRIu32 "/%" PRIu32
             "/%" PRId32 " seq=%" PRIu32 "/%" PRIu32
             " dup/lost/fail=%" PRIu32 "/%" PRIu32 "/%" PRIu32
             "\n", ipi.tx_count[1], ipi.rx_count[1], pending10,
             ipi.last_tx_sequence[1], ipi.last_rx_sequence[1],
             ipi.duplicate_count[1], ipi.lost_count[1],
             ipi.send_failures[1]);
      printf("IPI irq/wake cpu0=%" PRIu32 "/%" PRIu32
             " cpu1=%" PRIu32 "/%" PRIu32
             " stale/spurious=%" PRIu32 "/%" PRIu32 "\n",
             ipi.irq_count[0], ipi.wake_count[0],
             ipi.irq_count[1], ipi.wake_count[1],
             ipi.stale_count, ipi.spurious_count);
    }
  else
    {
      printf("AP IPI unavailable magic/version/size=%08" PRIx32
             "/%" PRIu32 "/%" PRIu32 "\n",
             ipi.magic, ipi.version, ipi.size);
    }

  if (smp.magic == BK7258_AP_SMP_STATE_MAGIC &&
      smp.version == BK7258_AP_SMP_STATE_VERSION &&
      smp.size == sizeof(smp))
    {
      printf("AP SMP state=%s(%" PRIu32 ") error=%" PRIu32
             " generation=%" PRIu32 " online=%08" PRIx32
             " boots=%" PRIu32 " runs=%" PRIu32
             " requested/completed=%" PRIu32 "/%" PRIu32 "\n",
             apctl_smp_state_name(smp.state), smp.state, smp.error,
             smp.generation, smp.online_mask, smp.boot_count,
             smp.test_runs, smp.requested_count, smp.completed_count);
      printf("SMP tx/rx cpu0=%" PRIu32 "/%" PRIu32
             " cpu1=%" PRIu32 "/%" PRIu32
             " coalesced=%" PRIu32 "/%" PRIu32
             " fail=%" PRIu32 "/%" PRIu32 "\n",
             smp.tx_count[0], smp.rx_count[0],
             smp.tx_count[1], smp.rx_count[1],
             smp.coalesced_count[0], smp.coalesced_count[1],
             smp.send_failures[0], smp.send_failures[1]);
      printf("SMP handler call/delivered cpu0=%" PRIu32 "/%" PRIu32
             " cpu1=%" PRIu32 "/%" PRIu32
             " callbacks=%" PRIu32 "/%" PRIu32
             " lastcpu=%" PRIu32 "\n",
             smp.call_handler_count[0],
             smp.delivered_handler_count[0],
             smp.call_handler_count[1],
             smp.delivered_handler_count[1],
             smp.callback_count[0], smp.callback_count[1],
             smp.last_callback_cpu);
      printf("SMP SysTick cpu0/cpu1=%" PRIu32 "/%" PRIu32
             " sleep enter/return=%" PRIu32 "/%" PRIu32 "\n",
             smp.systick_irq_count[0], smp.systick_irq_count[1],
             smp.sleep_enter_count, smp.sleep_return_count);
    }
  else
    {
      printf("AP SMP unavailable magic/version/size=%08" PRIx32
             "/%" PRIu32 "/%" PRIu32 "\n",
             smp.magic, smp.version, smp.size);
    }
}

static void apctl_usage(void)
{
  printf("usage: apctl start|stop|restart|status [timeout_ms]\n");
  printf("       apctl cycle [count] [timeout_ms]\n");
  printf("       apctl ipitest [count] [timeout_ms]\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  uint32_t timeout;
  uint32_t count;
  uint32_t i;
  int ret = -EINVAL;

  if (argc < 2)
    {
      apctl_usage();
      return 1;
    }

  timeout = apctl_u32(argc > 2 ? argv[2] : NULL,
                      BK7258_AP_DEFAULT_TIMEOUT_MS);

  if (strcmp(argv[1], "start") == 0)
    {
      ret = bk7258_ap_start(timeout);
    }
  else if (strcmp(argv[1], "stop") == 0)
    {
      ret = bk7258_ap_stop(timeout);
    }
  else if (strcmp(argv[1], "restart") == 0)
    {
      ret = bk7258_ap_restart(timeout);
    }
  else if (strcmp(argv[1], "status") == 0)
    {
      apctl_status();
      return 0;
    }
  else if (strcmp(argv[1], "ipitest") == 0)
    {
      count = apctl_u32(argc > 2 ? argv[2] : NULL,
                        BK7258_AP_IPI_DEFAULT_COUNT);
      timeout = apctl_u32(argc > 3 ? argv[3] : NULL,
                          BK7258_AP_IPI_DEFAULT_TIMEOUT_MS);
      ret = bk7258_ap_ipi_test(count, timeout);
    }
  else if (strcmp(argv[1], "cycle") == 0)
    {
      count = apctl_u32(argc > 2 ? argv[2] : NULL, 3);
      timeout = apctl_u32(argc > 3 ? argv[3] : NULL,
                          BK7258_AP_DEFAULT_TIMEOUT_MS);

      /* Normalize the entry state so every iteration is one complete
       * start/stop generation, including when cycle is invoked from READY.
       */

      ret = bk7258_ap_stop(timeout);
      for (i = 0; ret >= 0 && i < count; i++)
        {
          ret = bk7258_ap_start(timeout);
          if (ret < 0)
            {
              break;
            }

          apctl_status();
          ret = bk7258_ap_stop(timeout);
        }
    }
  else
    {
      apctl_usage();
      return 1;
    }

  apctl_status();
  if (ret < 0)
    {
      if (ret == -ENOTSUP)
        {
          fprintf(stderr,
                  "apctl: %s is disabled while AP scheduler-online mode "
                  "is active\n", argv[1]);
        }
      else
        {
          fprintf(stderr, "apctl: %s failed: %d\n", argv[1], ret);
        }

      return 1;
    }

  return 0;
}
