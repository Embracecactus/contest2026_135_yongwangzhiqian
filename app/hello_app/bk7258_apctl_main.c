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

  bk7258_ap_get_status(&state);
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
}

static void apctl_usage(void)
{
  printf("usage: apctl start|stop|restart|status [timeout_ms]\n");
  printf("       apctl cycle [count] [timeout_ms]\n");
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
  else if (strcmp(argv[1], "cycle") == 0)
    {
      count = apctl_u32(argc > 2 ? argv[2] : NULL, 3);
      timeout = apctl_u32(argc > 3 ? argv[3] : NULL,
                          BK7258_AP_DEFAULT_TIMEOUT_MS);

      for (i = 0; i < count; i++)
        {
          ret = bk7258_ap_start(timeout);
          if (ret < 0)
            {
              break;
            }

          apctl_status();
          ret = bk7258_ap_stop(timeout);
          if (ret < 0)
            {
              break;
            }
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
      fprintf(stderr, "apctl: %s failed: %d\n", argv[1], ret);
      return 1;
    }

  return 0;
}
