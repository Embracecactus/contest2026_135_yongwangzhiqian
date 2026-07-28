/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/bk7258_os_adapt.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * BK7258 NuttX OS Adaptation Layer
 *
 * Bridges Beken SDK FreeRTOS API calls to NuttX equivalents so that the
 * prebuilt SDK libraries (libbk_wifi.a, libbk_rtos.a, etc.) can link and
 * run correctly on NuttX.
 *
 * Modelled after the BK7236N adaptation at
 *   armino/vendor_beken/chips/bk7236n/beken_os_adapt.c
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <debug.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <clock/clock.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <irq/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mqueue.h>
#include <nuttx/spinlock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/kthread.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>
#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <nuttx/arch.h>
#include <nuttx/init.h>
#include <nuttx/tls.h>

#include "os/os.h"
#include "os/mem.h"
#include "os/str.h"
#include "os/rtos_ext.h"

/* Undef SDK macros that conflict with our function definitions.
 * os/mem.h defines os_malloc/os_free/psram_malloc etc. as macros
 * redirecting to *_debug variants. We need real function definitions
 * for the prebuilt libraries to link against.
 */
#ifdef os_malloc
#undef os_malloc
#endif
#ifdef os_free
#undef os_free
#endif
#ifdef os_zalloc
#undef os_zalloc
#endif
#ifdef os_sram_malloc
#undef os_sram_malloc
#endif
#ifdef os_sram_calloc
#undef os_sram_calloc
#endif
#ifdef os_sram_zalloc
#undef os_sram_zalloc
#endif
#ifdef psram_malloc
#undef psram_malloc
#endif
#ifdef psram_zalloc
#undef psram_zalloc
#endif
#ifdef rtos_get_ms_per_tick
#undef rtos_get_ms_per_tick
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BEKEN_WAIT_FOREVER  (0xFFFFFFFF)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Timer adapter - bridges Beken timer to NuttX watchdog */

struct timer_adpt
{
  struct wdog_s wdog;       /* NuttX watchdog handle */
  bool          repeat;     /* True if periodic timer */
  uint32_t      delay;      /* Timeout in ticks */
  void          *priv;      /* Pointer back to beken_timer_t / beken2_timer_t */
};

/* Message queue adapter */

struct mq_adpt_s
{
  struct file mq;           /* NuttX message queue handle */
  uint32_t    msgsize;      /* Message size in bytes */
  char        name[16];     /* Message queue name */
  char        cname[32];    /* Display name for debug */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_os_delay_ms(uint32_t milliseconds)
{
  /* NuttX must never block the IDLE task.  SDK clock/UART setup can request
   * delays from up_initialize(), before nx_bringup() has created any other
   * runnable task.  Use an architecture busy wait until normal scheduling is
   * active, and whenever this adapter is called from IDLE or interrupt context.
   */

  if (!OSINIT_IDLELOOP() ||
      nxsched_getpid() == IDLE_PROCESS_ID ||
      up_interrupt_context())
    {
      up_mdelay(milliseconds);
    }
  else
    {
      nxsig_usleep(milliseconds * 1000u);
    }
}

static inline bk_err_t beken_errno_trans(int ret)
{
  if (!ret)
    {
      return BK_OK;
    }

  return BK_FAIL;
}

static void os_timer_callback(wdparm_t arg)
{
  struct timer_adpt *timer_apt = (struct timer_adpt *)arg;

  if (timer_apt->repeat)
    {
      beken_timer_t *timer = (beken_timer_t *)timer_apt->priv;
      if (timer->function)
        {
          timer->function(timer->arg);
        }

      wd_start(&timer_apt->wdog, timer_apt->delay,
               os_timer_callback, arg);
    }
  else
    {
      beken2_timer_t *timer = (beken2_timer_t *)timer_apt->priv;
      if (timer->function)
        {
          timer->function(timer->left_arg, timer->right_arg);
        }
    }
}

/****************************************************************************
 * Public Functions - Interrupt Management
 ****************************************************************************/

uint32_t rtos_disable_int(void)
{
  return enter_critical_section();
}

void rtos_enable_int(uint32_t int_level)
{
  leave_critical_section(int_level);
}

uint32_t rtos_enter_critical(void)
{
  return enter_critical_section();
}

void rtos_exit_critical(uint32_t int_level)
{
  leave_critical_section(int_level);
}

uint32_t rtos_before_sleep(void)
{
  return enter_critical_section();
}

void rtos_after_sleep(uint32_t int_level)
{
  leave_critical_section(int_level);
}

/****************************************************************************
 * Public Functions - Time
 ****************************************************************************/

bk_err_t beken_time_get_time(beken_time_t *time_ptr)
{
  struct timespec ts;

  clock_systime_timespec(&ts);
  *time_ptr = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

  return BK_OK;
}

uint32_t rtos_get_time(void)
{
  struct timespec ts;

  clock_systime_timespec(&ts);

  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

uint32_t beken_ms_per_tick(void)
{
  return MSEC_PER_TICK;
}

uint32_t rtos_get_tick_count(void)
{
  return (uint32_t)clock_systime_ticks();
}

uint64_t bk_get_tick(void)
{
  return (uint64_t)clock_systime_ticks();
}

uint32_t bk_get_ticks_per_second(void)
{
  return 1000000 / CONFIG_USEC_PER_TICK;
}

uint32_t rtos_get_ms_per_tick(void)
{
  return MSEC_PER_TICK;
}

uint32_t bk_get_second(void)
{
  struct timespec ts;

  clock_systime_timespec(&ts);

  return (uint32_t)ts.tv_sec;
}

/****************************************************************************
 * Public Functions - Thread Management
 ****************************************************************************/

bk_err_t rtos_create_thread(beken_thread_t *thread, uint8_t priority,
                            const char *name,
                            beken_thread_function_t function,
                            uint32_t stack_size, beken_thread_arg_t arg)
{
  pid_t pid = -1;

  if (arg)
    {
      wlerr("Task(%s)'s arg is NOT NULL\n", name);
    }
  else
    {
      pid = kthread_create(name,
                           SCHED_PRIORITY_DEFAULT + 2 - priority,
                           stack_size,
                           (main_t)function, NULL);
    }

  if (pid <= 0)
    {
      wlerr("ERROR: Failed to create thread(%s): %d\n", name, pid);
      return BK_FAIL;
    }

  if (thread)
    {
      *thread = (void *)((uintptr_t)pid);
    }

  return BK_OK;
}

bk_err_t rtos_create_sram_thread(beken_thread_t *thread, uint8_t priority,
                                 const char *name,
                                 beken_thread_function_t function,
                                 uint32_t stack_size,
                                 beken_thread_arg_t arg)
{
  return rtos_create_thread(thread, priority, name, function,
                            stack_size, arg);
}

bk_err_t rtos_create_psram_thread(beken_thread_t *thread,
                                  uint8_t priority,
                                  const char *name,
                                  beken_thread_function_t function,
                                  uint32_t stack_size,
                                  beken_thread_arg_t arg)
{
  return rtos_create_thread(thread, priority, name, function,
                            stack_size, arg);
}

bk_err_t rtos_delete_thread(beken_thread_t *thread)
{
  pid_t pid = 0;

  if (thread)
    {
      pid = (pid_t)((uintptr_t)*thread);
    }

  task_delete(pid);

  return BK_OK;
}

bool rtos_is_current_thread(beken_thread_t *thread)
{
  pid_t pid = nxsched_getpid();

  return (pid == (pid_t)((uintptr_t)*thread));
}

beken_thread_t *rtos_get_current_thread(void)
{
  return (beken_thread_t *)(uintptr_t)nxsched_getpid();
}

bk_err_t rtos_thread_join(beken_thread_t *thread)
{
  /* Not fully implemented - stub */

  return BK_OK;
}

bk_err_t rtos_thread_force_awake(beken_thread_t *thread)
{
  /* Not fully implemented - stub */

  return BK_OK;
}

void rtos_suspend_thread(beken_thread_t *thread)
{
  /* Stub - not fully supported */
}

void rtos_suspend_all_thread(void)
{
  sched_lock();
}

void rtos_resume_thread(beken_thread_t *thread)
{
  /* Stub - not fully supported */
}

void rtos_resume_all_thread(void)
{
  sched_unlock();
}

void rtos_thread_sleep(uint32_t seconds)
{
  sleep(seconds);
}

void rtos_thread_msleep(uint32_t milliseconds)
{
  bk7258_os_delay_ms(milliseconds);
}

bk_err_t rtos_print_thread_status(char *buffer, int length)
{
  /* Stub */

  return BK_OK;
}

/****************************************************************************
 * Public Functions - Semaphore
 ****************************************************************************/

bk_err_t rtos_init_semaphore(beken_semaphore_t *semaphore, int max_count)
{
  sem_t *sem = NULL;
  int ret;

  sem = kmm_malloc(sizeof(sem_t));
  if (!sem)
    {
      wlerr("ERROR: Failed to malloc semaphore\n");
      return BK_FAIL;
    }

  ret = nxsem_init(sem, 0, 0);
  if (ret == OK)
    {
      *semaphore = sem;
    }
  else
    {
      wlerr("ERROR: Failed to create semaphore:%d\n", ret);
      kmm_free(sem);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_init_semaphore_ex(beken_semaphore_t *semaphore,
                                int max_count, int init_count)
{
  sem_t *sem = NULL;
  int ret;

  sem = kmm_malloc(sizeof(sem_t));
  if (!sem)
    {
      wlerr("ERROR: Failed to malloc semaphore\n");
      return BK_FAIL;
    }

  ret = nxsem_init(sem, 0, init_count);
  if (ret == OK)
    {
      *semaphore = sem;
    }
  else
    {
      wlerr("ERROR: Failed to create semaphore:%d\n", ret);
      kmm_free(sem);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_set_semaphore(beken_semaphore_t *semaphore)
{
  int ret;
  sem_t *sem = (sem_t *)(*semaphore);

  ret = nxsem_post(sem);
  if (ret != OK)
    {
      wlerr("ERROR: Failed to post semaphore:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_get_semaphore(beken_semaphore_t *semaphore,
                            uint32_t timeout_ms)
{
  int ret;
  sem_t *sem = (sem_t *)(*semaphore);

  if (timeout_ms == 0)
    {
      ret = nxsem_trywait(sem);
    }
  else if (timeout_ms == BEKEN_WAIT_FOREVER)
    {
      ret = nxsem_wait(sem);
    }
  else
    {
      ret = nxsem_tickwait(sem, MSEC2TICK(timeout_ms));
    }

  if (ret != OK)
    {
      wlerr("ERROR: Failed to get semaphore:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

int rtos_get_semaphore_count(beken_semaphore_t *semaphore)
{
  sem_t *sem = (sem_t *)(*semaphore);
  int count = 0;

  nxsem_get_value(sem, &count);

  return count;
}

bk_err_t rtos_deinit_semaphore(beken_semaphore_t *semaphore)
{
  int ret;
  sem_t *sem = (sem_t *)(*semaphore);

  ret = nxsem_destroy(sem);
  if (ret != OK)
    {
      wlerr("ERROR: Failed to destroy semaphore:%d\n", ret);
    }

  kmm_free(sem);

  return beken_errno_trans(ret);
}

/****************************************************************************
 * Public Functions - Mutex
 ****************************************************************************/

bk_err_t rtos_init_mutex(beken_mutex_t *mtx)
{
  int ret;
  mutex_t *mutex;

  mutex = kmm_malloc(sizeof(mutex_t));
  if (!mutex)
    {
      wlerr("ERROR: Failed to kmm_malloc\n");
      return BK_FAIL;
    }

  ret = nxmutex_init(mutex);
  if (ret == OK)
    {
      *mtx = mutex;
    }
  else
    {
      wlerr("ERROR: Failed to create mutex, ret:%d\n", ret);
      kmm_free(mutex);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_lock_mutex(beken_mutex_t *mtx)
{
  int ret;
  mutex_t *mutex = (mutex_t *)*mtx;

  ret = nxmutex_lock(mutex);
  if (ret != OK)
    {
      wlerr("ERROR: Failed to lock mutex:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_trylock_mutex(beken_mutex_t *mtx)
{
  int ret;
  mutex_t *mutex = (mutex_t *)*mtx;

  ret = nxmutex_trylock(mutex);

  return beken_errno_trans(ret);
}

bk_err_t rtos_lock_mutex_timeout(beken_mutex_t *mtx, uint32_t timeout_ms)
{
  int ret;
  mutex_t *mutex = (mutex_t *)*mtx;
  struct timespec ts;

  ts.tv_sec  = timeout_ms / 1000;
  ts.tv_nsec = (timeout_ms % 1000) * 1000000;

  ret = nxmutex_clocklock(mutex, CLOCK_MONOTONIC, &ts);

  return beken_errno_trans(ret);
}

bk_err_t rtos_unlock_mutex(beken_mutex_t *mtx)
{
  int ret;
  mutex_t *mutex = (mutex_t *)*mtx;

  ret = nxmutex_unlock(mutex);
  if (ret != OK)
    {
      wlerr("ERROR: Failed to unlock mutex:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_deinit_mutex(beken_mutex_t *mtx)
{
  mutex_t *mutex = (mutex_t *)*mtx;

  nxmutex_destroy(mutex);
  kmm_free(mutex);

  return BK_OK;
}

bk_err_t rtos_init_recursive_mutex(beken_mutex_t *mtx)
{
  /* NuttX mutex is already recursive-capable via nxmutex */

  return rtos_init_mutex(mtx);
}

bk_err_t rtos_lock_recursive_mutex(beken_mutex_t *mtx)
{
  return rtos_lock_mutex(mtx);
}

bk_err_t rtos_unlock_recursive_mutex(beken_mutex_t *mtx)
{
  return rtos_unlock_mutex(mtx);
}

bk_err_t rtos_deinit_recursive_mutex(beken_mutex_t *mtx)
{
  return rtos_deinit_mutex(mtx);
}

/****************************************************************************
 * Public Functions - Message Queue
 ****************************************************************************/

bk_err_t rtos_init_queue(beken_queue_t *queue, const char *name,
                         uint32_t message_size,
                         uint32_t number_of_messages)
{
  struct mq_attr attr;
  struct mq_adpt_s *mq_adpt;
  int ret;

  mq_adpt = kmm_malloc(sizeof(struct mq_adpt_s));
  if (!mq_adpt)
    {
      wlerr("ERROR: Failed to kmm_malloc\n");
      return BK_FAIL;
    }

  memset(mq_adpt, 0x0, sizeof(struct mq_adpt_s));

  if (name)
    {
      snprintf(mq_adpt->name, sizeof(mq_adpt->name), "/tmp/%s", name);
    }
  else
    {
      snprintf(mq_adpt->name, sizeof(mq_adpt->name), "/tmp/%p",
               mq_adpt);
    }

  attr.mq_maxmsg  = number_of_messages;
  attr.mq_msgsize = message_size;
  attr.mq_curmsgs = 0;
  attr.mq_flags   = 0;

  strncpy(mq_adpt->cname, name ? name : "null",
          sizeof(mq_adpt->cname) - 1);

  ret = file_mq_open(&mq_adpt->mq, mq_adpt->name,
                     O_RDWR | O_CREAT, 0644, &attr);
  if (ret < 0)
    {
      wlerr("ERROR: Failed to create mqueue\n");
      kmm_free(mq_adpt);
      return beken_errno_trans(ret);
    }

  mq_adpt->msgsize = message_size;
  *queue           = mq_adpt;

  return beken_errno_trans(ret);
}

bk_err_t rtos_push_to_queue(beken_queue_t *queue, void *message,
                            uint32_t timeout_ms)
{
  int ret;
  struct mq_adpt_s *mq_adpt = (struct mq_adpt_s *)*queue;

  if (timeout_ms == BEKEN_WAIT_FOREVER || timeout_ms == 0)
    {
      ret = file_mq_send(&mq_adpt->mq, (const char *)message,
                         mq_adpt->msgsize, 0);
      if (ret < 0)
        {
          wlerr("Failed to send message to mqueue error=%d\n", ret);
        }
    }
  else
    {
      struct timespec timeout;

      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_sec  += timeout_ms / 1000;
      timeout.tv_nsec += (timeout_ms % 1000) * 1000 * 1000;

      if (timeout.tv_nsec >= 1000000000)
        {
          timeout.tv_sec  += timeout.tv_nsec / 1000000000;
          timeout.tv_nsec %= 1000000000;
        }

      ret = file_mq_timedsend(&mq_adpt->mq, (const char *)message,
                              mq_adpt->msgsize, 0, &timeout);
      if (ret < 0)
        {
          wlerr("Failed to timedsend message to mqueue error=%d\n", ret);
        }
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_push_to_queue_front(beken_queue_t *queue, void *message,
                                  uint32_t timeout_ms)
{
  /* NuttX mq does not support front insertion; fall back to normal send */

  return rtos_push_to_queue(queue, message, timeout_ms);
}

bk_err_t rtos_pop_from_queue(beken_queue_t *queue, void *message,
                             uint32_t timeout_ms)
{
  int ret;
  struct mq_adpt_s *mq_adpt = (struct mq_adpt_s *)*queue;

  if (timeout_ms == BEKEN_WAIT_FOREVER)
    {
      ret = file_mq_receive(&mq_adpt->mq, message,
                            mq_adpt->msgsize, 0);
      if (ret < 0)
        {
          wlerr("Failed to receive message from mqueue error=%d\n", ret);
        }
    }
  else
    {
      struct timespec timeout;

      clock_gettime(CLOCK_REALTIME, &timeout);
      timeout.tv_sec  += timeout_ms / 1000;
      timeout.tv_nsec += (timeout_ms % 1000) * 1000 * 1000;

      if (timeout.tv_nsec >= 1000000000)
        {
          timeout.tv_sec  += timeout.tv_nsec / 1000000000;
          timeout.tv_nsec %= 1000000000;
        }

      ret = file_mq_timedreceive(&mq_adpt->mq, message,
                                 mq_adpt->msgsize, 0, &timeout);
      if (ret < 0)
        {
          wlerr("Failed to timedreceive message from mqueue error=%d\n",
                ret);
        }
    }

  return ret > 0 ? BK_OK : BK_FAIL;
}

bk_err_t rtos_deinit_queue(beken_queue_t *queue)
{
  struct mq_adpt_s *mq_adpt = (struct mq_adpt_s *)*queue;

  file_mq_close(&mq_adpt->mq);
  file_mq_unlink(mq_adpt->name);
  kmm_free(mq_adpt);

  return OK;
}

bool rtos_is_queue_empty(beken_queue_t *queue)
{
  struct mq_adpt_s *mq_adpt = (struct mq_adpt_s *)*queue;
  struct mq_attr mq_attr;

  file_mq_getattr(&mq_adpt->mq, &mq_attr);

  return (mq_attr.mq_curmsgs == 0);
}

bool rtos_is_queue_full(beken_queue_t *queue)
{
  struct mq_adpt_s *mq_adpt = (struct mq_adpt_s *)*queue;
  struct mq_attr mq_attr;

  file_mq_getattr(&mq_adpt->mq, &mq_attr);

  return (mq_attr.mq_curmsgs == mq_attr.mq_maxmsg);
}

bool rtos_reset_queue(beken_queue_t *queue)
{
  /* NuttX does not have a direct mq reset; return success */

  return true;
}

/****************************************************************************
 * Public Functions - Event Flags
 ****************************************************************************/

bk_err_t rtos_init_event_flags(beken_event_t *event_flags)
{
  sem_t *sem;

  sem = kmm_malloc(sizeof(sem_t));
  if (!sem)
    {
      return BK_FAIL;
    }

  nxsem_init(sem, 0, 0);
  *event_flags = sem;

  return BK_OK;
}

beken_event_flags_t rtos_wait_for_event_flags(
    beken_event_t *event_flags,
    uint32_t flags_to_wait_for,
    beken_bool_t clear_set_flags,
    beken_event_flags_wait_option_t wait_option,
    uint32_t timeout_ms)
{
  sem_t *sem = (sem_t *)(*event_flags);
  int ret;

  if (timeout_ms == BEKEN_WAIT_FOREVER)
    {
      ret = nxsem_wait(sem);
    }
  else if (timeout_ms == 0)
    {
      ret = nxsem_trywait(sem);
    }
  else
    {
      ret = nxsem_tickwait(sem, MSEC2TICK(timeout_ms));
    }

  return ret == OK ? flags_to_wait_for : 0;
}

void rtos_set_event_flags(beken_event_t *event_flags,
                          uint32_t flags_to_set)
{
  sem_t *sem = (sem_t *)(*event_flags);

  nxsem_post(sem);
}

beken_event_flags_t rtos_clear_event_flags(beken_event_t *event_flags,
                                           uint32_t flags_to_clear)
{
  /* Stub - return 0 */

  return 0;
}

beken_event_flags_t rtos_sync_event_flags(beken_event_t *event_flags,
                                          uint32_t flags_to_set,
                                          uint32_t flags_to_wait_for,
                                          uint32_t timeout_ms)
{
  rtos_set_event_flags(event_flags, flags_to_set);

  return rtos_wait_for_event_flags(event_flags, flags_to_wait_for,
                                   true, WAIT_FOR_ANY_EVENT, timeout_ms);
}

bk_err_t rtos_deinit_event_flags(beken_event_t *event_flags)
{
  sem_t *sem = (sem_t *)(*event_flags);

  nxsem_destroy(sem);
  kmm_free(sem);

  return BK_OK;
}

/****************************************************************************
 * Public Functions - Timer
 ****************************************************************************/

bk_err_t rtos_init_timer(beken_timer_t *timer, uint32_t time_ms,
                         timer_handler_t function, void *arg)
{
  struct timer_adpt *timer_apt = NULL;

  timer_apt = kmm_malloc(sizeof(struct timer_adpt));
  if (!timer_apt)
    {
      wlerr("ERROR: Failed to malloc struct timer_adpt\n");
      return BK_FAIL;
    }

  memset(timer_apt, 0x0, sizeof(struct timer_adpt));
  memset(timer, 0x0, sizeof(beken_timer_t));

  timer_apt->delay  = MSEC2TICK(time_ms);
  timer_apt->priv   = timer;
  timer_apt->repeat = true;

  timer->handle   = timer_apt;
  timer->function = function;
  timer->arg      = arg;

  return BK_OK;
}

bk_err_t rtos_start_timer(beken_timer_t *timer)
{
  int ret;
  struct timer_adpt *timer_apt = (struct timer_adpt *)timer->handle;

  ret = wd_start(&timer_apt->wdog, timer_apt->delay,
                 os_timer_callback, (wdparm_t)timer_apt);
  if (ret != OK)
    {
      wlerr("ERROR: Failed to start timer:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_stop_timer(beken_timer_t *timer)
{
  int ret = OK;
  struct timer_adpt *timer_apt;

  if (timer && timer->handle)
    {
      timer_apt = (struct timer_adpt *)timer->handle;
      if (WDOG_ISACTIVE(&timer_apt->wdog))
        {
          ret = wd_cancel(&timer_apt->wdog);
          if (ret != OK)
            {
              wlerr("WARN: Failed to cancel timer:%d\n", ret);
            }
        }
    }

  return BK_OK;
}

bk_err_t rtos_reload_timer(beken_timer_t *timer)
{
  rtos_stop_timer(timer);
  rtos_start_timer(timer);

  return OK;
}

bk_err_t rtos_deinit_timer(beken_timer_t *timer)
{
  rtos_stop_timer(timer);
  kmm_free(timer->handle);

  return OK;
}

bool rtos_is_timer_init(beken_timer_t *timer)
{
  return (timer->handle) ? true : false;
}

bool rtos_is_timer_running(beken_timer_t *timer)
{
  struct timer_adpt *timer_apt;

  timer_apt = timer->handle;

  return (wd_gettime(&timer_apt->wdog) > 0);
}

bk_err_t rtos_init_oneshot_timer(beken2_timer_t *timer, uint32_t time_ms,
                                 timer_2handler_t function,
                                 void *larg, void *rarg)
{
  struct timer_adpt *timer_apt = NULL;

  timer_apt = kmm_malloc(sizeof(struct timer_adpt));
  if (!timer_apt)
    {
      wlerr("ERROR: Failed to malloc struct timer_adpt\n");
      return BK_FAIL;
    }

  memset(timer_apt, 0x0, sizeof(struct timer_adpt));
  memset(timer, 0x0, sizeof(beken2_timer_t));

  timer_apt->delay  = MSEC2TICK(time_ms);
  timer_apt->priv   = timer;
  timer_apt->repeat = false;

  timer->handle   = timer_apt;
  timer->function = function;
  timer->left_arg = larg;
  timer->right_arg = rarg;

  return BK_OK;
}

bk_err_t rtos_start_oneshot_timer(beken2_timer_t *timer)
{
  int ret;
  struct timer_adpt *timer_apt = (struct timer_adpt *)timer->handle;

  ret = wd_start(&timer_apt->wdog, timer_apt->delay,
                 os_timer_callback, (wdparm_t)timer_apt);
  if (ret != OK)
    {
      wlerr("ERROR: Failed to start timer:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_stop_oneshot_timer(beken2_timer_t *timer)
{
  int ret = OK;
  struct timer_adpt *timer_apt;

  if (timer && timer->handle)
    {
      timer_apt = (struct timer_adpt *)timer->handle;
      if (WDOG_ISACTIVE(&timer_apt->wdog))
        {
          ret = wd_cancel(&timer_apt->wdog);
          if (ret != OK)
            {
              wlerr("WARN: Failed to stop one-shot timer:%d\n", ret);
            }
        }
    }

  return BK_OK;
}

bk_err_t rtos_oneshot_reload_timer(beken2_timer_t *timer)
{
  rtos_stop_oneshot_timer(timer);
  rtos_start_oneshot_timer(timer);

  return OK;
}

bk_err_t rtos_change_period(beken_timer_t *timer, uint32_t time_ms)
{
  struct timer_adpt *timer_apt = (struct timer_adpt *)timer->handle;

  rtos_stop_timer(timer);
  timer_apt->delay = MSEC2TICK(time_ms);
  rtos_start_timer(timer);

  return OK;
}

bk_err_t rtos_oneshot_reload_timer_ex(beken2_timer_t *timer,
                                      uint32_t time_ms,
                                      timer_2handler_t function,
                                      void *larg, void *rarg)
{
  struct timer_adpt *timer_apt = (struct timer_adpt *)timer->handle;

  rtos_stop_oneshot_timer(timer);

  timer_apt->delay  = MSEC2TICK(time_ms);
  timer_apt->priv   = timer;
  timer_apt->repeat = false;

  timer->handle   = timer_apt;
  timer->function = function;
  timer->left_arg = larg;
  timer->right_arg = rarg;

  rtos_start_oneshot_timer(timer);

  return OK;
}

bk_err_t rtos_deinit_oneshot_timer(beken2_timer_t *timer)
{
  rtos_stop_oneshot_timer(timer);
  kmm_free(timer->handle);

  return BK_OK;
}

bool rtos_is_oneshot_timer_running(beken2_timer_t *timer)
{
  struct timer_adpt *timer_apt;

  if (!timer || !timer->handle)
    {
      return false;
    }

  timer_apt = timer->handle;

  return (wd_gettime(&timer_apt->wdog) > 0);
}

bool rtos_is_oneshot_timer_init(beken2_timer_t *timer)
{
  return (timer && timer->handle) ? true : false;
}

uint32_t rtos_get_timer_expiry_time(beken_timer_t *timer)
{
  struct timer_adpt *timer_apt;

  if (!timer || !timer->handle)
    {
      return 0;
    }

  timer_apt = timer->handle;

  return wd_gettime(&timer_apt->wdog);
}

/* Memory functions — provided here for NuttX (libbk_rtos.a excluded to
 * avoid FreeRTOS-based implementations conflicting with ours). */

void *os_malloc(size_t size)
{
  void *p = kmm_malloc(size);
  if (!p)
    {
      wlerr("ERROR: Failed to malloc %zu\n", size);
    }

  return p;
}

void os_free(void *ptr)
{
  kmm_free(ptr);
}

void *os_zalloc(size_t size)
{
  return kmm_zalloc(size);
}

void *os_sram_malloc(size_t size)
{
  return kmm_malloc(size);
}

void *os_sram_calloc(size_t a, size_t b)
{
  return kmm_calloc(a, b);
}

void *os_sram_zalloc(size_t size)
{
  return kmm_zalloc(size);
}

void *os_realloc(void *ptr, size_t size)
{
  return kmm_realloc(ptr, size);
}

void *psram_malloc(size_t size)
{
  return kmm_malloc(size);
}

void *psram_zalloc(size_t size)
{
  return kmm_zalloc(size);
}

void *os_malloc_debug(const char *func_name, int line, size_t size,
                      int need_zero)
{
  (void)func_name;
  (void)line;

  return need_zero ? kmm_zalloc(size) : kmm_malloc(size);
}

void *os_sram_malloc_debug(const char *func_name, int line, size_t size,
                           int need_zero)
{
  (void)func_name;
  (void)line;

  return need_zero ? kmm_zalloc(size) : kmm_malloc(size);
}

void *psram_malloc_debug(const char *func_name, int line, size_t size,
                         int need_zero)
{
  (void)func_name;
  (void)line;

  return need_zero ? kmm_zalloc(size) : kmm_malloc(size);
}

void *os_free_debug(const char *func_name, int line, void *ptr)
{
  (void)func_name;
  (void)line;

  kmm_free(ptr);
  return NULL;
}

void os_dump_memory_stats(uint32_t start_tick, uint32_t ticks_since_malloc,
                          const char *task)
{
  (void)start_tick;
  (void)ticks_since_malloc;
  (void)task;
}

void *os_malloc_wifi_buffer(size_t size)
{
  return kmm_malloc(size);
}

uint32_t bk_psram_heap_get_used_count(void)
{
  return 0;
}

void bk_psram_heap_get_used_state(void)
{
}

void bk_psram_heap_dump_data(void)
{
}

INT32 os_memcmp(const void *s1, const void *s2, UINT32 n)
{
  return memcmp(s1, s2, (unsigned int)n);
}

void *os_memmove(void *out, const void *in, UINT32 n)
{
  return memmove(out, in, n);
}

void *os_memcpy(void *out, const void *in, UINT32 n)
{
  return memcpy(out, in, n);
}

int os_memcmp_const(const void *a, const void *b, size_t len)
{
  return memcmp(a, b, len);
}

void *os_memset(void *b, int c, UINT32 len)
{
  return (void *)memset(b, c, (unsigned int)len);
}

/****************************************************************************
 * Public Functions - String
 ****************************************************************************/

UINT32 os_strlen(const char *str)
{
  return (UINT32)strlen(str);
}

INT32 os_strcmp(const char *s1, const char *s2)
{
  return (INT32)strcmp(s1, s2);
}

INT32 os_strncmp(const char *s1, const char *s2, const UINT32 n)
{
  return (INT32)strncmp(s1, s2, (size_t)n);
}

INT32 os_snprintf(char *buf, UINT32 size, const char *fmt, ...)
{
  va_list ap;
  INT32 ret;

  va_start(ap, fmt);
  ret = (INT32)vsnprintf(buf, (size_t)size, fmt, ap);
  va_end(ap);

  return ret;
}

INT32 os_vsnprintf(char *buf, UINT32 size, const char *fmt, va_list ap)
{
  return (INT32)vsnprintf(buf, (size_t)size, fmt, ap);
}

char *os_strncpy(char *out, const char *in, const UINT32 n)
{
  return strncpy(out, in, (size_t)n);
}

UINT32 os_strtoul(const char *nptr, char **endptr, int base)
{
  return (UINT32)strtoul(nptr, endptr, base);
}

char *os_strcpy(char *out, const char *in)
{
  return strcpy(out, in);
}

char *os_strchr(const char *s, int c)
{
  return strchr(s, c);
}

char *os_strdup(const char *s)
{
  size_t len;
  char *d;

  if (s == NULL)
    {
      return NULL;
    }

  len = strlen(s) + 1;
  d = kmm_malloc(len);
  if (d == NULL)
    {
      return NULL;
    }

  memcpy(d, s, len);

  return d;
}

int os_strcasecmp(const char *s1, const char *s2)
{
  return strcasecmp(s1, s2);
}

int os_strncasecmp(const char *s1, const char *s2, size_t n)
{
  return strncasecmp(s1, s2, n);
}

char *os_strrchr(const char *s, int c)
{
  return strrchr(s, c);
}

char *os_strstr(const char *haystack, const char *needle)
{
  return strstr(haystack, needle);
}

size_t os_strlcpy(char *dest, const char *src, size_t siz)
{
  return strlcpy(dest, src, siz);
}

/****************************************************************************
 * Public Functions - Delay
 ****************************************************************************/

bk_err_t rtos_delay_milliseconds(uint32_t num_ms)
{
  bk7258_os_delay_ms(num_ms);
  return BK_OK;
}

/****************************************************************************
 * Public Functions - Scheduler / System
 ****************************************************************************/

bool rtos_is_in_interrupt_context(void)
{
  return up_interrupt_context();
}

bool rtos_local_irq_disabled(void)
{
  /* Stub: assume IRQs are not disabled */

  return false;
}

bool rtos_is_scheduler_suspended(void)
{
  /* Stub - assume not suspended */

  return false;
}

bool rtos_is_scheduler_started(void)
{
  return OSINIT_IDLELOOP();
}

char *rtos_get_name(void)
{
  return "NuttX";
}

char *rtos_get_version(void)
{
  return "1.x";
}

void rtos_start_scheduler(void)
{
  /* NuttX scheduler is already running */
}

void rtos_shutdown(void)
{
  up_irq_disable();
  for (; ; );
}

/****************************************************************************
 * Public Functions - Heap Info
 ****************************************************************************/

size_t rtos_get_total_heap_size(void)
{
  struct mallinfo info;

  info = kmm_mallinfo();

  return (info.fordblks + info.uordblks);
}

size_t rtos_get_free_heap_size(void)
{
  struct mallinfo info;

  info = kmm_mallinfo();

  return info.fordblks;
}

size_t rtos_get_minimum_free_heap_size(void)
{
  struct mallinfo info;

  info = kmm_mallinfo();

  return (info.fordblks + info.uordblks - info.usmblks);
}

size_t rtos_get_psram_total_heap_size(void)
{
  /* No PSRAM heap on this port */

  return 0;
}

size_t rtos_get_psram_free_heap_size(void)
{
  return 0;
}

size_t rtos_get_psram_minimum_free_heap_size(void)
{
  return 0;
}

/* bk_psram_heap_* provided by libos_source.a — do not define here */

/****************************************************************************
 * Public Functions - Scheduler Lock (FreeRTOS vTaskSuspendAll equivalent)
 ****************************************************************************/

void vTaskSuspendAll(void)
{
  sched_lock();
}

int xTaskResumeAll(void)
{
  sched_unlock();
  return 1;
}

/****************************************************************************
 * Public Functions - Logging Stubs
 *
 * The SDK logging macros (BK_LOGI/W/E) call bk_printf_ext() and
 * bk_printf_raw().  Provide NuttX-backed implementations.
 ****************************************************************************/

void bk_printf_ext(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  (void)level;

  if (tag)
    {
      syslog(LOG_INFO, "[%s] ", tag);
    }

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_printf_raw(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  (void)level;
  (void)tag;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_null_printf(const char *fmt, ...)
{
  /* Intentionally empty - suppress output */
}

void bk_mem_dump(const char *title, uint32_t start, uint32_t len)
{
  /* Stub */
}

/****************************************************************************
 * Public Functions - Event Extended (rtos_ext.h)
 ****************************************************************************/

bk_err_t rtos_init_event_ex(rtos_event_ext_t *event)
{
  sem_t *sem;

  sem = kmm_malloc(sizeof(sem_t));
  if (!sem)
    {
      return BK_FAIL;
    }

  nxsem_init(sem, 0, 0);
  event->event_semaphore = sem;
  event->event_flag = 0;

  return BK_OK;
}

bk_err_t rtos_deinit_event_ex(rtos_event_ext_t *event)
{
  sem_t *sem = (sem_t *)event->event_semaphore;

  nxsem_destroy(sem);
  kmm_free(sem);

  return BK_OK;
}

bk_err_t rtos_set_event_ex(rtos_event_ext_t *event, u32 event_flag)
{
  sem_t *sem = (sem_t *)event->event_semaphore;

  event->event_flag |= event_flag;
  nxsem_post(sem);

  return BK_OK;
}

u32 rtos_wait_event_ex(rtos_event_ext_t *event, u32 event_flag,
                       u32 any_event, u32 timeout)
{
  sem_t *sem = (sem_t *)event->event_semaphore;
  int ret;

  if (timeout == BEKEN_WAIT_FOREVER)
    {
      ret = nxsem_wait(sem);
    }
  else if (timeout == 0)
    {
      ret = nxsem_trywait(sem);
    }
  else
    {
      ret = nxsem_tickwait(sem, MSEC2TICK(timeout));
    }

  if (ret == OK)
    {
      u32 flags = event->event_flag & event_flag;
      event->event_flag &= ~event_flag;
      return flags;
    }

  return 0;
}

/****************************************************************************
 * Public Functions - HISR (rtos_ext.h)
 ****************************************************************************/

bk_err_t rtos_create_hisr(rtos_hisr_cb_t *hisr_cb, high_isr_t hisr,
                          void *param, u32 hisr_id)
{
  if (!hisr_cb)
    {
      return BK_FAIL;
    }

  hisr_cb->hisr       = hisr;
  hisr_cb->hisr_param = param;
  hisr_cb->hisr_id    = hisr_id;
  hisr_cb->inited     = 1;

  return BK_OK;
}

bk_err_t rtos_activate_hisr(rtos_hisr_cb_t *hisr_cb)
{
  if (!hisr_cb || !hisr_cb->hisr)
    {
      return BK_FAIL;
    }

  hisr_cb->hisr(hisr_cb->hisr_param);

  return BK_OK;
}

void rtos_hisr_task(void *param)
{
  /* Stub - HISR runs inline in this adaptation */
}
