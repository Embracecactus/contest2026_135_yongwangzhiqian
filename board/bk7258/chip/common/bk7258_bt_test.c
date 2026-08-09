/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/
 * bk7258_bt_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Generation-safe control plane for N12 Bluetooth hardware validation.
 * RPMsg callbacks only copy requests/results and signal semaphores.  The AP
 * performs socket ioctls and bounded scan waits in one logical-CPU0 worker.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_BT_IPC_TEST

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef CONFIG_BK7258_AP_CORE
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <unistd.h>

#  include <netpacket/bluetooth.h>

#  include <nuttx/net/bluetooth.h>
#  include <nuttx/wireless/bluetooth/bt_ioctl.h>
#endif

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_bt_ipc.h>
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_BLE_GATT)
#  include <arch/chip/bk7258_ble_gatt.h>
#endif
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rptun.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_BT_TEST_EPT_NAME          "bk7258-bt-test"
#define BK7258_BT_TEST_WIRE_MAGIC        0x57544242u /* "BBTW" */
#define BK7258_BT_TEST_WIRE_VERSION      1u
#define BK7258_BT_TEST_SEND_TIMEOUT_MS   500u
#define BK7258_BT_TEST_CONNECT_MS        3000u

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_BT_TEST_REMOTE_NAME     "cp"
#  define BK7258_BT_TEST_IFNAME          "bnep0"
#  define BK7258_BT_TEST_MAX_RESULTS     8u
#  define BK7258_BT_TEST_AD_MANUFACTURER 0xffu
#  define BK7258_BT_TEST_N12V_AD_LENGTH  11u
#else
#  define BK7258_BT_TEST_REMOTE_NAME     "ap"
#endif

enum bk7258_bt_test_command_e
{
  BK7258_BT_TEST_COMMAND_START = 1,
  BK7258_BT_TEST_COMMAND_REPORT
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_bt_test_wire_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t operation;
  uint32_t scan_duration_ms;
  uint32_t timeout_ms;
  struct bk7258_bt_test_result_s result;
};

struct bk7258_bt_test_dev_s
{
  struct rpmsg_endpoint ept;
  bool initialized;
  bool endpoint_created;
  int connection_error;
#ifdef CONFIG_BK7258_AP_CORE
  sem_t request_sem;
  bool abort;
  bool busy;
  int request_status;
  uint32_t request_generation;
  uint32_t request_sequence;
  uint32_t request_operation;
  uint32_t request_scan_duration_ms;
  uint32_t request_timeout_ms;
#else
  sem_t report_sem;
  bool report_valid;
  uint32_t waiting_generation;
  uint32_t waiting_sequence;
  struct bk7258_bt_test_result_s report;
#endif
};

/****************************************************************************
 * Compile-time ABI Gates
 ****************************************************************************/

static_assert(offsetof(struct bk7258_bt_test_wire_s, result) == 32u,
              "BK7258 Bluetooth test wire header changed");
static_assert(sizeof(struct bk7258_bt_test_wire_s) <=
              BK7258_RPMSG_TEST_FRAME_SIZE,
              "BK7258 Bluetooth report cannot fit one RPMsg frame");
static_assert(sizeof(struct bk7258_bt_test_wire_s) ==
              BK7258_RPMSG_TEST_FRAME_SIZE,
              "BK7258 Bluetooth report must account for the full frame");

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_AP_SUPERVISOR)
static_assert(CONFIG_BK7258_BT_IPC_TEST_PRIORITY <
              CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
              "Bluetooth test worker must remain below AP heartbeat");
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_bt_test_dev_s g_bk7258_bt_test;

#ifndef CONFIG_BK7258_AP_CORE
static mutex_t g_bk7258_bt_test_lock = NXMUTEX_INITIALIZER;
static uint32_t g_bk7258_bt_test_next_sequence;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_bt_test_sem_init(sem_t *sem)
{
  int ret = nxsem_init(sem, 0, 0);

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(sem, SEM_PRIO_NONE);
    }
#endif

  return ret;
}

static void bk7258_bt_test_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static bool bk7258_bt_test_generation_ready(uint32_t generation)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  __asm volatile ("dmb sy" ::: "memory");
  return generation != 0 &&
         control->magic == BK7258_RPTUN_CONTROL_MAGIC &&
         control->version == BK7258_RPTUN_CONTROL_VERSION &&
         control->size == sizeof(*control) &&
         control->generation == generation &&
         control->state == BK7258_RPTUN_STATE_CONNECTED;
}

static bool bk7258_bt_test_endpoint_ready(void)
{
  struct bk7258_bt_test_dev_s *priv = &g_bk7258_bt_test;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         bk7258_bt_test_generation_ready(control->generation) &&
         is_rpmsg_ept_ready(&priv->ept);
}

static bool bk7258_bt_test_deadline_expired(clock_t started,
                                             uint32_t timeout_ms)
{
  return clock_systime_ticks() - started >= MSEC2TICK(timeout_ms);
}

static int bk7258_bt_test_send_bounded(const void *data, size_t length,
                                       uint32_t generation,
                                       uint32_t timeout_ms)
{
  struct bk7258_bt_test_dev_s *priv = &g_bk7258_bt_test;
  clock_t started = clock_systime_ticks();
  int ret = -ENOTCONN;

  for (; ; )
    {
      if (!bk7258_bt_test_endpoint_ready() ||
          !bk7258_bt_test_generation_ready(generation))
        {
          return -ENOTCONN;
        }

      ret = rpmsg_trysend(&priv->ept, data, length);
      if (ret >= 0)
        {
          return OK;
        }

      if (ret != -ENOMEM && ret != -EAGAIN)
        {
          return ret;
        }

      if (bk7258_bt_test_deadline_expired(started, timeout_ms))
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(1000);
    }
}

#ifdef CONFIG_BK7258_AP_CORE

static int bk7258_bt_test_neg_errno(void)
{
  int error = get_errno();

  return error > 0 ? -error : -EIO;
}

static void bk7258_bt_test_prepare_request(struct btreq_s *request)
{
  memset(request, 0, sizeof(*request));
  strlcpy(request->btr_name, BK7258_BT_TEST_IFNAME,
          sizeof(request->btr_name));
}

static bool bk7258_bt_test_address_valid(const uint8_t *address)
{
  bool all_zero = true;
  bool all_ff = true;
  unsigned int i;

  for (i = 0; i < 6; i++)
    {
      all_zero = all_zero && address[i] == 0;
      all_ff = all_ff && address[i] == UINT8_MAX;
    }

  /* bt_addr_t stores HCI byte order, so the public/OUI octet is val[5]. */

  return !all_zero && !all_ff && (address[5] & 1u) == 0;
}

static bool bk7258_bt_test_address_fallback(const uint8_t *address)
{
  static const uint8_t hci_order[6] =
    {
      0x19, 0x00, 0x00, 0x8c, 0x47, 0xc8
    };

  static const uint8_t sdk_order[6] =
    {
      0xc8, 0x47, 0x8c, 0x00, 0x00, 0x19
    };

  return memcmp(address, hci_order, sizeof(hci_order)) == 0 ||
         memcmp(address, sdk_order, sizeof(sdk_order)) == 0;
}

static int bk7258_bt_test_info(int socket_fd,
                               struct bk7258_bt_test_result_s *result)
{
  struct btreq_s request;
  int ret;

  bk7258_bt_test_prepare_request(&request);
  ret = ioctl(socket_fd, SIOCGBTINFO,
              (unsigned long)((uintptr_t)&request));
  if (ret < 0)
    {
      return bk7258_bt_test_neg_errno();
    }

  memcpy(result->bdaddr, request.btr_bdaddr.val, sizeof(result->bdaddr));
  result->address_valid = bk7258_bt_test_address_valid(result->bdaddr);
  result->address_fallback =
    bk7258_bt_test_address_fallback(result->bdaddr);
  result->acl_mtu = request.btr_acl_mtu;
  result->acl_buffers = request.btr_num_acl;

  bk7258_bt_test_prepare_request(&request);
  ret = ioctl(socket_fd, SIOCGBTFEAT,
              (unsigned long)((uintptr_t)&request));
  if (ret < 0)
    {
      return bk7258_bt_test_neg_errno();
    }

  memcpy(result->features, request.btr_features0,
         sizeof(result->features));

  bk7258_bt_test_prepare_request(&request);
  ret = ioctl(socket_fd, SIOCGBTLEFEAT,
              (unsigned long)((uintptr_t)&request));
  if (ret < 0)
    {
      return bk7258_bt_test_neg_errno();
    }

  memcpy(result->le_features, request.btr_features0,
         sizeof(result->le_features));
  return result->address_valid ? OK : -EADDRNOTAVAIL;
}

static bool bk7258_bt_test_n12v_match(
  const struct bt_scanresponse_s *response)
{
  static const uint8_t expected[] =
  {
    0xfe, 0xff, 0x4e, 0x31, 0x32, 0x56, 0x01, 0x02, 0x03, 0x04
  };

  uint8_t offset = 0;

  while (offset < response->sr_len)
    {
      uint8_t field_length = response->sr_data[offset];
      uint8_t remaining = response->sr_len - offset;

      if (field_length == 0)
        {
          break;
        }

      if (field_length >= remaining)
        {
          return false;
        }

      if (field_length >= BK7258_BT_TEST_N12V_AD_LENGTH &&
          response->sr_data[offset + 1] ==
            BK7258_BT_TEST_AD_MANUFACTURER &&
          memcmp(&response->sr_data[offset + 2], expected,
                 sizeof(expected)) == 0)
        {
          return true;
        }

      offset += field_length + 1;
    }

  return false;
}

static int bk7258_bt_test_scan(int socket_fd, uint32_t duration_ms,
                               struct bk7258_bt_test_result_s *result)
{
  struct bt_scanresponse_s responses[BK7258_BT_TEST_MAX_RESULTS];
  struct btreq_s request;
  bool scanning = false;
  uint8_t copy_length;
  uint8_t selected = 0;
  uint8_t i;
  int stop_ret = OK;
  int ret;

  bk7258_bt_test_prepare_request(&request);
  request.btr_dupenable = true;
  ret = ioctl(socket_fd, SIOCBTSCANSTART,
              (unsigned long)((uintptr_t)&request));
  if (ret < 0)
    {
      return bk7258_bt_test_neg_errno();
    }

  scanning = true;
  (void)nxsig_usleep(duration_ms * 1000u);

  memset(responses, 0, sizeof(responses));
  bk7258_bt_test_prepare_request(&request);
  request.btr_nrsp = BK7258_BT_TEST_MAX_RESULTS;
  request.btr_rsp = responses;
  ret = ioctl(socket_fd, SIOCBTSCANGET,
              (unsigned long)((uintptr_t)&request));
  if (ret < 0)
    {
      ret = bk7258_bt_test_neg_errno();
    }
  else
    {
      result->scan_results = request.btr_nrsp;
      if (request.btr_nrsp > 0)
        {
          for (i = 0; i < request.btr_nrsp; i++)
            {
              if (bk7258_bt_test_n12v_match(&responses[i]))
                {
                  selected = i;
                  result->n12v_payload_match = 1;
                  break;
                }
            }

          result->selected_index = selected;
          memcpy(result->first_addr, responses[selected].sr_addr.val,
                 sizeof(result->first_addr));
          result->first_addr_type = responses[selected].sr_addr.type;
          result->first_rssi = responses[selected].sr_rssi;
          result->first_adv_type = responses[selected].sr_type;
          copy_length = responses[selected].sr_len;
          if (copy_length > sizeof(result->first_adv_data))
            {
              copy_length = sizeof(result->first_adv_data);
            }

          result->first_adv_len = copy_length;
          memcpy(result->first_adv_data, responses[selected].sr_data,
                 copy_length);
        }
      else
        {
          ret = -ENODATA;
        }
    }

  if (scanning)
    {
      bk7258_bt_test_prepare_request(&request);
      if (ioctl(socket_fd, SIOCBTSCANSTOP,
                (unsigned long)((uintptr_t)&request)) < 0)
        {
          stop_ret = bk7258_bt_test_neg_errno();
        }
    }

  return ret < 0 ? ret : stop_ret;
}

static int bk7258_bt_test_execute(uint32_t operation,
                                  uint32_t scan_duration_ms,
                                  struct bk7258_bt_test_result_s *result)
{
#ifdef CONFIG_BK7258_BLE_GATT
  struct bk7258_ble_gatt_stats_s gatt;
#endif
  int socket_fd;
  int ret;

  if (operation == BK7258_BT_TEST_OPERATION_STATS)
    {
      ret = bk7258_bt_hci_get_stats(&result->hci);
#ifdef CONFIG_BK7258_BLE_GATT
      if (ret >= 0)
        {
          ret = bk7258_ble_gatt_get_stats(&gatt);
        }

      if (ret >= 0)
        {
          result->gatt.state = gatt.state > UINT8_MAX ?
                               UINT8_MAX : (uint8_t)gatt.state;
          result->gatt.worker_cpu = gatt.worker_cpu > UINT8_MAX ?
                                    UINT8_MAX : (uint8_t)gatt.worker_cpu;
          result->gatt.last_error = gatt.last_error > INT16_MAX ?
                                    INT16_MAX :
                                    gatt.last_error < INT16_MIN ?
                                    INT16_MIN : (int16_t)gatt.last_error;
          result->gatt.connected = gatt.connected > UINT16_MAX ?
                                   UINT16_MAX : (uint16_t)gatt.connected;
          result->gatt.disconnected = gatt.disconnected > UINT16_MAX ?
                                      UINT16_MAX :
                                      (uint16_t)gatt.disconnected;
          result->gatt.readvertised = gatt.readvertised > UINT16_MAX ?
                                      UINT16_MAX :
                                      (uint16_t)gatt.readvertised;
          result->gatt.queue_full = gatt.queue_full > UINT16_MAX ?
                                    UINT16_MAX :
                                    (uint16_t)gatt.queue_full;
        }
#endif
      return ret;
    }

  socket_fd = socket(PF_BLUETOOTH, SOCK_RAW, BTPROTO_L2CAP);
  if (socket_fd < 0)
    {
      return bk7258_bt_test_neg_errno();
    }

  ret = bk7258_bt_test_info(socket_fd, result);
  if (ret >= 0 && operation == BK7258_BT_TEST_OPERATION_SCAN)
    {
      ret = bk7258_bt_test_scan(socket_fd, scan_duration_ms, result);
    }

  close(socket_fd);
  return ret;
}

static void bk7258_bt_test_result_init(
  struct bk7258_bt_test_result_s *result, uint32_t generation,
  uint32_t sequence, uint32_t operation, uint32_t scan_duration_ms)
{
  int cpu = sched_getcpu();

  memset(result, 0, sizeof(*result));
  result->magic = BK7258_BT_TEST_RESULT_MAGIC;
  result->version = BK7258_BT_TEST_RESULT_VERSION;
  result->size = sizeof(*result);
  result->generation = generation;
  result->sequence = sequence;
  result->operation = operation;
  result->worker_cpu = cpu >= 0 ? (uint32_t)cpu : UINT32_MAX;
  result->scan_duration_ms = scan_duration_ms;
  result->gatt.worker_cpu = UINT8_MAX;
}

static FAR void *bk7258_bt_test_worker(FAR void *arg)
{
  struct bk7258_bt_test_dev_s *priv = &g_bk7258_bt_test;
  struct bk7258_bt_test_wire_s report;
  uint32_t generation;
  uint32_t sequence;
  uint32_t operation;
  uint32_t duration_ms;
  uint32_t timeout_ms;
  int status;

  (void)arg;
  for (; ; )
    {
      if (nxsem_wait_uninterruptible(&priv->request_sem) < 0)
        {
          continue;
        }

      if (__atomic_load_n(&priv->abort, __ATOMIC_ACQUIRE))
        {
          __atomic_store_n(&priv->busy, false, __ATOMIC_RELEASE);
          continue;
        }

      __asm volatile ("dmb sy" ::: "memory");
      generation = priv->request_generation;
      sequence = priv->request_sequence;
      operation = priv->request_operation;
      duration_ms = priv->request_scan_duration_ms;
      timeout_ms = priv->request_timeout_ms;
      status = priv->request_status;

      memset(&report, 0, sizeof(report));
      report.magic = BK7258_BT_TEST_WIRE_MAGIC;
      report.version = BK7258_BT_TEST_WIRE_VERSION;
      report.command = BK7258_BT_TEST_COMMAND_REPORT;
      report.generation = generation;
      report.sequence = sequence;
      report.operation = operation;
      report.scan_duration_ms = duration_ms;
      report.timeout_ms = timeout_ms;
      bk7258_bt_test_result_init(&report.result, generation, sequence,
                                 operation, duration_ms);

      if (status >= 0 && !bk7258_bt_test_generation_ready(generation))
        {
          status = -ESTALE;
        }

      if (status >= 0)
        {
          status = bk7258_bt_test_execute(operation, duration_ms,
                                          &report.result);
        }

      report.result.status = status;
      (void)bk7258_bt_test_send_bounded(
        &report, sizeof(report), generation,
        timeout_ms < BK7258_BT_TEST_SEND_TIMEOUT_MS ?
        timeout_ms : BK7258_BT_TEST_SEND_TIMEOUT_MS);
      __atomic_store_n(&priv->busy, false, __ATOMIC_RELEASE);
    }

  return NULL;
}

static int bk7258_bt_test_spawn_worker(void)
{
  pthread_attr_t attr;
  struct sched_param param;
  cpu_set_t cpuset = (cpu_set_t)1u;
  pthread_t thread;
  bool initialized = false;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      initialized = true;
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr,
                                      CONFIG_BK7258_BT_IPC_TEST_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = CONFIG_BK7258_BT_IPC_TEST_PRIORITY;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr, bk7258_bt_test_worker, NULL);
      if (ret == 0)
        {
          (void)pthread_setname_np(thread, "bk-bt-test");
        }
    }

  if (initialized)
    {
      (void)pthread_attr_destroy(&attr);
    }

  return ret == 0 ? OK : -ret;
}

static void bk7258_bt_test_send_immediate_error(
  const struct bk7258_bt_test_wire_s *request, int status)
{
  struct bk7258_bt_test_wire_s report;

  memset(&report, 0, sizeof(report));
  report.magic = BK7258_BT_TEST_WIRE_MAGIC;
  report.version = BK7258_BT_TEST_WIRE_VERSION;
  report.command = BK7258_BT_TEST_COMMAND_REPORT;
  report.generation = request->generation;
  report.sequence = request->sequence;
  report.operation = request->operation;
  report.scan_duration_ms = request->scan_duration_ms;
  report.timeout_ms = request->timeout_ms;
  bk7258_bt_test_result_init(&report.result, request->generation,
                             request->sequence, request->operation,
                             request->scan_duration_ms);
  report.result.status = status;
  (void)rpmsg_trysend(&g_bk7258_bt_test.ept, &report, sizeof(report));
}

#endif /* CONFIG_BK7258_AP_CORE */

static int bk7258_bt_test_ept_cb(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t length,
                                  uint32_t source, FAR void *priv_)
{
  struct bk7258_bt_test_dev_s *priv = priv_;
  struct bk7258_bt_test_wire_s *message = data;

  (void)ept;
  (void)source;
  if (message == NULL ||
      length < offsetof(struct bk7258_bt_test_wire_s, result) ||
      message->magic != BK7258_BT_TEST_WIRE_MAGIC ||
      message->version != BK7258_BT_TEST_WIRE_VERSION)
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (message->command == BK7258_BT_TEST_COMMAND_START &&
      length == offsetof(struct bk7258_bt_test_wire_s, result))
    {
      bool expected = false;
      int request_status = OK;

      if (!__atomic_compare_exchange_n(&priv->busy, &expected, true, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        {
          bk7258_bt_test_send_immediate_error(message, -EBUSY);
          return OK;
        }

      if ((message->operation != BK7258_BT_TEST_OPERATION_INFO &&
           message->operation != BK7258_BT_TEST_OPERATION_SCAN &&
           message->operation != BK7258_BT_TEST_OPERATION_STATS) ||
          message->timeout_ms < BK7258_BT_TEST_TIMEOUT_MIN_MS ||
          message->timeout_ms > BK7258_BT_TEST_TIMEOUT_MAX_MS ||
          ((message->operation == BK7258_BT_TEST_OPERATION_INFO ||
            message->operation == BK7258_BT_TEST_OPERATION_STATS) &&
           message->scan_duration_ms != 0) ||
          (message->operation == BK7258_BT_TEST_OPERATION_SCAN &&
           (message->scan_duration_ms < BK7258_BT_TEST_SCAN_MIN_MS ||
            message->scan_duration_ms > BK7258_BT_TEST_SCAN_MAX_MS)))
        {
          request_status = -EINVAL;
        }
      else if (!bk7258_bt_test_generation_ready(message->generation))
        {
          request_status = -ESTALE;
        }

      priv->request_generation = message->generation;
      priv->request_sequence = message->sequence;
      priv->request_operation = message->operation;
      priv->request_scan_duration_ms = message->scan_duration_ms;
      priv->request_timeout_ms = message->timeout_ms;
      priv->request_status = request_status;
      __asm volatile ("dmb sy" ::: "memory");
      (void)nxsem_post(&priv->request_sem);
      return OK;
    }
#else
  if (message->command == BK7258_BT_TEST_COMMAND_REPORT &&
      length == sizeof(*message) &&
      message->generation == priv->waiting_generation &&
      message->sequence == priv->waiting_sequence)
    {
      memcpy(&priv->report, &message->result, sizeof(priv->report));
      __asm volatile ("dmb sy" ::: "memory");
      priv->report_valid = true;
      (void)nxsem_post(&priv->report_sem);
      return OK;
    }
#endif

  return -ENOMSG;
}

static void bk7258_bt_test_device_created(FAR struct rpmsg_device *rdev,
                                           FAR void *priv_)
{
  struct bk7258_bt_test_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, BK7258_BT_TEST_REMOTE_NAME) != 0)
    {
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  priv->ept.priv = priv;
  priv->abort = false;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, BK7258_BT_TEST_EPT_NAME,
    RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, bk7258_bt_test_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
#else
  priv->connection_error = OK;
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_bt_test_ns_match(FAR struct rpmsg_device *rdev,
                                    FAR void *priv_, FAR const char *name,
                                    uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL &&
         strcmp(cpuname, BK7258_BT_TEST_REMOTE_NAME) == 0 &&
         strcmp(name, BK7258_BT_TEST_EPT_NAME) == 0;
}

static void bk7258_bt_test_ns_bind(FAR struct rpmsg_device *rdev,
                                   FAR void *priv_, FAR const char *name,
                                   uint32_t dest)
{
  struct bk7258_bt_test_dev_s *priv = priv_;

  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
    bk7258_bt_test_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
}
#endif

static void bk7258_bt_test_device_destroy(FAR struct rpmsg_device *rdev,
                                           FAR void *priv_)
{
  struct bk7258_bt_test_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, BK7258_BT_TEST_REMOTE_NAME) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  __atomic_store_n(&priv->abort, true, __ATOMIC_RELEASE);
  (void)nxsem_post(&priv->request_sem);
#else
  priv->report_valid = false;
  (void)nxsem_post(&priv->report_sem);
#endif

  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_bt_test_initialize(void)
{
  struct bk7258_bt_test_dev_s *priv = &g_bk7258_bt_test;
  int ret;

  if (priv->initialized)
    {
      return OK;
    }

  memset(priv, 0, sizeof(*priv));
  priv->connection_error = -ENOTCONN;
#ifdef CONFIG_BK7258_AP_CORE
  ret = bk7258_bt_test_sem_init(&priv->request_sem);
#else
  ret = bk7258_bt_test_sem_init(&priv->report_sem);
#endif
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = rpmsg_register_callback(
    priv, bk7258_bt_test_device_created, bk7258_bt_test_device_destroy,
    NULL, NULL);
#else
  ret = rpmsg_register_callback(
    priv, bk7258_bt_test_device_created, bk7258_bt_test_device_destroy,
    bk7258_bt_test_ns_match, bk7258_bt_test_ns_bind);
#endif
  if (ret < 0)
    {
#ifdef CONFIG_BK7258_AP_CORE
      nxsem_destroy(&priv->request_sem);
#else
      nxsem_destroy(&priv->report_sem);
#endif
      return ret;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = bk7258_bt_test_spawn_worker();
  if (ret < 0)
    {
      rpmsg_unregister_callback(
        priv, bk7258_bt_test_device_created,
        bk7258_bt_test_device_destroy, NULL, NULL);
      nxsem_destroy(&priv->request_sem);
      return ret;
    }
#endif

  priv->initialized = true;
  return OK;
}

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_bt_test_run(enum bk7258_bt_test_operation_e operation,
                       uint32_t scan_duration_ms, uint32_t timeout_ms,
                       struct bk7258_bt_test_result_s *result)
{
  struct bk7258_bt_test_dev_s *priv = &g_bk7258_bt_test;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_bt_test_wire_s request;
  clock_t connect_started;
  uint32_t waited = 0;
  uint32_t slice;
  int ret;

  if (result == NULL ||
      (operation != BK7258_BT_TEST_OPERATION_INFO &&
       operation != BK7258_BT_TEST_OPERATION_SCAN &&
       operation != BK7258_BT_TEST_OPERATION_STATS) ||
      timeout_ms < BK7258_BT_TEST_TIMEOUT_MIN_MS ||
      timeout_ms > BK7258_BT_TEST_TIMEOUT_MAX_MS ||
      ((operation == BK7258_BT_TEST_OPERATION_INFO ||
        operation == BK7258_BT_TEST_OPERATION_STATS) &&
       scan_duration_ms != 0) ||
      (operation == BK7258_BT_TEST_OPERATION_SCAN &&
       (scan_duration_ms < BK7258_BT_TEST_SCAN_MIN_MS ||
        scan_duration_ms > BK7258_BT_TEST_SCAN_MAX_MS)))
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  ret = nxmutex_lock(&g_bk7258_bt_test_lock);
  if (ret < 0)
    {
      return ret;
    }

  connect_started = clock_systime_ticks();
  while (!bk7258_bt_test_endpoint_ready() &&
         !bk7258_bt_test_deadline_expired(connect_started,
                                           BK7258_BT_TEST_CONNECT_MS))
    {
      nxsig_usleep(1000);
    }

  if (!bk7258_bt_test_endpoint_ready())
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -ENOTCONN;
      goto out;
    }

  bk7258_bt_test_flush_sem(&priv->report_sem);
  if (++g_bk7258_bt_test_next_sequence == 0)
    {
      g_bk7258_bt_test_next_sequence++;
    }

  priv->waiting_generation = control->generation;
  priv->waiting_sequence = g_bk7258_bt_test_next_sequence;
  priv->report_valid = false;
  priv->connection_error = OK;
  memset(&priv->report, 0, sizeof(priv->report));
  memset(&request, 0, offsetof(struct bk7258_bt_test_wire_s, result));
  request.magic = BK7258_BT_TEST_WIRE_MAGIC;
  request.version = BK7258_BT_TEST_WIRE_VERSION;
  request.command = BK7258_BT_TEST_COMMAND_START;
  request.generation = priv->waiting_generation;
  request.sequence = priv->waiting_sequence;
  request.operation = operation;
  request.scan_duration_ms = scan_duration_ms;
  request.timeout_ms = timeout_ms;

  ret = bk7258_bt_test_send_bounded(
    &request, offsetof(struct bk7258_bt_test_wire_s, result),
    priv->waiting_generation, BK7258_BT_TEST_SEND_TIMEOUT_MS);
  if (ret < 0)
    {
      goto out;
    }

  ret = -ETIMEDOUT;
  while (waited < timeout_ms)
    {
      slice = timeout_ms - waited;
      if (slice > 50u)
        {
          slice = 50u;
        }

      ret = nxsem_tickwait_uninterruptible(&priv->report_sem,
                                            MSEC2TICK(slice));
      if (ret >= 0)
        {
          break;
        }

      if (ret != -ETIMEDOUT)
        {
          goto out;
        }

      if (!bk7258_bt_test_endpoint_ready() ||
          !bk7258_bt_test_generation_ready(priv->waiting_generation))
        {
          ret = priv->connection_error < 0 ?
                priv->connection_error : -ENOTCONN;
          goto out;
        }

      waited += slice;
    }

  if (ret < 0)
    {
      goto out;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (!priv->report_valid)
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -EPROTO;
      goto out;
    }

  memcpy(result, &priv->report, sizeof(*result));
  if (result->magic != BK7258_BT_TEST_RESULT_MAGIC ||
      result->version != BK7258_BT_TEST_RESULT_VERSION ||
      result->size != sizeof(*result) ||
      result->generation != priv->waiting_generation ||
      result->sequence != priv->waiting_sequence ||
      result->operation != (uint32_t)operation)
    {
      ret = -EPROTO;
      goto out;
    }

  ret = result->status;

out:
  nxmutex_unlock(&g_bk7258_bt_test_lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_BT_IPC_TEST */
