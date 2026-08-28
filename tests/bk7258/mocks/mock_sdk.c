/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/mocks/mock_sdk.c
 *
 * Implementation of the mocked Beken SDK mailbox transport, the AMP boot
 * state, and the NuttX kthread/semaphore shims used by
 * bk7258_rptun_mbox.c.  This file is compiled together with a verbatim copy
 * of the real bk7258_rptun_mbox.c; it provides every external symbol the
 * implementation references except the implementation's own functions.
 ****************************************************************************/

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "mock_sdk.h"

/* ------------------------------------------------------------------ */
/* AMP boot state                                                     */
/* ------------------------------------------------------------------ */

static struct bk7258_ap_boot_state_s g_boot_state;
static struct bk7258_rptun_control_s g_control;

volatile struct bk7258_ap_boot_state_s *
bk7258_ap_boot_state(void)
{
  return &g_boot_state;
}

volatile struct bk7258_rptun_control_s *
bk7258_rptun_control(void)
{
  return &g_control;
}

/* ------------------------------------------------------------------ */
/* Controllable transport state                                       */
/* ------------------------------------------------------------------ */

static bool g_busy = false;
static bool g_tx_complete = true;

static mb_chnl_cmd_t g_last_write;
static int g_write_count = 0;

static pthread_mutex_t g_write_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_write_cond = PTHREAD_COND_INITIALIZER;

static chnl_rx_isr_t      g_rx_cb = NULL;
static chnl_tx_cmpl_isr_t g_tx_cb = NULL;

void mock_mbox_set_busy(bool busy)
{
  g_busy = busy;
}

void mock_mbox_set_tx_complete(bool enabled)
{
  g_tx_complete = enabled;
}

void mock_mbox_reset(void)
{
  g_busy = false;
  g_tx_complete = true;
  memset(&g_last_write, 0, sizeof(g_last_write));
  g_write_count = 0;
}

void mock_mbox_get_last_write(uint32_t *type, uint32_t *generation,
                              uint32_t *value)
{
  pthread_mutex_lock(&g_write_lock);
  if (type)      *type = g_last_write.param1;
  if (generation) *generation = g_last_write.param2;
  if (value)     *value = g_last_write.param3;
  pthread_mutex_unlock(&g_write_lock);
}

int mock_mbox_write_count(void)
{
  int c;
  pthread_mutex_lock(&g_write_lock);
  c = g_write_count;
  pthread_mutex_unlock(&g_write_lock);
  return c;
}

int mock_mbox_wait_for_write(int64_t timeout_ms)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec  += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L)
    {
      ts.tv_sec  += ts.tv_nsec / 1000000000L;
      ts.tv_nsec %= 1000000000L;
    }

  pthread_mutex_lock(&g_write_lock);
  int rc = pthread_cond_timedwait(&g_write_cond, &g_write_lock, &ts);
  pthread_mutex_unlock(&g_write_lock);
  return rc == 0 ? 0 : -1;
}

void mock_mbox_inject_rx(uint32_t type, uint32_t generation, uint32_t value)
{
  mb_chnl_cmd_t cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.hdr.cmd = BK7258_RPTUN_MBOX_COMMAND;
  cmd.param1 = type;
  cmd.param2 = generation;
  cmd.param3 = value;

  if (g_rx_cb)
    {
      g_rx_cb(NULL, &cmd);
    }
}

void mock_mbox_set_boot_generation(uint32_t generation)
{
  g_boot_state.generation = generation;
  g_control.generation = generation;
}

void mock_mbox_set_boot_state(uint32_t state)
{
  g_boot_state.state = state;
}

/* ------------------------------------------------------------------ */
/* SDK mailbox channel API                                            */
/* ------------------------------------------------------------------ */

bk_err_t mb_chnl_init(void)
{
  return BK_OK;
}

bk_err_t mb_chnl_open(uint8_t log_chnl, void *param)
{
  (void)log_chnl;
  (void)param;
  return BK_OK;
}

bk_err_t mb_chnl_close(uint8_t log_chnl)
{
  (void)log_chnl;
  return BK_OK;
}

bk_err_t mb_chnl_ctrl(uint8_t log_chnl, int cmd, void *param)
{
  (void)log_chnl;
  switch (cmd)
    {
      case MB_CHNL_GET_STATUS:
        *(uint8_t *)param = g_busy ? 1u : 0u;
        break;
      case MB_CHNL_SET_RX_ISR:
        g_rx_cb = (chnl_rx_isr_t)param;
        break;
      case MB_CHNL_SET_TX_CMPL_ISR:
        g_tx_cb = (chnl_tx_cmpl_isr_t)param;
        break;
      default:
        break;
    }
  return BK_OK;
}

bk_err_t mb_chnl_write(uint8_t log_chnl, mb_chnl_cmd_t *cmd_buf)
{
  (void)log_chnl;

  if (g_busy)
    {
      return BK_ERR_BUSY;
    }

  pthread_mutex_lock(&g_write_lock);
  g_last_write = *cmd_buf;
  g_write_count++;
  pthread_cond_signal(&g_write_cond);
  pthread_mutex_unlock(&g_write_lock);

  /* Simulate the SDK firing the TX-complete ISR after a successful send.
   * When disabled (lost completion interrupt), the worker must still
   * recover via its 1 ms poll. */
  if (g_tx_complete && g_tx_cb)
    {
      g_tx_cb(NULL, (mb_chnl_ack_t *)cmd_buf);
    }

  return BK_OK;
}

/* ------------------------------------------------------------------ */
/* NuttX semaphore shim                                               */
/* ------------------------------------------------------------------ */

int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{
  return sem_init(sem, pshared, value);
}

int nxsem_destroy(sem_t *sem)
{
  return sem_destroy(sem);
}

int nxsem_post(sem_t *sem)
{
  return sem_post(sem);
}

int nxsem_trywait(sem_t *sem)
{
  return sem_trywait(sem);
}

int nxsem_tickwait_uninterruptible(sem_t *sem, int ticks)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec  += ticks / 1000;
  ts.tv_nsec += (ticks % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L)
    {
      ts.tv_sec  += ts.tv_nsec / 1000000000L;
      ts.tv_nsec %= 1000000000L;
    }

  if (sem_timedwait(sem, &ts) == 0)
    {
      return 0;
    }

  return (errno == ETIMEDOUT) ? -ETIMEDOUT : -errno;
}

int nxsem_wait_uninterruptible(sem_t *sem)
{
  return sem_wait(sem) == 0 ? 0 : -errno;
}

/* ------------------------------------------------------------------ */
/* NuttX kthread shim                                                 */
/* ------------------------------------------------------------------ */

typedef struct
{
  int (*entry)(int, char *argv[]);
} kthr_arg_t;

static void *kthread_tramp(void *p)
{
  kthr_arg_t *a = (kthr_arg_t *)p;
  a->entry(0, NULL);
  free(a);
  return NULL;
}

static pthread_t g_worker_pt;
static bool g_worker_valid = false;

pid_t kthread_create(const char *name, int priority, int stacksize,
                     int (*entry)(int, char *argv[]), char *argv[])
{
  (void)name;
  (void)priority;
  (void)stacksize;
  (void)argv;

  kthr_arg_t *a = (kthr_arg_t *)malloc(sizeof(*a));
  a->entry = entry;

  pthread_t pt;
  pthread_create(&pt, NULL, kthread_tramp, a);
  g_worker_pt = pt;
  g_worker_valid = true;
  return 1;   /* any non-negative pid; the implementation only stores it */
}

int kthread_delete(pid_t pid)
{
  (void)pid;
  return 0;
}

void mock_mbox_fini(void)
{
  if (g_worker_valid)
    {
      pthread_cancel(g_worker_pt);
      pthread_join(g_worker_pt, NULL);
      g_worker_valid = false;
    }
}
