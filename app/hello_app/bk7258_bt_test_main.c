/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/hello_app/
 * bk7258_bt_test_main.c
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

#include <arch/chip/bk7258_bt_ipc.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkbttest_u32(const char *text, uint32_t *value)
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

static void bkbttest_usage(void)
{
  printf("usage:\n"
         "  bkbttest info [timeout_ms=10000]\n"
         "  bkbttest stats [timeout_ms=10000]\n"
         "  bkbttest scan [duration_ms=3000] [timeout_ms=10000]\n"
         "  bkbttest all  [duration_ms=3000] [timeout_ms=10000]\n"
         "  bkbttest n12v [duration_ms=3000] [timeout_ms=10000]\n"
         "scan duration: %u..%u ms, timeout: %u..%u ms\n",
         BK7258_BT_TEST_SCAN_MIN_MS, BK7258_BT_TEST_SCAN_MAX_MS,
         BK7258_BT_TEST_TIMEOUT_MIN_MS, BK7258_BT_TEST_TIMEOUT_MAX_MS);
}

static void bkbttest_print_bytes(const uint8_t *data, uint32_t length)
{
  uint32_t i;

  for (i = 0; i < length; i++)
    {
      printf("%s%02x", i == 0 ? "" : " ", data[i]);
    }
}

static int bkbttest_one(enum bk7258_bt_test_operation_e operation,
                        uint32_t duration_ms, uint32_t timeout_ms,
                        bool require_n12v)
{
  struct bk7258_bt_test_result_s result;
  const char *name = operation == BK7258_BT_TEST_OPERATION_INFO ? "info" :
                     operation == BK7258_BT_TEST_OPERATION_SCAN ? "scan" :
                     "stats";
  int ret;

  printf("BBTT BEGIN operation=%s duration_ms=%" PRIu32
         " timeout_ms=%" PRIu32 "\n", name, duration_ms, timeout_ms);
  ret = bk7258_bt_test_run(operation, duration_ms, timeout_ms, &result);
  if (result.magic != BK7258_BT_TEST_RESULT_MAGIC)
    {
      printf("BBTT FAIL operation=%s transport=%d\n", name, ret);
      return ret < 0 ? ret : -EPROTO;
    }

  printf("BBTT RESULT gen=%" PRIu32 " sequence=%" PRIu32
         " operation=%s status=%" PRId32 " worker_cpu=%" PRIu32 "\n",
         result.generation, result.sequence, name, result.status,
         result.worker_cpu);
  if (operation == BK7258_BT_TEST_OPERATION_STATS)
    {
      printf("BBTT HCI command_tx=%" PRIu32 " acl_tx=%" PRIu32
             " event_rx=%" PRIu32 " acl_rx=%" PRIu32
             " invalid_rx=%" PRIu32 " receive_errors=%" PRIu32 "\n",
             result.hci.command_tx, result.hci.acl_tx,
             result.hci.event_rx, result.hci.acl_rx,
             result.hci.invalid_rx, result.hci.receive_errors);
      printf("BBTT HCI_LAST command=%08" PRIx32
             " acl_tx=%08" PRIx32 " event_header=%08" PRIx32
             " event_payload=%08" PRIx32 " acl_rx=%08" PRIx32
             " acl_rx_payload0=%08" PRIx32
             " acl_rx_payload1=%08" PRIx32 "\n",
             result.hci.last_command, result.hci.last_acl_tx,
             result.hci.last_event_header, result.hci.last_event_payload,
             result.hci.last_acl_rx, result.hci.last_acl_rx_payload0,
             result.hci.last_acl_rx_payload1);
      printf("BBTT HOST conn_rx=%" PRIu32 " l2cap_rx=%" PRIu32
             " l2cap_tx=%" PRIu32 " conn_tx=%" PRIu32
             " bt_send_acl=%" PRIu32 " bt_send_errors=%" PRIu32 "\n",
             result.hci.host_conn_rx, result.hci.host_l2cap_rx,
             result.hci.host_l2cap_tx, result.hci.host_conn_tx,
             result.hci.host_bt_send_acl,
             result.hci.host_bt_send_errors);
      printf("BBTT HOST_LAST l2cap_rx=%08" PRIx32
             " l2cap_tx=%08" PRIx32 "\n",
             result.hci.last_l2cap_rx, result.hci.last_l2cap_tx);
      printf("BBTT HOST_CTX gatt_connected=%" PRIu32
             " gatt_disconnected=%" PRIu32 " pdu_alloc=%" PRIu32
             " pdu_fail=%" PRIu32 "\n",
             result.hci.host_gatt_connected,
             result.hci.host_gatt_disconnected,
             result.hci.host_pdu_alloc, result.hci.host_pdu_fail);
      printf("BBTT HOST_COMPAT mtu_clamped=%" PRIu32
             " last_mtu=%" PRIu32 "->%" PRIu32 "\n",
             result.hci.host_mtu_clamped,
             result.hci.last_mtu_clamp >> 16,
             result.hci.last_mtu_clamp & 0xffffu);
      printf("BBTT HCI_CMD complete=%" PRIu32 " status=%" PRIu32
             " last_complete=%08" PRIx32 " last_status=%08" PRIx32
             "\n",
             result.hci.hci_cmd_complete, result.hci.hci_cmd_status,
             result.hci.last_cmd_complete, result.hci.last_cmd_status);
      printf("BBTT HCI_FLOW host_num_completed_dropped=%" PRIu32 "\n",
             result.hci.host_num_completed_dropped);
      printf("BBTT HCI_LINK connected=%" PRIu32
             " disconnected=%" PRIu32 " status=%" PRIu32
             " handle=%" PRIu32 " reason=%" PRIu32 "\n",
             result.hci.hci_le_connected,
             result.hci.hci_disconnected,
             result.hci.last_disconnection & 0xffu,
             result.hci.last_disconnection >> 8 & 0xffffu,
             result.hci.last_disconnection >> 24);
      printf("BBTT N13 state=%u last_error=%d worker_cpu=%u"
             " connected=%u disconnected=%u readvertised=%u"
             " queue_full=%u\n",
             (unsigned int)result.gatt.state,
             (int)result.gatt.last_error,
             (unsigned int)result.gatt.worker_cpu,
             (unsigned int)result.gatt.connected,
             (unsigned int)result.gatt.disconnected,
             (unsigned int)result.gatt.readvertised,
             (unsigned int)result.gatt.queue_full);

      {
        uint32_t trace_total = result.hci.host_att_trace_sequence;
        uint32_t trace_count = trace_total > BK7258_BT_ATT_TRACE_DEPTH ?
                                 BK7258_BT_ATT_TRACE_DEPTH : trace_total;
        uint32_t trace_sequence = trace_total - trace_count;

        for (; trace_sequence < trace_total; trace_sequence++)
          {
            const struct bk7258_bt_att_trace_s *trace =
              &result.hci.host_att_trace[
                trace_sequence % BK7258_BT_ATT_TRACE_DEPTH];
            uint32_t length =
              trace->meta >> BK7258_BT_ATT_TRACE_LENGTH_SHIFT &
              BK7258_BT_ATT_TRACE_LENGTH_MASK;
            uint32_t cid = trace->meta & BK7258_BT_ATT_TRACE_CID_MASK;

            printf("BBTT ATT_TRACE sequence=%" PRIu32
                   " direction=%s cid=%04" PRIx32
                   " length=%" PRIu32 " data0=%08" PRIx32
                   " data1=%08" PRIx32 "\n",
                   trace_sequence,
                   trace->meta & BK7258_BT_ATT_TRACE_TX ? "tx" : "rx",
                   cid, length, trace->data0, trace->data1);
          }
      }
    }
  else
    {
      printf("BBTT INFO bdaddr=%02x:%02x:%02x:%02x:%02x:%02x"
             " valid=%u fallback=%u acl_mtu=%" PRIu32
             " acl_buffers=%" PRIu32 "\n",
             result.bdaddr[5], result.bdaddr[4], result.bdaddr[3],
             result.bdaddr[2], result.bdaddr[1], result.bdaddr[0],
             result.address_valid, result.address_fallback,
             result.acl_mtu, result.acl_buffers);
      printf("BBTT FEATURES br=");
      bkbttest_print_bytes(result.features, sizeof(result.features));
      printf(" le=");
      bkbttest_print_bytes(result.le_features, sizeof(result.le_features));
      printf("\n");
    }

  if (operation == BK7258_BT_TEST_OPERATION_SCAN)
    {
      printf("BBTT SCAN duration_ms=%" PRIu32 " results=%" PRIu32,
             result.scan_duration_ms, result.scan_results);
      if (result.scan_results > 0)
        {
          printf(" selected=%u n12v_match=%u"
                 " addr=%02x:%02x:%02x:%02x:%02x:%02x"
                 " type=%u rssi=%d adv_type=%u adv_len=%u data=",
                 result.selected_index, result.n12v_payload_match,
                 result.first_addr[5], result.first_addr[4],
                 result.first_addr[3], result.first_addr[2],
                 result.first_addr[1], result.first_addr[0],
                 result.first_addr_type, result.first_rssi,
                 result.first_adv_type, result.first_adv_len);
          bkbttest_print_bytes(result.first_adv_data,
                               result.first_adv_len);
        }

      printf("\n");
    }

  if (ret < 0 || result.status < 0 || result.worker_cpu != 0 ||
      (operation != BK7258_BT_TEST_OPERATION_STATS &&
       (!result.address_valid || result.address_fallback)) ||
      (operation == BK7258_BT_TEST_OPERATION_SCAN &&
       (result.scan_results == 0 ||
        (require_n12v && !result.n12v_payload_match))))
    {
      printf("BBTT FAIL operation=%s ret=%d status=%" PRId32
             " valid=%u fallback=%u results=%" PRIu32
             " n12v_match=%u\n",
             name, ret, result.status, result.address_valid,
             result.address_fallback, result.scan_results,
             result.n12v_payload_match);
      if (ret < 0)
        {
          return ret;
        }

      if (result.status < 0)
        {
          return result.status;
        }

      return -EADDRNOTAVAIL;
    }

  printf("BBTT PASS operation=%s gen=%" PRIu32
         " sequence=%" PRIu32 "\n",
         name, result.generation, result.sequence);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  uint32_t duration_ms = 3000;
  uint32_t timeout_ms = 10000;
  bool run_info;
  bool run_scan;
  bool run_stats;
  bool require_n12v;
  int info_ret = OK;
  int scan_ret = OK;
  int stats_ret = OK;

  if (argc < 2)
    {
      bkbttest_usage();
      return EXIT_FAILURE;
    }

  require_n12v = strcmp(argv[1], "n12v") == 0;
  run_stats = strcmp(argv[1], "stats") == 0;
  run_info = strcmp(argv[1], "info") == 0 || strcmp(argv[1], "all") == 0 ||
             require_n12v;
  run_scan = strcmp(argv[1], "scan") == 0 || strcmp(argv[1], "all") == 0 ||
             require_n12v;
  if (!run_info && !run_scan && !run_stats)
    {
      bkbttest_usage();
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "info") == 0 || run_stats)
    {
      if (argc > 3 ||
          (argc == 3 && bkbttest_u32(argv[2], &timeout_ms) < 0))
        {
          bkbttest_usage();
          return EXIT_FAILURE;
        }
    }
  else if (argc > 4 ||
           (argc >= 3 && bkbttest_u32(argv[2], &duration_ms) < 0) ||
           (argc == 4 && bkbttest_u32(argv[3], &timeout_ms) < 0))
    {
      bkbttest_usage();
      return EXIT_FAILURE;
    }

  if (timeout_ms < BK7258_BT_TEST_TIMEOUT_MIN_MS ||
      timeout_ms > BK7258_BT_TEST_TIMEOUT_MAX_MS ||
      (run_scan &&
       (duration_ms < BK7258_BT_TEST_SCAN_MIN_MS ||
        duration_ms > BK7258_BT_TEST_SCAN_MAX_MS ||
        timeout_ms <= duration_ms)))
    {
      bkbttest_usage();
      return EXIT_FAILURE;
    }

  if (run_info)
    {
      info_ret = bkbttest_one(BK7258_BT_TEST_OPERATION_INFO, 0,
                              timeout_ms, false);
    }

  if (run_scan)
    {
      scan_ret = bkbttest_one(BK7258_BT_TEST_OPERATION_SCAN, duration_ms,
                              timeout_ms, require_n12v);
    }

  if (run_stats)
    {
      stats_ret = bkbttest_one(BK7258_BT_TEST_OPERATION_STATS, 0,
                               timeout_ms, false);
    }

  if (info_ret < 0 || scan_ret < 0 || stats_ret < 0)
    {
      if (run_stats)
        {
          printf("BBTT SUITE FAIL stats=%d\n", stats_ret);
        }
      else
        {
          printf("BBTT SUITE FAIL info=%d scan=%d\n", info_ret, scan_ret);
        }

      return EXIT_FAILURE;
    }

  if (run_stats)
    {
      printf("BBTT SUITE PASS stats=1\n");
    }
  else
    {
      printf("BBTT SUITE PASS info=%u scan=%u\n", run_info, run_scan);
    }

  return EXIT_SUCCESS;
}
