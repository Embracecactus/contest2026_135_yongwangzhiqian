/* SPDX-License-Identifier: Apache-2.0 */
/* Actual AP PM client and CP PM server linked through a deterministic RPMsg
 * peer.  Hardware registers and scheduling are the only substituted edges. */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>

#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_pm_ipc.h"

int ap_pm_initialize(void);
int ap_pm_soft_off_request(void);
int ap_pm_soft_off_status(void);
int cp_pm_initialize(void);

struct callback_registration_s
{
  void *priv;
  rpmsg_dev_cb_t created;
  rpmsg_dev_cb_t destroyed;
  rpmsg_match_cb_t match;
  rpmsg_bind_cb_t bind;
};

static struct callback_registration_s g_client_registration;
static struct callback_registration_s g_server_registration;
static struct rpmsg_endpoint *g_client_endpoint;
static struct rpmsg_endpoint *g_server_endpoint;
static rpmsg_ept_cb_t g_client_callback;
static rpmsg_ept_cb_t g_server_callback;
static void *g_client_priv;
static void *g_server_priv;
static struct bk7258_rptun_control_s g_control;
static struct bk7258_pm_wire_s g_requests[4];
static struct bk7258_pm_wire_s g_last_response;
static unsigned int g_request_count;
static unsigned int g_response_count;
static unsigned int g_dropped_responses;
static unsigned int g_drop_response_count;
static unsigned int g_soft_off_calls;
static unsigned int g_soft_off_status_calls;
static unsigned int g_wait_count;
static bool g_route_responses = true;
static clock_t g_ticks;

int bk_pm_module_vote_power_ctrl(unsigned int module, int power_state);

static struct rpmsg_device g_cp_device =
{
  .cpuname = "cp",
  .support_ns = true,
  .support_ack = true,
};

static struct rpmsg_device g_ap_device =
{
  .cpuname = "ap",
  .support_ns = true,
  .support_ack = true,
};

volatile struct bk7258_rptun_control_s *bk7258_rptun_control(void)
{
  return &g_control;
}

const char *rpmsg_get_cpuname(struct rpmsg_device *rdev)
{
  return rdev == NULL ? NULL : rdev->cpuname;
}

int rpmsg_register_callback(void *priv, rpmsg_dev_cb_t created,
                            rpmsg_dev_cb_t destroyed,
                            rpmsg_match_cb_t match, rpmsg_bind_cb_t bind)
{
  struct callback_registration_s *registration =
    match == NULL ? &g_client_registration : &g_server_registration;

  registration->priv = priv;
  registration->created = created;
  registration->destroyed = destroyed;
  registration->match = match;
  registration->bind = bind;
  return 0;
}

void rpmsg_unregister_callback(void *priv, rpmsg_dev_cb_t created,
                               rpmsg_dev_cb_t destroyed,
                               rpmsg_match_cb_t match, rpmsg_bind_cb_t bind)
{
  (void)priv;
  (void)created;
  (void)destroyed;
  (void)match;
  (void)bind;
}

int rpmsg_create_ept(struct rpmsg_endpoint *ept,
                     struct rpmsg_device *rdev, const char *name,
                     uint32_t src, uint32_t dest, rpmsg_ept_cb_t callback,
                     rpmsg_ns_unbind_cb_t unbind)
{
  void *priv = ept->priv;

  (void)src;
  (void)unbind;
  memset(ept, 0, sizeof(*ept));
  ept->rdev = rdev;
  ept->priv = priv;
  ept->dest_addr = dest;
  strncpy(ept->name, name, sizeof(ept->name) - 1);
  if (strcmp(rdev->cpuname, "cp") == 0)
    {
      g_client_endpoint = ept;
      g_client_callback = callback;
      g_client_priv = ept->priv;
    }
  else
    {
      g_server_endpoint = ept;
      g_server_callback = callback;
      g_server_priv = ept->priv;
    }

  return 0;
}

void rpmsg_destroy_ept(struct rpmsg_endpoint *ept)
{
  ept->rdev = NULL;
  ept->dest_addr = RPMSG_ADDR_ANY;
}

int rpmsg_trysend(struct rpmsg_endpoint *ept, const void *data, int len)
{
  assert(len == (int)sizeof(struct bk7258_pm_wire_s));
  if (ept == g_client_endpoint)
    {
      assert(g_server_callback != NULL && g_server_endpoint != NULL);
      assert(g_request_count < sizeof(g_requests) / sizeof(g_requests[0]));
      memcpy(&g_requests[g_request_count++], data, sizeof(g_requests[0]));
      return g_server_callback(g_server_endpoint, (void *)data,
                               (size_t)len, 0, g_server_priv);
    }

  assert(ept == g_server_endpoint);
  memcpy(&g_last_response, data, sizeof(g_last_response));
  g_response_count++;
  if (g_drop_response_count != 0)
    {
      g_drop_response_count--;
      g_dropped_responses++;
      return len;
    }

  if (!g_route_responses)
    {
      return len;
    }

  assert(g_client_callback != NULL && g_client_endpoint != NULL);
  (void)g_client_callback(g_client_endpoint, (void *)data, (size_t)len,
                          0, g_client_priv);
  return len;
}

int nxmutex_init(mutex_t *mutex)
{
  return pthread_mutex_init(mutex, NULL) == 0 ? 0 : -EINVAL;
}

int nxmutex_destroy(mutex_t *mutex)
{
  return pthread_mutex_destroy(mutex) == 0 ? 0 : -EINVAL;
}

int nxmutex_lock(mutex_t *mutex)
{
  return pthread_mutex_lock(mutex) == 0 ? 0 : -EINVAL;
}

int nxmutex_timedlock(mutex_t *mutex, unsigned int timeout_ms)
{
  (void)timeout_ms;
  return nxmutex_lock(mutex);
}

int nxmutex_unlock(mutex_t *mutex)
{
  return pthread_mutex_unlock(mutex) == 0 ? 0 : -EINVAL;
}

int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{
  return sem_init(sem, pshared, value) == 0 ? 0 : -errno;
}

int nxsem_destroy(sem_t *sem)
{
  return sem_destroy(sem) == 0 ? 0 : -errno;
}

int nxsem_post(sem_t *sem)
{
  return sem_post(sem) == 0 ? 0 : -errno;
}

int nxsem_trywait(sem_t *sem)
{
  return sem_trywait(sem) == 0 ? 0 : -errno;
}

int nxsem_tickwait_uninterruptible(sem_t *sem, int ticks)
{
  (void)ticks;
  g_wait_count++;
  return sem_trywait(sem) == 0 ? 0 : -ETIMEDOUT;
}

int nxsem_wait_uninterruptible(sem_t *sem)
{
  return sem_wait(sem) == 0 ? 0 : -errno;
}

clock_t clock_systime_ticks(void)
{
  return g_ticks;
}

int clock_systime_timespec(struct timespec *ts)
{
  ts->tv_sec = g_ticks / 1000;
  ts->tv_nsec = (g_ticks % 1000) * 1000000;
  return 0;
}

int nxsig_usleep(uint32_t usec)
{
  g_ticks += (clock_t)((usec + 999) / 1000);
  return 0;
}

bool up_interrupt_context(void)
{
  return false;
}

pid_t nxsched_gettid(void)
{
  return 1;
}

void test_pm_putreg32(uint32_t value, uintptr_t address)
{
  (void)value;
  (void)address;
}

void bk7258_systick_recalc(void)
{
}

int cp_pm_soft_off_request(void)
{
  g_soft_off_calls++;
  return 0;
}

int cp_pm_soft_off_status(void)
{
  g_soft_off_status_calls++;
  return 1;
}

int cp_pm_frequency_vote(enum bk7258_pm_freq_client_e client,
                         bk7258_pm_opp_t opp)
{
  (void)client;
  (void)opp;
  return 0;
}

int cp_pm_frequency_get_status(struct bk7258_pm_frequency_status_s *status)
{
  memset(status, 0, sizeof(*status));
  return 0;
}

int __real_bk_pm_module_vote_power_ctrl(unsigned int module,
                                        int power_state)
{
  return bk_pm_module_vote_power_ctrl(module, power_state);
}

uint32_t __real_sys_drv_aud_select_clock(uint32_t value)
{
  return value;
}

void sys_drv_dev_clk_pwr_up(int dev, int power_up)
{
  (void)dev;
  (void)power_up;
}

int bk_pm_module_vote_power_ctrl(unsigned int module, int power_state)
{
  (void)module;
  (void)power_state;
  return 0;
}

void sys_drv_module_power_ctrl(int module, int power_state)
{
  (void)module;
  (void)power_state;
}

int32_t sys_drv_module_power_state_get(int module)
{
  (void)module;
  return 0;
}

void smem_reset_lastblock(void)
{
}

void sys_hal_aud_select_clock(uint32_t value)
{
  (void)value;
}

void sys_hal_set_auxs_cis_clk_sel(uint32_t value)
{
  (void)value;
}

void sys_hal_set_auxs_cis_clk_div(uint32_t value)
{
  (void)value;
}

void sys_hal_set_cis_auxs_clk_en(uint32_t value)
{
  (void)value;
}

static void connect_peers(void)
{
  memset(&g_control, 0, sizeof(g_control));
  g_control.generation = 41;
  assert(ap_pm_initialize() == 0);
  assert(cp_pm_initialize() == 0);
  assert(g_client_registration.created != NULL);
  assert(g_server_registration.bind != NULL);
  g_server_registration.created(&g_ap_device, g_server_registration.priv);
  g_client_registration.created(&g_cp_device, g_client_registration.priv);
  assert(g_server_registration.match(&g_ap_device,
                                     g_server_registration.priv,
                                     BK7258_PM_EPT_NAME, 1));
  g_server_registration.bind(&g_ap_device, g_server_registration.priv,
                             BK7258_PM_EPT_NAME, 1);
  assert(g_client_endpoint != NULL && g_server_endpoint != NULL);
  g_client_endpoint->dest_addr = 1;
}

static void deliver_direct(const struct bk7258_pm_wire_s *request)
{
  g_route_responses = false;
  assert(g_server_callback(g_server_endpoint, (void *)request,
                           sizeof(*request), 0, g_server_priv) >= 0);
  g_route_responses = true;
}

int main(void)
{
  struct bk7258_pm_wire_s altered;
  struct bk7258_pm_wire_s newer;

  connect_peers();
  g_drop_response_count = 1;
  assert(ap_pm_soft_off_request() == 0);
  assert(g_soft_off_calls == 1);
  assert(g_request_count == 2);
  assert(g_response_count == 2 && g_dropped_responses == 1);
  assert(g_wait_count == 2);
  assert(memcmp(&g_requests[0], &g_requests[1],
                sizeof(g_requests[0])) == 0);
  assert(g_requests[0].magic == BK7258_PM_MAGIC);
  assert(g_requests[0].version == BK7258_PM_VERSION);
  assert(g_requests[0].command == BK7258_PM_COMMAND_SOFT_OFF);
  assert(g_requests[0].generation == 41);
  assert(g_requests[0].sequence != 0);
  assert(g_requests[0].clock == 0 && g_requests[0].reserved == 0);

  altered = g_requests[0];
  altered.clock = 1;
  deliver_direct(&altered);
  assert(g_last_response.status == -EPROTO);
  assert(g_soft_off_calls == 1);

  newer = g_requests[0];
  newer.sequence++;
  newer.command = BK7258_PM_COMMAND_SOFT_OFF_STATUS;
  deliver_direct(&newer);
  assert(g_last_response.status == 1);
  assert(g_soft_off_status_calls == 1 && g_soft_off_calls == 1);

  deliver_direct(&g_requests[0]);
  assert(g_last_response.status == -ESTALE);
  assert(g_soft_off_status_calls == 1 && g_soft_off_calls == 1);

  puts("CONTRACT_PASS LIFE-02.power-cp-lost-reply-replay");
  return 0;
}
