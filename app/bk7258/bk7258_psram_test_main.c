/****************************************************************************
 * app/bk7258/bk7258_psram_test_main.c
 * bk7258_psram_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_psram.h>

#ifdef CONFIG_BK7258_AP_CONTROL
#  include <arch/chip/bk7258_amp.h>
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkpsramtest_u32(const char *text, uint32_t *value)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(text, &end, 0);
  if (errno != 0 || text[0] == '\0' || *end != '\0' ||
      parsed > UINT32_MAX)
    {
      return -EINVAL;
    }

  *value = (uint32_t)parsed;
  return OK;
}

static void bkpsramtest_usage(void)
{
  printf("usage:\n"
         "  bkpsramtest info\n"
         "  bkpsramtest heap [iterations=%u]\n"
         "  bkpsramtest all  [iterations=%u]\n"
         "The destructive raw-capacity test is boot-only.\n",
         CONFIG_BK7258_PSRAM_TEST_ITERATIONS,
         CONFIG_BK7258_PSRAM_TEST_ITERATIONS);
}

#ifdef CONFIG_BK7258_AP_CONTROL
static bool bkpsramtest_ap_result(
  const struct bk7258_ap_boot_state_s *state,
  struct bk7258_psram_test_result_s *result)
{
  const volatile uint32_t *source;
  uint32_t *destination = (uint32_t *)result;
  uintptr_t address =
    state->reserved[BK7258_PSRAM_AP_RESERVED_RESULT];
  uint32_t index;

  if (state->reserved[BK7258_PSRAM_AP_RESERVED_MAGIC] !=
        BK7258_PSRAM_AP_RESULT_READY ||
      (address & (sizeof(uint32_t) - 1u)) != 0u ||
      address < BK7258_AP_RAM_BASE ||
      address > BK7258_SHARED_RAM_BASE - sizeof(*result))
    {
      return false;
    }

  source = (const volatile uint32_t *)address;
  __asm volatile ("dmb sy" ::: "memory");
  for (index = 0; index < sizeof(*result) / sizeof(uint32_t); index++)
    {
      destination[index] = source[index];
    }

  __asm volatile ("dmb sy" ::: "memory");
  return true;
}
#endif

static int bkpsramtest_info(void)
{
  struct bk7258_psram_info_s info;
  int ret;

  ret = bk7258_psram_get_info(&info);
  if (ret < 0)
    {
      printf("BPSR INFO FAIL ret=%d\n", ret);
      return ret;
    }

  printf("BPSR INFO status=%" PRId32 " ready=%" PRIu32
         " id=%04" PRIx32 " config=%04" PRIx32
         " capacity=%" PRIu32
         " heap=%08" PRIx32 "+%" PRIu32
         " total=%" PRIu32 " free=%" PRIu32
         " minfree=%" PRIu32 " mpu=%" PRIu32 "\n",
         info.init_status, info.ready, info.chip_id, info.config_value,
         info.capacity,
         info.heap_base, info.heap_size, info.heap_total, info.heap_free,
         info.heap_minimum_free, info.mpu_valid);
  printf("BPSR RAW runs=%" PRIu32 " passes=%" PRIu32
         " fail=%08" PRIx32 " expected=%08" PRIx32
         " actual=%08" PRIx32 "\n",
         info.boot_test_runs, info.boot_test_passes,
         info.boot_test_fail_address, info.boot_test_expected,
         info.boot_test_actual);

  if (info.init_status < 0 || !info.ready || !info.mpu_valid ||
      (info.chip_id != 0x8d08u && info.chip_id != 0x8d09u) ||
      (info.capacity != BK7258_PSRAM_8M_SIZE &&
       info.capacity != BK7258_PSRAM_16M_SIZE))
    {
      printf("BPSR INFO FAIL invariant\n");
      return -EIO;
    }

#ifdef CONFIG_BK7258_PSRAM_BOOT_TEST
  if (info.boot_test_runs != 1u || info.boot_test_passes != 1u ||
      info.boot_test_fail_address != 0u)
    {
      printf("BPSR INFO FAIL raw_gate\n");
      return -EIO;
    }
#endif

#ifdef CONFIG_BK7258_AP_CONTROL
  {
    struct bk7258_ap_boot_state_s state;
    struct bk7258_psram_test_result_s result;

    bk7258_ap_get_status(&state);
    printf("BPSR AP generation=%" PRIu32 " state=%" PRIu32
           " error=%" PRIu32 " heap=%08" PRIx32
           " gate=%08" PRIx32 "\n",
           state.generation, state.state, state.error,
           state.reserved[BK7258_PSRAM_AP_RESERVED_HEAP],
           state.reserved[BK7258_PSRAM_AP_RESERVED_GATE]);
    if (bkpsramtest_ap_result(&state, &result))
      {
        printf("BPSR APTEST status=%" PRId32
               " requested=%" PRIu32
               " completed=%" PRIu32 "/%" PRIu32
               " active=%" PRIu32 "/%" PRIu32
               " stage=%" PRIu32 "/%" PRIu32
               " errors=%" PRIu32 "/%" PRIu32
               " cpu=%" PRIu32 "/%" PRIu32
               " free=%" PRIu32 "->%" PRIu32 "\n",
               result.status, result.requested_iterations,
               result.completed[0], result.completed[1],
               result.active_iteration[0], result.active_iteration[1],
               result.stage[0], result.stage[1],
               result.errors[0], result.errors[1],
               result.observed_cpu[0], result.observed_cpu[1],
               result.free_before, result.free_after);
      }

    if (state.magic != BK7258_AP_BOOT_STATE_MAGIC ||
        state.state != BK7258_AP_STATE_READY ||
        state.error != BK7258_AP_ERROR_NONE ||
        state.reserved[BK7258_PSRAM_AP_RESERVED_HEAP] !=
          BK7258_PSRAM_AP_HEAP_BASE ||
        state.reserved[BK7258_PSRAM_AP_RESERVED_GATE] !=
          BK7258_PSRAM_AP_TEST_PASSED)
      {
        printf("BPSR INFO FAIL ap_gate\n");
        return -EIO;
      }
  }
#endif

  printf("BPSR INFO PASS\n");
  return OK;
}

static int bkpsramtest_heap(uint32_t iterations)
{
  struct bk7258_psram_test_result_s result;
  int ret;

  printf("BPSR HEAP BEGIN iterations=%" PRIu32 "\n", iterations);
  ret = bk7258_psram_heap_test(iterations, false, &result);
  printf("BPSR HEAP RESULT status=%" PRId32
         " requested=%" PRIu32 " completed=%" PRIu32
         " active=%" PRIu32 " stage=%" PRIu32
         " errors=%" PRIu32 " cpu=%" PRIu32
         " free=%" PRIu32 "->%" PRIu32 "\n",
         result.status, result.requested_iterations,
         result.completed[0], result.active_iteration[0], result.stage[0],
         result.errors[0], result.observed_cpu[0],
         result.free_before, result.free_after);

  if (ret < 0 || result.status < 0 ||
      result.completed[0] != iterations || result.errors[0] != 0 ||
      result.free_before != result.free_after)
    {
      printf("BPSR HEAP FAIL ret=%d\n", ret);
      return ret < 0 ? ret : -EIO;
    }

  printf("BPSR HEAP PASS iterations=%" PRIu32 "\n", iterations);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  uint32_t iterations = CONFIG_BK7258_PSRAM_TEST_ITERATIONS;
  int ret;

  if (argc < 2 || argc > 3)
    {
      bkpsramtest_usage();
      return EXIT_FAILURE;
    }

  if (argc == 3 &&
      (bkpsramtest_u32(argv[2], &iterations) < 0 ||
       iterations < 1u || iterations > 4096u))
    {
      bkpsramtest_usage();
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "info") == 0)
    {
      ret = argc == 2 ? bkpsramtest_info() : -EINVAL;
    }
  else if (strcmp(argv[1], "heap") == 0)
    {
      ret = bkpsramtest_heap(iterations);
    }
  else if (strcmp(argv[1], "all") == 0)
    {
      ret = bkpsramtest_info();
      if (ret >= 0)
        {
          ret = bkpsramtest_heap(iterations);
        }
    }
  else
    {
      bkpsramtest_usage();
      return EXIT_FAILURE;
    }

  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
