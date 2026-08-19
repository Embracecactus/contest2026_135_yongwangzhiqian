/****************************************************************************
 * tests/bk7258/framework/mock_sdk_scale_rotate.c
 *
 * SDK stand-in implementation (see mock_sdk_scale_rotate.h).  Also owns the
 * suite-local counting-semaphore/clock shims (nuttx_yuv variants) so the
 * driver's blocking waits stay deterministic and single-threaded.
 ****************************************************************************/

#include "mock_sdk_scale_rotate.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <common/bk_err.h>
#include <nuttx/semaphore.h>
#include <nuttx/clock.h>

static int g_mock_sr_ret[MOCK_SR_FN_MAX];
static int g_mock_sr_ret_once_fn = -1;
static int g_mock_sr_ret_once_val;
static struct mock_sr_call_s g_mock_sr_calls[MOCK_SR_LOG_MAX];
static unsigned int g_mock_sr_call_count;
static int32_t g_mock_sr_sys_enable;

static void (*g_mock_sr_scale_isr[HW_SCALE1 + 1])(void *);
static void *g_mock_sr_scale_isr_param[HW_SCALE1 + 1];
static rott_int_callback_t g_mock_sr_rott_isr[2];

static scale_drv_config_t g_mock_sr_scale_config;
static rott_config_t g_mock_sr_rott_config;
static rott_input_data_flow_t g_mock_sr_reverse_in;
static rott_output_data_flow_t g_mock_sr_reverse_out;
static uint32_t g_mock_sr_rott_mask;

void (*g_mock_sr_tickwait_hook)(void);
static sem_t *g_mock_sr_last_wait_sem;

/* ---------------------------------------------------------------------- */
/* Logging                                                                */
/* ---------------------------------------------------------------------- */

static void mock_sr_log(int fn, uint32_t a0, uint32_t a1, uint32_t a2)
{
  if (g_mock_sr_call_count < MOCK_SR_LOG_MAX)
    {
      g_mock_sr_calls[g_mock_sr_call_count].fn = fn;
      g_mock_sr_calls[g_mock_sr_call_count].a0 = a0;
      g_mock_sr_calls[g_mock_sr_call_count].a1 = a1;
      g_mock_sr_calls[g_mock_sr_call_count].a2 = a2;
    }

  g_mock_sr_call_count++;
}

void mock_sr_sdk_reset(void)
{
  int i;

  memset(g_mock_sr_ret, 0, sizeof(g_mock_sr_ret));
  g_mock_sr_ret_once_fn = -1;
  g_mock_sr_call_count = 0;
  g_mock_sr_sys_enable = 0;
  g_mock_sr_tickwait_hook = NULL;
  memset(&g_mock_sr_scale_config, 0, sizeof(g_mock_sr_scale_config));
  memset(&g_mock_sr_rott_config, 0, sizeof(g_mock_sr_rott_config));
  g_mock_sr_reverse_in = ROTT_INPUT_NORMAL;
  g_mock_sr_reverse_out = ROTT_OUTPUT_NORMAL;
  g_mock_sr_rott_mask = 0;
  g_mock_sr_last_wait_sem = NULL;
  for (i = 0; i < HW_SCALE1 + 1; i++)
    {
      g_mock_sr_scale_isr[i] = NULL;
      g_mock_sr_scale_isr_param[i] = NULL;
    }

  for (i = 0; i < 2; i++)
    {
      g_mock_sr_rott_isr[i] = NULL;
    }
}

void mock_sr_set_ret(int fn, int ret)
{
  g_mock_sr_ret[fn] = ret;
}

/* One-shot failure injection: the next fetch of fn returns ret, then
 * recovers.  Needed where a persistent failure would wedge the driver's
 * singleton (partial uninitialize) and poison every later test. */
void mock_sr_set_ret_once(int fn, int ret)
{
  g_mock_sr_ret_once_fn = fn;
  g_mock_sr_ret_once_val = ret;
}

static int mock_sr_get_ret(int fn)
{
  if (g_mock_sr_ret_once_fn == fn)
    {
      g_mock_sr_ret_once_fn = -1;
      return g_mock_sr_ret_once_val;
    }

  return g_mock_sr_ret[fn];
}

void mock_sr_set_sys_enable(int32_t ret)
{
  g_mock_sr_sys_enable = ret;
}

void mock_sr_post_completion(void)
{
  if (g_mock_sr_last_wait_sem != NULL)
    {
      nxsem_post(g_mock_sr_last_wait_sem);
    }
}

void mock_sr_set_tickwait_hook(void (*hook)(void))
{
  g_mock_sr_tickwait_hook = hook;
}

unsigned int mock_sr_call_count(void)
{
  return g_mock_sr_call_count;
}

int mock_sr_calls_of(int fn)
{
  unsigned int i;
  int count = 0;

  for (i = 0; i < g_mock_sr_call_count; i++)
    {
      if (g_mock_sr_calls[i].fn == fn)
        {
          count++;
        }
    }

  return count;
}

const struct mock_sr_call_s *mock_sr_call(unsigned int index)
{
  return &g_mock_sr_calls[index];
}

const scale_drv_config_t *mock_sr_scale_config(void)
{
  return &g_mock_sr_scale_config;
}

const rott_config_t *mock_sr_rott_config(void)
{
  return &g_mock_sr_rott_config;
}

rott_input_data_flow_t mock_sr_reverse_input(void)
{
  return g_mock_sr_reverse_in;
}

rott_output_data_flow_t mock_sr_reverse_output(void)
{
  return g_mock_sr_reverse_out;
}

uint32_t mock_sr_rott_int_mask(void)
{
  return g_mock_sr_rott_mask;
}

/* ---------------------------------------------------------------------- */
/* Hardware scale driver                                                  */
/* ---------------------------------------------------------------------- */

bk_err_t bk_hw_scale_driver_init(scale_id_t id)
{
  mock_sr_log(MOCK_SR_FN_SCALE_DRIVER_INIT, (uint32_t)id, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_SCALE_DRIVER_INIT);
}

bk_err_t bk_hw_scale_driver_deinit(scale_id_t id)
{
  mock_sr_log(MOCK_SR_FN_SCALE_DRIVER_DEINIT, (uint32_t)id, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_SCALE_DRIVER_DEINIT);
}

bk_err_t bk_hw_scale_isr_register(scale_id_t id, void (*fn)(void *),
                                  void *arg)
{
  mock_sr_log(MOCK_SR_FN_SCALE_ISR_REGISTER, (uint32_t)id, 0, 0);
  if (id == HW_SCALE0 || id == HW_SCALE1)
    {
      g_mock_sr_scale_isr[id] = fn;
      g_mock_sr_scale_isr_param[id] = arg;
    }

  return mock_sr_get_ret(MOCK_SR_FN_SCALE_ISR_REGISTER);
}

bk_err_t bk_hw_scale_isr_unregister(scale_id_t id)
{
  mock_sr_log(MOCK_SR_FN_SCALE_ISR_UNREGISTER, (uint32_t)id, 0, 0);
  if (id == HW_SCALE0 || id == HW_SCALE1)
    {
      g_mock_sr_scale_isr[id] = NULL;
      g_mock_sr_scale_isr_param[id] = NULL;
    }

  return mock_sr_get_ret(MOCK_SR_FN_SCALE_ISR_UNREGISTER);
}

bk_err_t bk_hw_scale_int_enable(scale_id_t id, bool enable)
{
  mock_sr_log(MOCK_SR_FN_SCALE_INT_ENABLE, (uint32_t)id, enable, 0);
  return mock_sr_get_ret(MOCK_SR_FN_SCALE_INT_ENABLE);
}

bk_err_t bk_hw_scale_mem_free(scale_id_t id)
{
  mock_sr_log(MOCK_SR_FN_SCALE_MEM_FREE, (uint32_t)id, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_SCALE_MEM_FREE);
}

bk_err_t bk_hw_scale_stop(scale_id_t id)
{
  mock_sr_log(MOCK_SR_FN_SCALE_STOP, (uint32_t)id, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_SCALE_STOP);
}

bk_err_t hw_scale_frame(scale_id_t id, scale_drv_config_t *config)
{
  mock_sr_log(MOCK_SR_FN_SCALE_FRAME, (uint32_t)id, 0, 0);
  if (config != NULL)
    {
      g_mock_sr_scale_config = *config;
    }

  return mock_sr_get_ret(MOCK_SR_FN_SCALE_FRAME);
}

void mock_sr_fire_scale(scale_id_t id)
{
  if (id == HW_SCALE0 || id == HW_SCALE1)
    {
      if (g_mock_sr_scale_isr[id] != NULL)
        {
          g_mock_sr_scale_isr[id](g_mock_sr_scale_isr_param[id]);
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Rotator driver                                                         */
/* ---------------------------------------------------------------------- */

bk_err_t bk_rott_driver_init(void)
{
  mock_sr_log(MOCK_SR_FN_ROTT_DRIVER_INIT, 0, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_ROTT_DRIVER_INIT);
}

bk_err_t bk_rott_driver_deinit(void)
{
  mock_sr_log(MOCK_SR_FN_ROTT_DRIVER_DEINIT, 0, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_ROTT_DRIVER_DEINIT);
}

bk_err_t bk_rott_soft_reset(void)
{
  mock_sr_log(MOCK_SR_FN_ROTT_SOFT_RESET, 0, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_ROTT_SOFT_RESET);
}

bk_err_t bk_rott_isr_register(rott_int_type_t type, rott_int_callback_t fn)
{
  mock_sr_log(MOCK_SR_FN_ROTT_ISR_REGISTER, (uint32_t)type, 0, 0);
  if (type == ROTATE_COMPLETE_INT || type == ROTATE_CFG_ERR_INT)
    {
      g_mock_sr_rott_isr[type] = fn;
    }

  return mock_sr_get_ret(MOCK_SR_FN_ROTT_ISR_REGISTER);
}

bk_err_t bk_rott_int_enable(uint32_t mask, bool enable)
{
  mock_sr_log(MOCK_SR_FN_ROTT_INT_ENABLE, mask, enable, 0);
  g_mock_sr_rott_mask = mask;
  return mock_sr_get_ret(MOCK_SR_FN_ROTT_INT_ENABLE);
}

bk_err_t bk_rott_data_reverse(rott_input_data_flow_t input_flow,
                              rott_output_data_flow_t output_flow)
{
  mock_sr_log(MOCK_SR_FN_ROTT_DATA_REVERSE, (uint32_t)input_flow,
              (uint32_t)output_flow, 0);
  g_mock_sr_reverse_in = input_flow;
  g_mock_sr_reverse_out = output_flow;
  return mock_sr_get_ret(MOCK_SR_FN_ROTT_DATA_REVERSE);
}

bk_err_t bk_rott_enable(void)
{
  mock_sr_log(MOCK_SR_FN_ROTT_ENABLE, 0, 0, 0);
  return mock_sr_get_ret(MOCK_SR_FN_ROTT_ENABLE);
}

bk_err_t rott_config(rott_config_t *config)
{
  mock_sr_log(MOCK_SR_FN_ROTT_CONFIG, 0, 0, 0);
  if (config != NULL)
    {
      g_mock_sr_rott_config = *config;
    }

  return mock_sr_get_ret(MOCK_SR_FN_ROTT_CONFIG);
}

void mock_sr_fire_rott_complete(void)
{
  if (g_mock_sr_rott_isr[ROTATE_COMPLETE_INT] != NULL)
    {
      g_mock_sr_rott_isr[ROTATE_COMPLETE_INT]();
    }
}

void mock_sr_fire_rott_error(void)
{
  if (g_mock_sr_rott_isr[ROTATE_CFG_ERR_INT] != NULL)
    {
      g_mock_sr_rott_isr[ROTATE_CFG_ERR_INT]();
    }
}

/* ---------------------------------------------------------------------- */
/* CPU1/CPU2 interrupt-route ABI                                          */
/* ---------------------------------------------------------------------- */

int32_t sys_drv_core_intr_group2_enable(uint32_t core_id, uint32_t param)
{
  mock_sr_log(MOCK_SR_FN_SYS_GROUP2_ENABLE, core_id, param, 0);
  return g_mock_sr_sys_enable;
}

int32_t sys_drv_core_intr_group2_disable(uint32_t core_id, uint32_t param)
{
  mock_sr_log(MOCK_SR_FN_SYS_GROUP2_DISABLE, core_id, param, 0);
  return 0;
}

/* ---------------------------------------------------------------------- */
/* NuttX counting-semaphore shim (nuttx_yuv variant)                      */
/* ---------------------------------------------------------------------- */

int nxsem_init(sem_t *sem, int pshared, unsigned int value)
{
  (void)pshared;
  sem->count = (int)value;
  return 0;
}

int nxsem_destroy(sem_t *sem)
{
  (void)sem;
  return 0;
}

int nxsem_reset(sem_t *sem, unsigned int count)
{
  sem->count = (int)count;
  return 0;
}

int nxsem_post(sem_t *sem)
{
  if (sem->count < INT_MAX)
    {
      sem->count++;
    }

  return 0;
}

int nxsem_trywait(sem_t *sem)
{
  if (sem->count > 0)
    {
      sem->count--;
      return 0;
    }

  return -EAGAIN;
}

int nxsem_tickwait_uninterruptible(sem_t *sem, int ticks)
{
  (void)ticks;
  g_mock_sr_last_wait_sem = sem;
  if (sem->count > 0)
    {
      sem->count--;
      return 0;
    }

  /* A suite hook may fire ISRs (posting the completion) on this first
   * empty wait; otherwise the wait times out deterministically. */

  if (g_mock_sr_tickwait_hook != NULL)
    {
      g_mock_sr_tickwait_hook();
    }

  if (sem->count > 0)
    {
      sem->count--;
      return 0;
    }

  return -ETIMEDOUT;
}

/* ---------------------------------------------------------------------- */
/* Clock shim (nuttx_yuv variant)                                         */
/* ---------------------------------------------------------------------- */

clock_t clock_systime_ticks(void)
{
  return 0;
}