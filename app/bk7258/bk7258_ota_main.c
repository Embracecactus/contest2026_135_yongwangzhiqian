/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/bk7258/bk7258_ota_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal operator command for the CP-owned paired OTA mechanism.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
#  include <arch/board/board.h>
#endif

static const char *bkota_pair_state(enum bk7258_ota_pair_state_e state)
{
  switch (state)
    {
      case BK7258_OTA_PAIR_PENDING:
        return "pending";
      case BK7258_OTA_PAIR_CONFIRMED:
        return "confirmed";
      default:
        return "invalid";
    }
}

static int bkota_status(void)
{
  struct bk7258_ota_geometry_s geometry;
  struct bk7258_ota_pair_snapshot_s pair;
  struct bk7258_ota_manager_status_s manager;
#ifdef CONFIG_BK7258_AP_SUPERVISOR
  struct bk7258_ap_supervisor_status_s supervisor;
#endif
#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
  struct bk7258_ota_trial_status_s trial;
#endif
  volatile struct bk7258_ap_boot_state_s *ap = bk7258_ap_boot_state();
  volatile struct bk7258_ap_fault_state_s *fault = bk7258_ap_fault_state();
  volatile struct bk7258_cpu2_probe_state_s *cpu2 =
    bk7258_cpu2_probe_state();
  volatile struct bk7258_rptun_control_s *rptun = bk7258_rptun_control();
  int ret = bk7258_ota_inactive_geometry(&geometry);

  if (ret < 0)
    {
      fprintf(stderr, "bkota: active slot is invalid: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("active=%c inactive=%c cp=0x%08" PRIx32 "+0x%08" PRIx32
         " ap=0x%08" PRIx32 "+0x%08" PRIx32 "\n",
         geometry.active_slot == BK7258_BOOT_SLOT_PRIMARY ? 'A' : 'B',
         geometry.inactive_slot == BK7258_BOOT_SLOT_PRIMARY ? 'A' : 'B',
         geometry.cp_raw_offset, geometry.cp_raw_size,
         geometry.ap_raw_offset, geometry.ap_raw_size);
  ret = bk7258_ota_get_active_pair(&pair);
  if (ret < 0)
    {
      printf("pair=invalid error=%d\n", ret);
    }
  else
    {
      printf("pair=%s version=%u.%u.%u+%lu counter=%lu\n",
             bkota_pair_state(pair.state),
             (unsigned int)pair.version.major,
             (unsigned int)pair.version.minor,
             (unsigned int)pair.version.revision,
             (unsigned long)pair.version.build,
             (unsigned long)pair.security_counter);
    }
  printf("ap magic=%08" PRIx32 " version=%" PRIu32
         " state=%" PRIu32 " error=%" PRIu32
         " generation=%" PRIu32 " heartbeat=%" PRIu32 "\n",
         ap->magic, ap->version, ap->state, ap->error,
         ap->generation, ap->heartbeat);
  printf("ap event=%" PRIu32 " core=%" PRIu32 "/%" PRIu32
         " vtor=%08" PRIx32 "/%08" PRIx32
         " msp=%08" PRIx32 "/%08" PRIx32 " clock=%" PRIu32 "\n",
         ap->last_event, ap->local_core_id, ap->physical_core_id,
         ap->initial_vtor, ap->runtime_vtor,
         ap->initial_msp, ap->runtime_msp, ap->clock_hz);
  printf("ap fault magic=%08" PRIx32 " exception=%" PRIu32
         " error=%" PRIu32 " hfsr=%08" PRIx32
         " cfsr=%08" PRIx32 " pc=%08" PRIx32 " lr=%08" PRIx32 "\n",
         fault->magic, fault->exception, fault->error,
         fault->hfsr, fault->cfsr, fault->stacked_pc, fault->stacked_lr);
  printf("cpu2 magic=%08" PRIx32 " state=%" PRIu32
         " error=%" PRIu32 " generation=%" PRIu32
         " heartbeat=%" PRIu32 " ready=%" PRIu32
         " online=%08" PRIx32 "\n",
         cpu2->magic, cpu2->state, cpu2->error, cpu2->generation,
         cpu2->heartbeat, cpu2->secondary_ready, cpu2->online_mask);
  printf("rptun magic=%08" PRIx32 " version=%" PRIu32
         " state=%" PRIu32 " error=%" PRIu32
         " generation=%" PRIu32 " flags=%08" PRIx32
         " heartbeat=%" PRIu32 "/%" PRIu32 "\n",
         rptun->magic, rptun->version, rptun->state, rptun->error,
         rptun->generation, rptun->flags,
         rptun->cp_heartbeat, rptun->ap_heartbeat);

  ret = bk7258_ota_rpmsg_manager_status(&manager, 5000u);
  if (ret < 0)
    {
      printf("manager=unavailable error=%d\n", ret);
    }
  else
    {
      printf("manager state=%u phase=%u image=%u progress=%lu/%lu error=%ld\n",
             (unsigned int)manager.state, (unsigned int)manager.phase,
             (unsigned int)manager.image, (unsigned long)manager.completed,
             (unsigned long)manager.total, (long)manager.last_error);
    }

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  ret = bk7258_ap_supervisor_get_status(&supervisor);
  if (ret < 0)
    {
      printf("supervisor=unavailable error=%d\n", ret);
    }
  else
    {
      printf("supervisor state=%lu generation=%lu flags=%08lx "
             "age=%lu/%lu/%lu healthy=%lu sample=%lu sequence=%lu "
             "faults=%lu recoveries=%lu\n",
             (unsigned long)supervisor.state,
             (unsigned long)supervisor.generation,
             (unsigned long)supervisor.flags,
             (unsigned long)supervisor.primary_age_ms,
             (unsigned long)supervisor.secondary_age_ms,
             (unsigned long)supervisor.transport_age_ms,
             (unsigned long)supervisor.healthy_age_ms,
             (unsigned long)supervisor.sample_age_ms,
             (unsigned long)supervisor.sample_sequence,
             (unsigned long)supervisor.fault_count,
             (unsigned long)supervisor.recovery_count);
    }
#endif

#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
  ret = bk7258_ota_trial_get_status(&trial);
  if (ret < 0)
    {
      printf("trial=unavailable error=%d\n", ret);
    }
  else
    {
      printf("trial state=%lu slot=%lu version=%u.%u.%u+%lu counter=%lu "
             "generation=%lu sample=%lu elapsed=%lu stable=%lu confirm=%lu "
             "worker=%lu/%lu error=%ld\n",
             (unsigned long)trial.state,
             (unsigned long)trial.active_slot,
             (unsigned int)trial.image_version.major,
             (unsigned int)trial.image_version.minor,
             (unsigned int)trial.image_version.revision,
             (unsigned long)trial.image_version.build,
             (unsigned long)trial.security_counter,
             (unsigned long)trial.supervisor_generation,
             (unsigned long)trial.sample_sequence,
             (unsigned long)trial.elapsed_ms,
             (unsigned long)trial.stable_age_ms,
             (unsigned long)trial.confirm_age_ms,
             (unsigned long)trial.policy_age_ms,
             (unsigned long)trial.deadline_age_ms,
             (long)trial.last_error);
    }
#endif

  return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
  struct bk7258_ota_pair_snapshot_s pair;
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return bkota_status();
    }

  if (argc == 2 && strcmp(argv[1], "confirm") == 0)
    {
      ret = bk7258_ota_get_active_pair(&pair);
      if (ret == 0 && pair.state == BK7258_OTA_PAIR_CONFIRMED)
        {
          printf("bkota: active CP/AP pair already confirmed\n");
          return EXIT_SUCCESS;
        }
      if (ret == 0)
        {
          ret = bk7258_ota_confirm_pair(&pair);
        }
      if (ret == -EALREADY)
        {
          ret = 0;
        }
      if (ret < 0)
        {
          fprintf(stderr, "bkota: pair confirm failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("bkota: active CP/AP pair confirmed\n");
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "reboot") == 0)
    {
      printf("bkota: rebooting through the CP whole-device watchdog\n");
      fflush(stdout);
      bk7258_ota_system_reset();
    }

  if (argc == 3 && strcmp(argv[1], "apply-file") == 0)
    {
      ret = bk7258_ota_rpmsg_apply_file(
              argv[2], CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
      if (ret < 0)
        {
          fprintf(stderr, "bkota: AP file staging failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("bkota: OTA request accepted; use 'bkota status' for progress\n");
      return EXIT_SUCCESS;
    }

  if ((argc == 3 || argc == 4) && strcmp(argv[1], "apply-http") == 0)
    {
      ret = bk7258_ota_rpmsg_apply_http(
              argv[2], argc == 4 ? argv[3] : "",
              CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
      if (ret < 0)
        {
          fprintf(stderr, "bkota: AP HTTP staging failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("bkota: OTA request accepted; use 'bkota status' for progress\n");
      return EXIT_SUCCESS;
    }

  if (argc == 2 && strcmp(argv[1], "cancel") == 0)
    {
      ret = bk7258_ota_rpmsg_manager_cancel(5000u);
      if (ret < 0)
        {
          fprintf(stderr, "bkota: AP cancel failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("bkota: AP OTA cancel requested\n");
      return EXIT_SUCCESS;
    }

  fprintf(stderr, "usage: bkota status | bkota apply-file <AP-path> | "
                  "bkota apply-http <catalog-url> [AP-ca-path] | "
                  "bkota cancel | bkota confirm | bkota reboot\n");
  return EXIT_FAILURE;
}
