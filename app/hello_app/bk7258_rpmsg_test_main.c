/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/hello_app/
 * bk7258_rpmsg_test_main.c
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

#include <arch/chip/bk7258_rptun.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkrpmsgtest_u32(const char *text, uint32_t *value)
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

static uint64_t bkrpmsgtest_us_x1000(uint32_t cycles, uint32_t frequency)
{
  if (frequency == 0)
    {
      return 0;
    }

  return (uint64_t)cycles * 1000000000ull / frequency;
}

static void bkrpmsgtest_print_latency(const char *name, uint32_t cycles,
                                      uint32_t frequency)
{
  uint64_t scaled = bkrpmsgtest_us_x1000(cycles, frequency);

  printf(" %s_cycles=%" PRIu32 " %s_us=%" PRIu64 ".%03" PRIu64,
         name, cycles, name, scaled / 1000u, scaled % 1000u);
}

static void bkrpmsgtest_print_heap(
  const char *name, const struct bk7258_rpmsg_test_heap_result_s *heap)
{
  printf(" %s_arena=%" PRIu32 " %s_used=%" PRIu32
         " %s_free=%" PRIu32 " %s_largest=%" PRIu32
         " %s_alloc_blocks=%" PRIu32 " %s_free_blocks=%" PRIu32,
         name, heap->arena, name, heap->allocated_bytes,
         name, heap->free_bytes, name, heap->largest_free,
         name, heap->allocated_blocks, name, heap->free_blocks);
}

static int bkrpmsgtest_one(uint32_t count, uint32_t payload,
                           uint32_t flags, uint32_t timeout_ms)
{
  struct bk7258_rpmsg_test_result_s result;
  uint64_t elapsed_cycles;
  uint64_t wire_bytes;
  uint64_t throughput;
  uint32_t i;
  int ret;

  printf("BRPT BEGIN count=%" PRIu32 " payload=%" PRIu32
         " frame=%" PRIu32 " load=%" PRIu32
         " timeout_ms=%" PRIu32 "\n",
         count, payload, BK7258_RPMSG_TEST_WIRE_HEADER_SIZE + payload,
         (uint32_t)((flags & BK7258_RPMSG_TEST_FLAG_CPU0_LOAD) != 0),
         timeout_ms);

  ret = bk7258_rpmsg_test_run(count, payload, flags, timeout_ms, &result);
  if (result.magic != BK7258_RPMSG_TEST_RESULT_MAGIC)
    {
      printf("BRPT FAIL transport=%d count=%" PRIu32
             " payload=%" PRIu32 " load=%" PRIu32 "\n",
             ret, count, payload,
             (uint32_t)((flags & BK7258_RPMSG_TEST_FLAG_CPU0_LOAD) != 0));
      return ret < 0 ? ret : -EPROTO;
    }

  printf("BRPT RESULT gen=%" PRIu32 " run=%" PRIu32
         " status=%" PRId32 " controller_cpu=%" PRIu32
         " count=%" PRIu32 " payload=%" PRIu32
         " frame=%" PRIu32 " load=%" PRIu32
         " frequency=%" PRIu32 "\n",
         result.generation, result.run_id, result.status,
         result.controller_cpu, result.count, result.payload_size,
         result.frame_size,
         (uint32_t)((result.flags &
                     BK7258_RPMSG_TEST_FLAG_CPU0_LOAD) != 0),
         result.frequency);

  for (i = 0; i < 2; i++)
    {
      const struct bk7258_rpmsg_test_cpu_result_s *cpu = &result.cpu[i];

      printf("BRPT CPU slot=%" PRIu32 " sender_cpu=%" PRIu32
             " callback_mask=0x%08" PRIx32
             " sent=%" PRIu32 " received=%" PRIu32
             " errors=%" PRIu32,
             i, cpu->sender_cpu, cpu->callback_cpu_mask,
             cpu->sent, cpu->received, cpu->errors);
      bkrpmsgtest_print_latency("min", cpu->min_cycles,
                                result.frequency);
      bkrpmsgtest_print_latency("p50", cpu->p50_cycles,
                                result.frequency);
      bkrpmsgtest_print_latency("p95", cpu->p95_cycles,
                                result.frequency);
      bkrpmsgtest_print_latency("p99", cpu->p99_cycles,
                                result.frequency);
      bkrpmsgtest_print_latency("max", cpu->max_cycles,
                                result.frequency);
      printf(" total_cycles=%" PRIu64 "\n", cpu->total_cycles);
    }

  printf("BRPT HEAP");
  bkrpmsgtest_print_heap("start", &result.heap_start);
  bkrpmsgtest_print_heap("spawn", &result.heap_after_spawn);
  bkrpmsgtest_print_heap("report", &result.heap_report);
  printf("\n");
  printf("BRPT SPAWN target=%" PRIu32 " stage=%" PRIu32
         " status=%" PRId32 " workers_expected=%" PRIu32
         " workers_done=%" PRIu32 "\n",
         result.spawn_target, result.spawn_stage, result.spawn_status,
         result.workers_expected, result.workers_done);

  elapsed_cycles = result.cpu[0].total_cycles >
                   result.cpu[1].total_cycles ?
                   result.cpu[0].total_cycles :
                   result.cpu[1].total_cycles;
  wire_bytes = (uint64_t)result.count * result.frame_size * 4u;
  throughput = elapsed_cycles != 0 ?
               wire_bytes * result.frequency / elapsed_cycles : 0;
  printf("BRPT THROUGHPUT wire_bytes=%" PRIu64
         " approx_bytes_per_sec=%" PRIu64 "\n",
         wire_bytes, throughput);

  if (ret < 0 || result.status < 0 || result.controller_cpu != 0 ||
      result.count != count || result.payload_size != payload)
    {
      printf("BRPT FAIL gen=%" PRIu32 " run=%" PRIu32
             " ret=%d status=%" PRId32 "\n",
             result.generation, result.run_id, ret, result.status);
      return ret < 0 ? ret : result.status < 0 ? result.status : -EIO;
    }

  for (i = 0; i < 2; i++)
    {
      if (result.cpu[i].sender_cpu != i ||
          result.cpu[i].callback_cpu_mask != 1u ||
          result.cpu[i].sent != count ||
          result.cpu[i].received != count ||
          result.cpu[i].errors != 0 ||
          result.cpu[i].min_cycles == 0 ||
          result.cpu[i].max_cycles == 0)
        {
          printf("BRPT FAIL gen=%" PRIu32 " run=%" PRIu32
                 " invariant_cpu=%" PRIu32 "\n",
                 result.generation, result.run_id, i);
          return -EIO;
        }
    }

  printf("BRPT PASS gen=%" PRIu32 " run=%" PRIu32
         " count=%" PRIu32 " payload=%" PRIu32
         " load=%" PRIu32 "\n",
         result.generation, result.run_id, count, payload,
         (uint32_t)((flags & BK7258_RPMSG_TEST_FLAG_CPU0_LOAD) != 0));
  return 0;
}

static void bkrpmsgtest_usage(void)
{
  printf("usage:\n"
         "  bkrpmsgtest all [count=1000] [timeout_ms=30000]\n"
         "  bkrpmsgtest syslog [timeout_ms=30000]\n"
         "  bkrpmsgtest run <count> <payload> <idle|load> "
         "[timeout_ms=30000]\n"
         "payload range: 1..%u (wire frame max %u)\n",
         BK7258_RPMSG_TEST_MAX_PAYLOAD,
         BK7258_RPMSG_TEST_FRAME_SIZE);
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
    BK7258_RPMSG_TEST_MAX_PAYLOAD
  };
  uint32_t count = 1000;
  uint32_t payload;
  uint32_t timeout_ms = 30000;
  uint32_t flags;
  uint32_t i;
  uint32_t j;
  int ret;

  if (argc < 2 || strcmp(argv[1], "all") == 0)
    {
      if (argc >= 3 && bkrpmsgtest_u32(argv[2], &count) < 0)
        {
          bkrpmsgtest_usage();
          return EXIT_FAILURE;
        }

      if (argc >= 4 && bkrpmsgtest_u32(argv[3], &timeout_ms) < 0)
        {
          bkrpmsgtest_usage();
          return EXIT_FAILURE;
        }

      if (argc > 4 || count == 0 ||
          count > BK7258_RPMSG_TEST_MAX_COUNT)
        {
          bkrpmsgtest_usage();
          return EXIT_FAILURE;
        }

      for (j = 0; j < 2; j++)
        {
          flags = j == 0 ? 0 : BK7258_RPMSG_TEST_FLAG_CPU0_LOAD;
          for (i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++)
            {
              ret = bkrpmsgtest_one(count, payloads[i], flags,
                                    timeout_ms);
              if (ret < 0)
                {
                  return EXIT_FAILURE;
                }
            }
        }

      printf("BRPT SUITE PASS runs=6 count=%" PRIu32 "\n", count);
      return EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "syslog") == 0)
    {
      if (argc > 3 ||
          (argc == 3 && bkrpmsgtest_u32(argv[2], &timeout_ms) < 0))
        {
          bkrpmsgtest_usage();
          return EXIT_FAILURE;
        }

      ret = bkrpmsgtest_one(1u, 1u,
                            BK7258_RPMSG_TEST_FLAG_SYSLOG_PROBE,
                            timeout_ms);
      if (ret >= 0)
        {
          printf("BRPT SYSLOG PROBE SENT\n");
        }

      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (strcmp(argv[1], "run") != 0 || argc < 5 || argc > 6 ||
      bkrpmsgtest_u32(argv[2], &count) < 0 ||
      bkrpmsgtest_u32(argv[3], &payload) < 0)
    {
      bkrpmsgtest_usage();
      return EXIT_FAILURE;
    }

  if (strcmp(argv[4], "idle") == 0)
    {
      flags = 0;
    }
  else if (strcmp(argv[4], "load") == 0)
    {
      flags = BK7258_RPMSG_TEST_FLAG_CPU0_LOAD;
    }
  else
    {
      bkrpmsgtest_usage();
      return EXIT_FAILURE;
    }

  if (argc == 6 && bkrpmsgtest_u32(argv[5], &timeout_ms) < 0)
    {
      bkrpmsgtest_usage();
      return EXIT_FAILURE;
    }

  ret = bkrpmsgtest_one(count, payload, flags, timeout_ms);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
