/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/bk7258/
 * bk7258_rpmsgfs_test_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_rptun.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkrpmsgfstest_u32(const char *text, uint32_t *value)
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
  return 0;
}

static void bkrpmsgfstest_usage(void)
{
  printf("usage:\n"
         "  bkrpmsgfstest all [iterations=1] [timeout_ms=30000]\n"
         "  bkrpmsgfstest run <iterations> <payload> "
         "[timeout_ms=30000]\n"
         "payload range: 1..%u, iterations: 1..%u\n",
         BK7258_RPMSGFS_TEST_MAX_PAYLOAD,
         BK7258_RPMSGFS_TEST_MAX_ITERATIONS);
}

static int bkrpmsgfstest_one(uint32_t iterations, uint32_t payload,
                             uint32_t timeout_ms)
{
  struct bk7258_rpmsgfs_test_result_s result = {0};
  struct mallinfo before = mallinfo();
  struct mallinfo after;
  uint64_t expected_bytes = (uint64_t)iterations * payload;
  int ret;

  printf("BRFS BEGIN iterations=%" PRIu32 " payload=%" PRIu32
         " timeout_ms=%" PRIu32 "\n",
         iterations, payload, timeout_ms);
  ret = bk7258_rpmsgfs_test_run(iterations, payload, timeout_ms, &result);
  after = mallinfo();

  if (result.magic != BK7258_RPMSGFS_TEST_RESULT_MAGIC)
    {
      printf("BRFS FAIL transport=%d iterations=%" PRIu32
             " payload=%" PRIu32 "\n", ret, iterations, payload);
      return ret < 0 ? ret : -EPROTO;
    }

  printf("BRFS RESULT gen=%" PRIu32 " sequence=%" PRIu32
         " status=%" PRId32 " step=%" PRIu32
         " worker_cpu=%" PRIu32 " iterations=%" PRIu32 "/%" PRIu32
         " payload=%" PRIu32 " written=%" PRIu32 " read=%" PRIu32
         " checksum=%08" PRIx32 "/%08" PRIx32
         " dir_entries=%" PRIu32 "\n",
         result.generation, result.sequence, result.status, result.step,
         result.worker_cpu, result.iterations_completed,
         result.iterations_requested, result.payload_size,
         result.bytes_written, result.bytes_read,
         result.actual_checksum, result.expected_checksum,
         result.dir_entries);
  printf("BRFS AP_HEAP before_used=%" PRIu32
         " before_free=%" PRIu32 " before_largest=%" PRIu32
         " after_used=%" PRIu32 " after_free=%" PRIu32
         " after_largest=%" PRIu32 "\n",
         result.heap_before_used, result.heap_before_free,
         result.heap_before_largest, result.heap_after_used,
         result.heap_after_free, result.heap_after_largest);
  printf("BRFS CP_HEAP before_used=%u before_free=%u before_largest=%u"
         " after_used=%u after_free=%u after_largest=%u\n",
         before.uordblks, before.fordblks, before.mxordblk,
         after.uordblks, after.fordblks, after.mxordblk);

  if (ret < 0 || result.status < 0 ||
      result.size != sizeof(result) || result.worker_cpu != 0 ||
      result.iterations_requested != iterations ||
      result.iterations_completed != iterations ||
      result.payload_size != payload ||
      result.bytes_written != expected_bytes ||
      result.bytes_read != expected_bytes ||
      result.actual_checksum != result.expected_checksum ||
      result.step != BK7258_RPMSGFS_TEST_STEP_NONE)
    {
      printf("BRFS FAIL gen=%" PRIu32 " sequence=%" PRIu32
             " ret=%d status=%" PRId32 " step=%" PRIu32 "\n",
             result.generation, result.sequence, ret, result.status,
             result.step);
      return ret < 0 ? ret : result.status < 0 ? result.status : -EIO;
    }

  printf("BRFS PASS gen=%" PRIu32 " sequence=%" PRIu32
         " iterations=%" PRIu32 " payload=%" PRIu32 "\n",
         result.generation, result.sequence, iterations, payload);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  static const uint32_t payloads[] =
  {
    1u,
    64u,
    464u,
    BK7258_RPMSGFS_TEST_MAX_PAYLOAD
  };
  uint32_t iterations = 1;
  uint32_t payload;
  uint32_t timeout_ms = 30000;
  uint32_t i;
  int ret;

  if (argc < 2 || strcmp(argv[1], "all") == 0)
    {
      if (argc >= 3 && bkrpmsgfstest_u32(argv[2], &iterations) < 0)
        {
          bkrpmsgfstest_usage();
          return EXIT_FAILURE;
        }

      if (argc >= 4 && bkrpmsgfstest_u32(argv[3], &timeout_ms) < 0)
        {
          bkrpmsgfstest_usage();
          return EXIT_FAILURE;
        }

      if (argc > 4 || iterations == 0 ||
          iterations > BK7258_RPMSGFS_TEST_MAX_ITERATIONS)
        {
          bkrpmsgfstest_usage();
          return EXIT_FAILURE;
        }

      for (i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++)
        {
          ret = bkrpmsgfstest_one(iterations, payloads[i], timeout_ms);
          if (ret < 0)
            {
              printf("BRFS SUITE FAIL index=%" PRIu32 " ret=%d\n", i,
                     ret);
              return EXIT_FAILURE;
            }
        }

      printf("BRFS SUITE PASS runs=%u iterations=%" PRIu32 "\n",
             (unsigned int)(sizeof(payloads) / sizeof(payloads[0])),
             iterations);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "run") != 0 || argc < 4 || argc > 5 ||
      bkrpmsgfstest_u32(argv[2], &iterations) < 0 ||
      bkrpmsgfstest_u32(argv[3], &payload) < 0 ||
      (argc == 5 && bkrpmsgfstest_u32(argv[4], &timeout_ms) < 0) ||
      iterations == 0 ||
      iterations > BK7258_RPMSGFS_TEST_MAX_ITERATIONS ||
      payload == 0 || payload > BK7258_RPMSGFS_TEST_MAX_PAYLOAD)
    {
      bkrpmsgfstest_usage();
      return EXIT_FAILURE;
    }

  ret = bkrpmsgfstest_one(iterations, payload, timeout_ms);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
