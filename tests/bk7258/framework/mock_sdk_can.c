/****************************************************************************
 * tests/bk7258/framework/mock_sdk_can.c
 *
 * Host SDK mock for the bk7258 CAN lower-half (bk7258_can.c).  Model
 * described in mock_sdk_can.h.  This TU also provides the real-blocking
 * nxsem_* shims (mocks/nuttx_can/nuttx/semaphore.h) and the real-pthread
 * kthread surface the driver's rx thread needs, plus the NuttX CAN
 * upper-half records (can_receive/can_txdone) and clock_systime_timespec.
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/semaphore.h>
#include <nuttx/can/can.h>

#include "mock_sdk_can.h"

/* nxsem_* shims: counting semaphore on pthread mutex+cond ----------------- */

int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{
  int ret;

  (void)pshared;
  sem->count = value;
  ret = pthread_mutex_init(&sem->lock, NULL);
  if (ret != 0)
    {
      return ret;
    }

  ret = pthread_cond_init(&sem->cond, NULL);
  if (ret != 0)
    {
      pthread_mutex_destroy(&sem->lock);
    }

  return ret;
}

int nxsem_destroy(sem_t *sem)
{
  int ret = pthread_cond_destroy(&sem->cond);
  int mutex_ret = pthread_mutex_destroy(&sem->lock);

  return ret != 0 ? ret : mutex_ret;
}

int nxsem_post(sem_t *sem)
{
  pthread_mutex_lock(&sem->lock);
  sem->count++;
  pthread_cond_signal(&sem->cond);
  pthread_mutex_unlock(&sem->lock);
  return 0;
}

int nxsem_wait(sem_t *sem)
{
  pthread_mutex_lock(&sem->lock);
  while (sem->count == 0)
    {
      pthread_cond_wait(&sem->cond, &sem->lock);
    }

  sem->count--;
  pthread_mutex_unlock(&sem->lock);
  return 0;
}

int nxsem_reset(sem_t *sem, unsigned int value)
{
  pthread_mutex_lock(&sem->lock);
  sem->count = value;
  pthread_mutex_unlock(&sem->lock);
  return 0;
}

/* kthread shims: real detached worker ------------------------------------- */

struct mock_can_thread_s
{
  int (*entry)(int argc, FAR char *argv[]);
};

static void *mock_can_tramp(void *arg)
{
  struct mock_can_thread_s *t = arg;
  int (*entry)(int argc, FAR char *argv[]) = t->entry;

  free(t);
  entry(0, NULL);
  return NULL;
}

static int g_mock_can_kthreads;
static pthread_t g_mock_can_tid;
static bool g_mock_can_tid_valid;

int kthread_create(FAR const char *name, int priority, int stack_size,
                   int (*entry)(int, FAR char *argv[]), FAR char *argv[])
{
  pthread_t tid;
  struct mock_can_thread_s *t;

  (void)name;
  (void)priority;
  (void)stack_size;
  (void)argv;

  if (g_mock_can_tid_valid)
    {
      return -EBUSY;
    }

  t = malloc(sizeof(*t));
  if (t == NULL)
    {
      return -ENOMEM;
    }

  t->entry = entry;
  if (pthread_create(&tid, NULL, mock_can_tramp, t) != 0)
    {
      free(t);
      return -EIO;
    }

  g_mock_can_tid = tid;
  g_mock_can_tid_valid = true;
  g_mock_can_kthreads++;
  return 1;
}

int kthread_delete(pid_t pid)
{
  (void)pid;

  if (g_mock_can_tid_valid)
    {
      if (pthread_join(g_mock_can_tid, NULL) != 0)
        {
          return -EIO;
        }

      g_mock_can_tid_valid = false;
    }

  return 0;
}

int mock_can_kthread_count(void)
{
  return g_mock_can_kthreads;
}

/* clock shim --------------------------------------------------------------- */

int clock_systime_timespec(FAR struct timespec *ts)
{
  return clock_gettime(CLOCK_MONOTONIC, ts);
}

/* Call log + return injection ---------------------------------------------- */

static struct mock_can_log_s g_mock_can_log;
static int g_mock_can_ret[MOCK_CAN_FN_CTRL + 1];
static int g_mock_can_once_fn = -1;
static int g_mock_can_once_ret;

static void mock_can_record(enum mock_sdk_can_fn_e fn, uint32_t a0,
                            uint32_t a1)
{
  if (g_mock_can_log.count < MOCK_CAN_LOG_MAX)
    {
      g_mock_can_log.call[g_mock_can_log.count].fn = fn;
      g_mock_can_log.call[g_mock_can_log.count].a0 = a0;
      g_mock_can_log.call[g_mock_can_log.count].a1 = a1;
    }

  g_mock_can_log.count++;
}

static int mock_can_get_ret(enum mock_sdk_can_fn_e fn)
{
  if ((enum mock_sdk_can_fn_e)g_mock_can_once_fn == fn)
    {
      g_mock_can_once_fn = -1;
      return g_mock_can_once_ret;
    }

  return g_mock_can_ret[fn];
}

FAR struct mock_can_log_s *mock_sdk_can_log(void)
{
  return &g_mock_can_log;
}

int mock_sdk_can_calls(enum mock_sdk_can_fn_e fn)
{
  int i;
  int n = 0;

  for (i = 0; i < g_mock_can_log.count; i++)
    {
      if (g_mock_can_log.call[i].fn == fn)
        {
          n++;
        }
    }

  return n;
}

FAR struct mock_can_call_s *mock_sdk_can_call(int index)
{
  return &g_mock_can_log.call[index];
}

void mock_sdk_can_set_ret(enum mock_sdk_can_fn_e fn, int ret)
{
  g_mock_can_ret[fn] = ret;
}

void mock_sdk_can_set_ret_once(enum mock_sdk_can_fn_e fn, int ret)
{
  g_mock_can_once_fn = fn;
  g_mock_can_once_ret = ret;
}

/* SDK RX FIFO --------------------------------------------------------------- */

#define MOCK_CAN_FIFO_HEADER_BYTES  5
#define MOCK_CAN_ERR_OK             0
#define MOCK_CAN_ERR_TIMEOUT        (-4102)

struct mock_can_fifo_slot_s
{
  bool inuse;
  uint8_t size;
  uint32_t id;
  uint8_t payload[MOCK_CAN_FIFO_MAX_PAYLOAD];
  uint32_t consumed;
};

static struct mock_can_fifo_slot_s g_mock_can_fifo[MOCK_CAN_FIFO_SLOTS];
static int g_mock_can_fifo_head;
static int g_mock_can_fifo_tail;

void mock_sdk_can_rx_enqueue(uint32_t id, FAR const uint8_t *payload,
                             uint32_t size)
{
  struct mock_can_fifo_slot_s *slot;

  if (size > MOCK_CAN_FIFO_MAX_PAYLOAD)
    {
      return;
    }

  slot = &g_mock_can_fifo[g_mock_can_fifo_tail];
  slot->inuse = true;
  slot->size = size;
  slot->id = id;
  slot->consumed = 0;
  if (payload != NULL && size > 0)
    {
      memcpy(slot->payload, payload, size);
    }

  g_mock_can_fifo_tail = (g_mock_can_fifo_tail + 1) % MOCK_CAN_FIFO_SLOTS;
}

void mock_sdk_can_rx_flush(void)
{
  memset(g_mock_can_fifo, 0, sizeof(g_mock_can_fifo));
  g_mock_can_fifo_head = 0;
  g_mock_can_fifo_tail = 0;
}

bool mock_sdk_can_rx_empty(void)
{
  return g_mock_can_fifo_head == g_mock_can_fifo_tail;
}

/* Loopback mirror ----------------------------------------------------------- */

static bool g_mock_can_lbmi;

bool mock_sdk_can_lbmi(void)
{
  return g_mock_can_lbmi;
}

/* Last sent frame ----------------------------------------------------------- */

static uint32_t g_mock_can_last_tx_id;
static uint32_t g_mock_can_last_tx_size;
static uint8_t g_mock_can_last_tx_payload[MOCK_CAN_FIFO_MAX_PAYLOAD];

FAR const uint8_t *mock_sdk_can_last_tx_payload(void)
{
  return g_mock_can_last_tx_payload;
}

uint32_t mock_sdk_can_last_tx_size(void)
{
  return g_mock_can_last_tx_size;
}

uint32_t mock_sdk_can_last_tx_id(void)
{
  return g_mock_can_last_tx_id;
}

/* Bit-rate capture ----------------------------------------------------------- */

static uint32_t g_mock_can_last_s_speed;
static uint32_t g_mock_can_last_f_speed;

uint32_t mock_sdk_can_last_s_speed(void)
{
  return g_mock_can_last_s_speed;
}

uint32_t mock_sdk_can_last_f_speed(void)
{
  return g_mock_can_last_f_speed;
}

/* Upper-half records --------------------------------------------------------- */

static struct mock_can_upper_rx_s g_mock_can_upper[MOCK_CAN_UPPER_RING];
static unsigned int g_mock_can_upper_rx_count;
static unsigned int g_mock_can_upper_txdone_count;

FAR struct mock_can_upper_rx_s *mock_can_upper_rx(int index)
{
  return &g_mock_can_upper[index];
}

unsigned int mock_can_upper_rx_count(void)
{
  return g_mock_can_upper_rx_count;
}

unsigned int mock_can_upper_txdone_count(void)
{
  return g_mock_can_upper_txdone_count;
}

int can_receive(FAR struct can_dev_s *dev, FAR struct can_hdr_s *hdr,
                FAR uint8_t *data)
{
  struct mock_can_upper_rx_s *rec;

  (void)dev;

  if (g_mock_can_upper_rx_count < MOCK_CAN_UPPER_RING)
    {
      rec = &g_mock_can_upper[g_mock_can_upper_rx_count];
      memcpy(&rec->hdr, hdr, sizeof(rec->hdr));
      if (data != NULL && hdr->ch_dlc <= CAN_MAX_DLEN)
        {
          memcpy(rec->data, data, hdr->ch_dlc);
        }
    }

  g_mock_can_upper_rx_count++;
  return 0;
}

void can_txdone(FAR struct can_dev_s *dev)
{
  (void)dev;
  g_mock_can_upper_txdone_count++;
}

/* SDK ABI -------------------------------------------------------------------- */

static can_callback_des_t g_mock_can_rx_cb;
static can_callback_des_t g_mock_can_tx_cb;
static can_callback_des_t g_mock_can_err_cb;

int bk_can_driver_init(void)
{
  mock_can_record(MOCK_CAN_FN_DRIVER_INIT, 0, 0);
  return mock_can_get_ret(MOCK_CAN_FN_DRIVER_INIT);
}

int bk_can_driver_deinit(void)
{
  mock_can_record(MOCK_CAN_FN_DRIVER_DEINIT, 0, 0);
  return mock_can_get_ret(MOCK_CAN_FN_DRIVER_DEINIT);
}

int bk_can_receive(uint8_t *buf, uint32_t expect_size,
                   uint32_t *recv_size, uint32_t timeout)
{
  struct mock_can_fifo_slot_s *slot;

  (void)timeout;
  uint32_t remaining;
  int ret;

  ret = mock_can_get_ret(MOCK_CAN_FN_RECEIVE);
  if (ret != MOCK_CAN_ERR_OK)
    {
      mock_can_record(MOCK_CAN_FN_RECEIVE, expect_size, 0);
      *recv_size = 0;
      return ret;
    }

  if (mock_sdk_can_rx_empty())
    {
      mock_can_record(MOCK_CAN_FN_RECEIVE, expect_size, 0);
      *recv_size = 0;
      return MOCK_CAN_ERR_TIMEOUT;
    }

  /* The SDK FIFO is frame-atomic: a read either delivers the full
   * request or times out empty (the whole frame is always buffered
   * before the rx ISR fires).  No partial reads, which the driver
   * assumes. */
  slot = &g_mock_can_fifo[g_mock_can_fifo_head];
  remaining = MOCK_CAN_FIFO_HEADER_BYTES + slot->size - slot->consumed;
  if (remaining < expect_size)
    {
      mock_can_record(MOCK_CAN_FN_RECEIVE, expect_size, 0);
      *recv_size = 0;
      return MOCK_CAN_ERR_TIMEOUT;
    }

  if (buf != NULL)
    {
      uint32_t i;
      uint32_t h;

      for (i = 0, h = slot->consumed; i < expect_size; i++, h++)
        {
          if (h < 1)
            {
              buf[i] = slot->size;
            }
          else if (h < 5)
            {
              buf[i] = (uint8_t)(slot->id >> (8 * (h - 1)));
            }
          else
            {
              buf[i] = slot->payload[h - 5];
            }
        }
    }

  slot->consumed += expect_size;
  if (slot->consumed >= (uint32_t)(MOCK_CAN_FIFO_HEADER_BYTES + slot->size))
    {
      slot->inuse = false;
      g_mock_can_fifo_head =
        (g_mock_can_fifo_head + 1) % MOCK_CAN_FIFO_SLOTS;
    }

  *recv_size = expect_size;
  mock_can_record(MOCK_CAN_FN_RECEIVE, expect_size, expect_size);
  return MOCK_CAN_ERR_OK;
}

int bk_can_send_ptb(can_frame_s *frame)
{
  int ret;

  mock_can_record(MOCK_CAN_FN_SEND_PTB, frame->size, 0);
  ret = mock_can_get_ret(MOCK_CAN_FN_SEND_PTB);
  if (ret != MOCK_CAN_ERR_OK)
    {
      return ret;
    }

  g_mock_can_last_tx_id = frame->tag.id;
  g_mock_can_last_tx_size = frame->size;
  if (frame->data != NULL && frame->size > 0)
    {
      memcpy(g_mock_can_last_tx_payload, frame->data,
             frame->size > MOCK_CAN_FIFO_MAX_PAYLOAD
               ? MOCK_CAN_FIFO_MAX_PAYLOAD
               : frame->size);
    }

  return MOCK_CAN_ERR_OK;
}

void bk_can_register_isr_callback(can_callback_des_t *rx_cb,
                                  can_callback_des_t *tx_cb)
{
  mock_can_record(MOCK_CAN_FN_REGISTER_ISR, 0, 0);

  if (rx_cb != NULL && rx_cb->cb != NULL && tx_cb != NULL &&
      tx_cb->cb != NULL)
    {
      g_mock_can_rx_cb = *rx_cb;
      g_mock_can_tx_cb = *tx_cb;
    }
}

void bk_can_register_err_callback(can_callback_des_t *err_cb)
{
  mock_can_record(MOCK_CAN_FN_REGISTER_ERR, 0, 0);

  if (err_cb != NULL && err_cb->cb != NULL)
    {
      g_mock_can_err_cb = *err_cb;
    }
}

bk_err_t can_driver_bit_rate_config(can_bit_rate_e s_speed,
                                    can_bit_rate_e f_speed)
{
  mock_can_record(MOCK_CAN_FN_BITRATE_CONFIG,
                  (uint32_t)s_speed, (uint32_t)f_speed);
  g_mock_can_last_s_speed = s_speed;
  g_mock_can_last_f_speed = f_speed;
  return mock_can_get_ret(MOCK_CAN_FN_BITRATE_CONFIG);
}

bk_err_t bk_can_abort_ptb(void)
{
  mock_can_record(MOCK_CAN_FN_ABORT_PTB, 0, 0);
  return mock_can_get_ret(MOCK_CAN_FN_ABORT_PTB);
}

bk_err_t bk_can_abort_all(void)
{
  mock_can_record(MOCK_CAN_FN_ABORT_ALL, 0, 0);
  return mock_can_get_ret(MOCK_CAN_FN_ABORT_ALL);
}

void can_hal_set_lbmi(uint32_t value)
{
  mock_can_record(MOCK_CAN_FN_SET_LBMI, value, 0);
  g_mock_can_lbmi = value != 0;
}

uint32_t can_hal_get_lbmi(void)
{
  mock_can_record(MOCK_CAN_FN_GET_LBMI, 0, 0);
  return g_mock_can_lbmi ? 1 : 0;
}

bk_err_t can_hal_ctrl(uint32_t command, void *parameter)
{
  (void)parameter;
  mock_can_record(MOCK_CAN_FN_CTRL, command, 0);
  return mock_can_get_ret(MOCK_CAN_FN_CTRL);
}

/* ISR injection --------------------------------------------------------------- */

void mock_sdk_can_fire_rx_isr(void)
{
  if (g_mock_can_rx_cb.cb != NULL)
    {
      g_mock_can_rx_cb.cb(g_mock_can_rx_cb.param);
    }
}

void mock_sdk_can_fire_tx_isr(void)
{
  if (g_mock_can_tx_cb.cb != NULL)
    {
      g_mock_can_tx_cb.cb(g_mock_can_tx_cb.param);
    }
}

void mock_sdk_can_fire_err_isr(void)
{
  if (g_mock_can_err_cb.cb != NULL)
    {
      g_mock_can_err_cb.cb(g_mock_can_err_cb.param);
    }
}

/* Reset ------------------------------------------------------------------------ */

void mock_sdk_can_reset(void)
{
  g_mock_can_log.count = 0;
  memset(g_mock_can_ret, 0, sizeof(g_mock_can_ret));
  g_mock_can_once_fn = -1;
  mock_sdk_can_rx_flush();
  g_mock_can_lbmi = false;
  g_mock_can_kthreads = 0;
  g_mock_can_last_tx_id = 0;
  g_mock_can_last_tx_size = 0;
  memset(g_mock_can_last_tx_payload, 0, sizeof(g_mock_can_last_tx_payload));
  g_mock_can_last_s_speed = 0;
  g_mock_can_last_f_speed = 0;
  memset(g_mock_can_upper, 0, sizeof(g_mock_can_upper));
  g_mock_can_upper_rx_count = 0;
  g_mock_can_upper_txdone_count = 0;
  g_mock_can_rx_cb.cb = NULL;
  g_mock_can_rx_cb.param = NULL;
  g_mock_can_tx_cb.cb = NULL;
  g_mock_can_tx_cb.param = NULL;
  g_mock_can_err_cb.cb = NULL;
  g_mock_can_err_cb.param = NULL;
}
