/****************************************************************************
 * app/testing/bk7258/bk7258_smp_diag.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diagnostic-only command for the BK7258 AP SMP exclusive-access stress.
 * Output is one "BKSMPDIAG key=value ..." record per line so host tooling
 * can parse it without depending on human-readable wording.
 *
 *   bksmpdiag [-n ops] [-c class_mask] [-l load_mask] [-w]
 *     -n  operations per CPU per class (0 = Kconfig target)
 *     -c  bit0 safe, bit1 SRAM 0x2806, bit2 SRAM 0x2809, bit3 PSRAM
 *     -l  bit0 RPMsg, bit1 DMA
 *     -w  dump witness and first-failure rings
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arch/chip/bk7258_ap_smp_diag.h>

static void bksmpdiag_dump_ring(bool failure)
{
  struct bk7258_smp_diag_witness_s witness;
  uint32_t limit = failure ? BK7258_SMP_DIAG_FAILURE_ENTRIES :
                   CONFIG_BK7258_AP_SMP_DIAG_WITNESS_ENTRIES;
  uint32_t index;

  /* Indices are split per CPU; unused slots return -ENOENT. */

  for (index = 0; index < limit; index++)
    {
      if (bk7258_ap_smp_diag_witness(index, failure, &witness) != 0)
        {
          continue;
        }

      printf("BKSMPDIAG %s index=%" PRIu32 " cpu=%u class=%u kind=%u "
             "address=0x%08" PRIx32 " observed=0x%08" PRIx32
             " release_sequence=%" PRIu32 "\n",
             failure ? "failure" : "witness", index,
             (unsigned int)witness.cpu,
             (unsigned int)witness.address_class,
             (unsigned int)witness.kind, witness.address,
             witness.observed, witness.release_sequence);
    }
}

int main(int argc, char *argv[])
{
  static struct bk7258_smp_diag_result_s result;
  struct bk7258_smp_diag_config_s config;
  bool witness = false;
  unsigned int cpu;
  unsigned int index;
  int option;
  int ret;

  memset(&config, 0, sizeof(config));
  config.class_mask = (1u << BK7258_SMP_DIAG_CLASS_COUNT) - 1u;
  while ((option = getopt(argc, argv, "n:c:l:w")) != -1)
    {
      switch (option)
        {
          case 'n':
            config.ops_per_class = strtoull(optarg, NULL, 0);
            break;

          case 'c':
            config.class_mask = (uint32_t)strtoul(optarg, NULL, 0);
            break;

          case 'l':
            config.load_mask = (uint32_t)strtoul(optarg, NULL, 0);
            break;

          case 'w':
            witness = true;
            break;

          default:
            fprintf(stderr, "usage: bksmpdiag [-n ops] [-c mask] "
                            "[-l mask] [-w]\n");
            return EXIT_FAILURE;
        }
    }

  ret = bk7258_ap_smp_diag_run(&config, &result);
  if (ret < 0)
    {
      printf("BKSMPDIAG run error=%d\n", ret);
      return EXIT_FAILURE;
    }

  printf("BKSMPDIAG summary version=%" PRIu32 " status=%" PRId32
         " ops_per_class=%" PRIu64 " target_met=%" PRIu32
         " witness=%" PRIu32 " failures=%" PRIu32 "\n",
         result.version, result.status, result.ops_per_class,
         result.target_met, result.witness_head, result.failure_count);

  for (cpu = 0; cpu < 2; cpu++)
    {
      const struct bk7258_smp_diag_cpu_regs_s *regs = &result.cpu[cpu];

      printf("BKSMPDIAG cpu=%u valid=%" PRIu32 " primask=%" PRIu32
             " basepri=0x%02" PRIx32 " irqsave_primask=%" PRIu32
             " irqsave_basepri=0x%02" PRIx32 " shpr=%08" PRIx32
             ",%08" PRIx32 ",%08" PRIx32 "\n",
             cpu, regs->valid, regs->primask, regs->basepri,
             regs->irqsave_primask, regs->irqsave_basepri,
             regs->shpr[0], regs->shpr[1], regs->shpr[2]);
      for (index = 0; index < BK7258_SMP_DIAG_NVIC_IPR_WORDS; index++)
        {
          printf("BKSMPDIAG cpu=%u ipr%u=0x%08" PRIx32 "\n",
                 cpu, index, regs->ipr[index]);
        }
    }

  for (index = 0; index < BK7258_SMP_DIAG_CLASS_COUNT; index++)
    {
      const struct bk7258_smp_diag_class_result_s *cls =
        &result.cls[index];

      printf("BKSMPDIAG class=%u status=%" PRId32 " address=0x%08" PRIx32
             " atomic_ops=%" PRIu64 " lock_ops=%" PRIu64
             " atomic_failures=%" PRIu32 " lock_failures=%" PRIu32
             " owner_violations=%" PRIu32 " sequence_failures=%" PRIu32
             " lockword_failures=%" PRIu32 "\n",
             index, cls->status, cls->address, cls->atomic_ops,
             cls->lock_ops, cls->atomic_failures, cls->lock_failures,
             cls->owner_violations, cls->sequence_failures,
             cls->lockword_failures);
    }

  printf("BKSMPDIAG load dma_ops=%" PRIu64 " dma_failures=%" PRIu32
         " dma_status=%" PRId32 " rpmsg_ops=%" PRIu64
         " rpmsg_failures=%" PRIu32 " rpmsg_status=%" PRId32 "\n",
         result.dma_ops, result.dma_failures, result.dma_status,
         result.rpmsg_ops, result.rpmsg_failures, result.rpmsg_status);
  printf("BKSMPDIAG placement tcbs=%" PRIu32 " tcb_psram=%" PRIu32
         " group_psram=%" PRIu32 " stack_psram=%" PRIu32
         " sync_psram_rejections=%" PRIu32 "\n",
         result.tcb_total, result.tcb_psram, result.group_psram,
         result.stack_psram, result.sync_psram_rejections);

  if (witness)
    {
      bksmpdiag_dump_ring(false);
      bksmpdiag_dump_ring(true);
    }

  return result.status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
