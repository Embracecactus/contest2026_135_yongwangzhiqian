/****************************************************************************
 * chips/bk7258/include/bk7258_ap_smp_diag.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diagnostic-only AP SMP exclusive-access stress and placement audit.  This
 * interface exists for hardware validation of the BK7258 dual-AP exclusive
 * monitor hypothesis; it is never part of a product configuration.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_SMP_DIAG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_SMP_DIAG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_BK7258_AP_SMP_DIAG

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SMP_DIAG_VERSION          1u
#define BK7258_SMP_DIAG_MIN_TARGET_OPS   10000000ull

/* 80 vector entries = 16 system + 64 external interrupts = 16 IPR words. */

#define BK7258_SMP_DIAG_NVIC_IPR_WORDS   16u
#define BK7258_SMP_DIAG_SHPR_WORDS       3u
#define BK7258_SMP_DIAG_FAILURE_ENTRIES  16u

#define BK7258_SMP_DIAG_LOAD_RPMSG       (1u << 0)
#define BK7258_SMP_DIAG_LOAD_DMA         (1u << 1)

#define BK7258_SMP_DIAG_KIND_ATOMIC      1u
#define BK7258_SMP_DIAG_KIND_LOCK        2u
#define BK7258_SMP_DIAG_KIND_OWNER       3u
#define BK7258_SMP_DIAG_KIND_SEQUENCE    4u
#define BK7258_SMP_DIAG_KIND_LOCKWORD    5u

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_smp_diag_class_e
{
  BK7258_SMP_DIAG_CLASS_SAFE = 0,  /* official 0x2800xxxx lock window */
  BK7258_SMP_DIAG_CLASS_SRAM_2806, /* ordinary AP SRAM 0x2806xxxx */
  BK7258_SMP_DIAG_CLASS_SRAM_2809, /* ordinary AP SRAM 0x2809xxxx */
  BK7258_SMP_DIAG_CLASS_PSRAM,     /* PSRAM aperture */
  BK7258_SMP_DIAG_CLASS_COUNT
};

struct bk7258_smp_diag_config_s
{
  uint64_t ops_per_class;          /* 0 selects the Kconfig target */
  uint32_t class_mask;             /* bit per enum bk7258_smp_diag_class_e */
  uint32_t load_mask;              /* BK7258_SMP_DIAG_LOAD_* */
};

struct bk7258_smp_diag_cpu_regs_s
{
  uint32_t valid;
  uint32_t cpu;
  uint32_t primask;
  uint32_t basepri;
  uint32_t irqsave_primask;        /* sampled inside up_irq_save() */
  uint32_t irqsave_basepri;
  uint32_t shpr[BK7258_SMP_DIAG_SHPR_WORDS];
  uint32_t ipr[BK7258_SMP_DIAG_NVIC_IPR_WORDS];
};

struct bk7258_smp_diag_class_result_s
{
  uint32_t address;
  int32_t  status;                 /* 0 run, -ENOENT window not reachable */
  uint64_t atomic_ops;
  uint64_t lock_ops;
  uint32_t atomic_failures;        /* lost update or non-monotonic old */
  uint32_t lock_failures;          /* lost update under the lock */
  uint32_t owner_violations;       /* two CPUs inside the critical section */
  uint32_t sequence_failures;      /* release sequence mismatch */
  uint32_t lockword_failures;      /* recursive owner/count not observed */
};

struct bk7258_smp_diag_witness_s
{
  uint8_t  cpu;
  uint8_t  address_class;
  uint16_t kind;
  uint32_t address;
  uint32_t observed;               /* old value seen by the acquiring op */
  uint32_t release_sequence;
};

struct bk7258_smp_diag_result_s
{
  uint32_t version;
  uint32_t size;
  int32_t  status;
  uint32_t target_met;             /* ops_per_class >= 10^7 */
  uint64_t ops_per_class;
  struct bk7258_smp_diag_cpu_regs_s cpu[2];
  struct bk7258_smp_diag_class_result_s cls[BK7258_SMP_DIAG_CLASS_COUNT];
  uint64_t dma_ops;
  uint32_t dma_failures;
  int32_t  dma_status;
  uint64_t rpmsg_ops;
  uint32_t rpmsg_failures;
  int32_t  rpmsg_status;
  uint32_t witness_head;
  uint32_t failure_count;
  uint32_t tcb_total;
  uint32_t tcb_psram;
  uint32_t group_psram;
  uint32_t stack_psram;
  uint32_t sync_psram_rejections;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Run the bounded diagnostic from a CPU0 task context.  It blocks until
 * every selected address class completes and may run for minutes at the
 * 10^7 target.  Interrupt-masking mechanisms are sampled, never changed.
 */

int bk7258_ap_smp_diag_run(const struct bk7258_smp_diag_config_s *config,
                           struct bk7258_smp_diag_result_s *result);

/* Read one entry from the rolling acquire-witness ring (failure == false)
 * or from the first-failures ring (failure == true).
 */

int bk7258_ap_smp_diag_witness(uint32_t index, bool failure,
                               struct bk7258_smp_diag_witness_s *witness);

#endif /* CONFIG_BK7258_AP_SMP_DIAG */
#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_SMP_DIAG_H */
