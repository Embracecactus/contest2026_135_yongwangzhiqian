/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_os_adapt.c
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
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <debug.h>
#include <pthread.h>
#include <sched.h>
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
#include <nuttx/mutex.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/kthread.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>
#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <nuttx/arch.h>
#include <nuttx/init.h>
#include <nuttx/tls.h>

#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif

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

/* The official FreeRTOS port dispatches software-timer callbacks from its
 * highest-priority timer daemon task, never from the tick ISR.  Keep the same
 * contract here: the watchdog only timestamps an expiry and a board-owned
 * kthread invokes the SDK callback.  The 3072-byte stack matches Beken's
 * configTIMER_TASK_STACK_DEPTH setting.
 */

#define BK7258_TIMER_SERVICE_NAME       "bk-sdk-timer"
#define BK7258_TIMER_SERVICE_PRIORITY   (SCHED_PRIORITY_DEFAULT + 10)
#define BK7258_TIMER_SERVICE_STACKSIZE  3072

/* Official v3.1.1.9 AP sdkconfig value.  NuttX owns the actual allocation;
 * this is only the requested stack size passed through the SDK static-task
 * compatibility entry points.
 */

#define BK7258_EVENT_TASK_STACKSIZE   3072u


#ifndef CONFIG_BK7258_AP_CORE
/* The official CP SDK profile prints on UART0, while this board's NuttX
 * console is UART1 (GPIO0/1).  Bluetooth uses bk_get_printf_port() to avoid
 * deinitializing the active console when its temporary DUT UART ownership is
 * released, so this wrapper must report the board wiring rather than the
 * SDK build-time default.
 */

#  define BK7258_NUTTX_CONSOLE_UART_PORT 1
#endif

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
#  define BK7258_OS_SDK_LOCK_FREE 0xf2eef2eeu
#  define BK7258_OS_SDK_LOCK_MAX_NEST 0xffu
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Timer adapter - bridges Beken timer to NuttX watchdog */

struct timer_adpt
{
  sq_entry_t    queue;             /* Timer-service queue linkage */
  struct wdog_s wdog;              /* NuttX watchdog handle */
  bool          repeat;            /* True if periodic timer */
  bool          enabled;           /* Logical SDK active state */
  bool          queued;            /* Queued to timer service */
  bool          deliver_pending;   /* Deliver the expired callback */
  bool          callback_running;  /* SDK callback is executing */
  bool          restart_pending;   /* Arm after queued/running callback */
  bool          delete_pending;    /* Free only in timer-service context */
  uint32_t      delay;             /* Timeout in ticks */
  void          *priv;             /* beken_timer_t / beken2_timer_t */
};

/* Message queue adapter */

struct mq_adpt_s
{
  struct file mq;           /* NuttX message queue handle */
  uint32_t    msgsize;      /* Message size in bytes */
  char        name[16];     /* Message queue name */
  char        cname[32];    /* Display name for debug */
};

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
/* Match the SDK SMP implementation of rtos_enter_critical(): first mask
 * interrupts on the current core, then serialize callers across cores with
 * a dedicated recursive-by-core lock.  The SDK spinlock is not equivalent to
 * NuttX spinlock_t: it records owner + nesting count, and SDK drivers legally
 * nest critical sections on one core.  This lock must remain independent
 * from NuttX's scheduler lock because rtos_disable_int() is an interrupt-mask
 * API, not a NuttX scheduler critical section.
 */

struct bk7258_os_sdk_lock_s
{
  uint32_t owner;
  uint32_t count;
};

static volatile struct bk7258_os_sdk_lock_s
  g_bk7258_sdk_critical_lock =
{
  .owner = BK7258_OS_SDK_LOCK_FREE,
  .count = 0
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static volatile bool g_bk7258_sdk_printf_enabled = true;
static sq_queue_t g_bk7258_timer_queue;
static sem_t g_bk7258_timer_sem = SEM_INITIALIZER(0);
static mutex_t g_bk7258_timer_init_lock = NXMUTEX_INITIALIZER;
static pid_t g_bk7258_timer_service_pid = -1;

#ifdef CONFIG_BK7258_BT_IPC
/* The immutable CP and AP BT IPC objects each create one long-lived binary
 * semaphore during bt_ipc_init().  Keep those objects out of the packet
 * heaps: a first raw-pointer HCI allocation can otherwise alias the matching
 * semaphore during the cold generation-1 startup sequence.  The init-scope
 * flag is asserted only around bt_ipc_init(), so every other SDK semaphore
 * keeps normal NuttX heap ownership.
 */

static sem_t g_bk7258_bt_ipc_send_sem;
static bool g_bk7258_bt_ipc_init_scope;
static bool g_bk7258_bt_ipc_send_sem_active;
static pid_t g_bk7258_bt_ipc_init_pid;

/* BT IPC exchanges heap pointers between processors and returns ownership
 * with HCI_FREE_PKT.  Keep a small, allocation-free tracker around that
 * narrow SDK call path while Wi-Fi/BT coexistence is being brought up.  It
 * turns a stale or duplicate cross-core free into evidence instead of
 * allowing the NuttX heap free list to be corrupted first.
 */

#define BK7258_BT_HEAP_TRACK_MAGIC  0x42544850u /* "BTHP" */
#define BK7258_BT_HEAP_TRACK_SLOTS  32u

struct bk7258_bt_heap_track_entry_s
{
  void    *pointer;
  uint32_t size;
  uint32_t alloc_line;
  uint32_t free_line;
  uint32_t alloc_sequence;
  uint32_t free_sequence;
  bool     active;
};

struct bk7258_bt_heap_track_s
{
  uint32_t magic;
  uint32_t sequence;
  uint32_t active;
  uint32_t high_water;
  uint32_t invalid_frees;
  uint32_t duplicate_allocations;
  void    *last_invalid_pointer;
  uint32_t last_invalid_line;
  struct bk7258_bt_heap_track_entry_s
    entries[BK7258_BT_HEAP_TRACK_SLOTS];
};

struct bk7258_bt_heap_track_s g_bk7258_bt_heap_track =
{
  .magic = BK7258_BT_HEAP_TRACK_MAGIC
};
#endif

/* The official BK7258 SMP SDK implements port_disable_interrupts_flag() with
 * PRIMASK (__get_PRIMASK() + __disable_irq()) and restores the saved PRIMASK
 * verbatim.  NuttX up_irq_save() uses BASEPRI on ARMv8-M, so use the exact
 * SDK contract here instead of substituting enter_critical_section().
 */

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
static inline irqstate_t bk7258_os_local_irq_save(void)
{
  irqstate_t flags;

  __asm volatile
    (
      "mrs %0, primask\n"
      "cpsid i\n"
      : "=r" (flags)
      :
      : "memory"
    );

  return flags;
}

static inline void bk7258_os_local_irq_restore(irqstate_t flags)
{
  __asm volatile
    (
      "msr primask, %0\n"
      :
      : "r" (flags)
      : "memory"
    );
}

static void bk7258_os_sdk_lock(void)
{
  uint32_t cpu = (uint32_t)up_cpu_index();
  uint32_t expected;
  uint32_t owner;

  for (;;)
    {
      owner = __atomic_load_n(&g_bk7258_sdk_critical_lock.owner,
                              __ATOMIC_ACQUIRE);
      if (owner == cpu)
        {
          /* Local PRIMASK is already set, so another context on this core
           * cannot race the recursive count update.
           */

          DEBUGASSERT(g_bk7258_sdk_critical_lock.count > 0 &&
                      g_bk7258_sdk_critical_lock.count <
                        BK7258_OS_SDK_LOCK_MAX_NEST);
          g_bk7258_sdk_critical_lock.count++;
          return;
        }

      if (owner == BK7258_OS_SDK_LOCK_FREE)
        {
          expected = BK7258_OS_SDK_LOCK_FREE;
          if (__atomic_compare_exchange_n(
                &g_bk7258_sdk_critical_lock.owner, &expected, cpu, false,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            {
              g_bk7258_sdk_critical_lock.count = 1;
              __asm volatile ("dmb sy" ::: "memory");
              return;
            }
        }

      /* Match the SDK spin_lock() wait model.  The owner releases with SEV,
       * so this does not consume CPU while the other AP core owns the lock.
       */

      __asm volatile ("wfe" ::: "memory");
    }
}

static void bk7258_os_sdk_unlock(void)
{
  uint32_t cpu = (uint32_t)up_cpu_index();

  DEBUGASSERT(__atomic_load_n(&g_bk7258_sdk_critical_lock.owner,
                              __ATOMIC_RELAXED) == cpu);
  DEBUGASSERT(g_bk7258_sdk_critical_lock.count > 0 &&
              g_bk7258_sdk_critical_lock.count <=
                BK7258_OS_SDK_LOCK_MAX_NEST);

  g_bk7258_sdk_critical_lock.count--;
  if (g_bk7258_sdk_critical_lock.count == 0)
    {
      __atomic_store_n(&g_bk7258_sdk_critical_lock.owner,
                       BK7258_OS_SDK_LOCK_FREE, __ATOMIC_RELEASE);
      __asm volatile ("dsb sy; sev" ::: "memory");
    }
}
#endif

static void bk7258_os_delay_ms(uint32_t milliseconds)
{
  /* NuttX must never block the IDLE task.  SDK clock/UART setup can request
   * delays from up_initialize(), before nx_bringup() has created any other
   * runnable task.  Use an architecture busy wait until normal scheduling is
   * active, and whenever this adapter is called from IDLE or interrupt context.
   */

  if (!OSINIT_IDLELOOP() ||
      nxsched_gettid() == IDLE_PROCESS_ID ||
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

/* All state and queue changes below are serialized with NuttX's scheduler
 * critical section.  wd_timer() already holds that recursive lock while it
 * invokes bk7258_timer_expiry(), which also makes the object lifetime safe
 * on the AP SMP image.
 */

static void bk7258_timer_expiry(wdparm_t arg);

static void bk7258_timer_queue_locked(struct timer_adpt *timer_apt,
                                      bool deliver)
{
  timer_apt->deliver_pending |= deliver;
  if (!timer_apt->queued)
    {
      timer_apt->queued = true;
      sq_addlast(&timer_apt->queue, &g_bk7258_timer_queue);
      (void)nxsem_post(&g_bk7258_timer_sem);
    }
}

static int bk7258_timer_start_locked(struct timer_adpt *timer_apt)
{
  int ret;

  if (WDOG_ISACTIVE(&timer_apt->wdog))
    {
      (void)wd_cancel(&timer_apt->wdog);
    }

  ret = wd_start(&timer_apt->wdog, timer_apt->delay,
                 bk7258_timer_expiry, (wdparm_t)timer_apt);
  if (ret == OK)
    {
      timer_apt->restart_pending = false;
    }
  else
    {
      timer_apt->enabled = false;
    }

  return ret;
}

static void bk7258_timer_expiry(wdparm_t arg)
{
  struct timer_adpt *timer_apt = (struct timer_adpt *)arg;
  irqstate_t flags;

  flags = enter_critical_section();

  if (!timer_apt->delete_pending && timer_apt->enabled)
    {
      /* A one-shot becomes inactive at expiry.  A periodic timer remains
       * logically active and is rearmed by the service before its callback,
       * matching the official FreeRTOS timer-daemon cadence.
       */

      if (!timer_apt->repeat)
        {
          timer_apt->enabled = false;
        }

      bk7258_timer_queue_locked(timer_apt, true);
    }

  leave_critical_section(flags);
}

static int bk7258_timer_service(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  for (;;)
    {
      struct timer_adpt *timer_apt;
      timer_handler_t periodic = NULL;
      timer_2handler_t oneshot = NULL;
      void *arg = NULL;
      void *left_arg = NULL;
      void *right_arg = NULL;
      sq_entry_t *entry;
      irqstate_t flags;
      bool deliver;
      bool free_timer = false;

      (void)nxsem_wait_uninterruptible(&g_bk7258_timer_sem);

      flags = enter_critical_section();
      entry = sq_remfirst(&g_bk7258_timer_queue);
      if (entry == NULL)
        {
          leave_critical_section(flags);
          continue;
        }

      /* queue is the first member of timer_adpt. */

      timer_apt = (struct timer_adpt *)entry;
      timer_apt->queued = false;

      if (timer_apt->delete_pending)
        {
          free_timer = true;
          deliver = false;
        }
      else
        {
          deliver = timer_apt->deliver_pending;
          timer_apt->deliver_pending = false;

          if (deliver)
            {
              timer_apt->callback_running = true;
              if (timer_apt->repeat)
                {
                  beken_timer_t *timer = timer_apt->priv;

                  periodic = timer->function;
                  arg = timer->arg;

                  if (timer_apt->enabled)
                    {
                      (void)bk7258_timer_start_locked(timer_apt);
                    }
                }
              else
                {
                  beken2_timer_t *timer = timer_apt->priv;

                  if (timer->beken_magic == BEKEN_MAGIC_WORD)
                    {
                      oneshot = timer->function;
                      left_arg = timer->left_arg;
                      right_arg = timer->right_arg;
                    }
                }
            }
          else if (timer_apt->enabled && timer_apt->restart_pending)
            {
              /* A stop/reload raced an expiry already queued to this
               * service.  Suppress the stale callback and start the new
               * period from the explicit reload operation.
               */

              (void)bk7258_timer_start_locked(timer_apt);
            }
        }

      leave_critical_section(flags);

      if (free_timer)
        {
          kmm_free(timer_apt);
          continue;
        }

      if (periodic != NULL)
        {
          periodic(arg);
        }
      else if (oneshot != NULL)
        {
          oneshot(left_arg, right_arg);
        }

      if (deliver)
        {
          flags = enter_critical_section();
          timer_apt->callback_running = false;

          if (timer_apt->delete_pending)
            {
              /* A periodic timer can expire again while its callback runs.
               * In that case the queued delete entry owns the final free;
               * freeing here would leave a dangling timer-service node.
               */

              if (!timer_apt->queued)
                {
                  free_timer = true;
                }
            }
          else if (timer_apt->enabled && timer_apt->restart_pending)
            {
              /* One-shot callbacks and periodic callbacks that explicitly
               * reload/retime themselves defer the new arm until the old
               * callback has returned.
               */

              (void)bk7258_timer_start_locked(timer_apt);
            }

          leave_critical_section(flags);

          if (free_timer)
            {
              kmm_free(timer_apt);
            }
        }
    }

  return EXIT_SUCCESS;
}

static int bk7258_timer_service_initialize(void)
{
  int ret;
  pid_t pid;

  ret = nxmutex_lock(&g_bk7258_timer_init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_timer_service_pid > 0)
    {
      nxmutex_unlock(&g_bk7258_timer_init_lock);
      return OK;
    }

  pid = kthread_create(BK7258_TIMER_SERVICE_NAME,
                       BK7258_TIMER_SERVICE_PRIORITY,
                       BK7258_TIMER_SERVICE_STACKSIZE,
                       bk7258_timer_service, NULL);
  if (pid <= 0)
    {
      nxmutex_unlock(&g_bk7258_timer_init_lock);
      return pid < 0 ? (int)pid : -ENOMEM;
    }

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  {
    cpu_set_t cpuset = (cpu_set_t)1u;

    /* The AP's SDK-facing services are owned by logical CPU0. */

    ret = sched_setaffinity(pid, sizeof(cpuset), &cpuset);
    if (ret < 0)
      {
        wlerr("WARN: Failed to pin SDK timer service: %d\n", ret);
      }
  }
#endif

  g_bk7258_timer_service_pid = pid;
  nxmutex_unlock(&g_bk7258_timer_init_lock);
  return OK;
}

static void bk7258_timer_delete_locked(struct timer_adpt *timer_apt)
{
  timer_apt->enabled = false;
  timer_apt->deliver_pending = false;
  timer_apt->restart_pending = false;
  timer_apt->delete_pending = true;

  if (WDOG_ISACTIVE(&timer_apt->wdog))
    {
      (void)wd_cancel(&timer_apt->wdog);
    }

  if (!timer_apt->queued && !timer_apt->callback_running)
    {
      bk7258_timer_queue_locked(timer_apt, false);
    }
}

/****************************************************************************
 * Public Functions - Interrupt Management
 ****************************************************************************/

uint32_t rtos_disable_int(void)
{
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  return (uint32_t)bk7258_os_local_irq_save();
#else
  return enter_critical_section();
#endif
}

void rtos_enable_int(uint32_t int_level)
{
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  bk7258_os_local_irq_restore((irqstate_t)int_level);
#else
  leave_critical_section(int_level);
#endif
}

/* These two entry points are part of the SDK ARM_CM33 port ABI.  Unlike
 * NuttX's normal critical-section API, the SDK contract saves and restores
 * PRIMASK verbatim.  Controller and exception objects call them directly.
 */

int port_disable_interrupts_flag(void)
{
  irqstate_t flags;

  __asm volatile
    (
      "mrs %0, primask\n"
      "cpsid i\n"
      : "=r" (flags)
      :
      : "memory"
    );

  return (int)flags;
}

void port_enable_interrupts_flag(int int_level)
{
  __asm volatile
    (
      "msr primask, %0\n"
      :
      : "r" ((irqstate_t)int_level)
      : "memory"
    );
}

uint32_t rtos_enter_critical(void)
{
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  irqstate_t flags = bk7258_os_local_irq_save();

  bk7258_os_sdk_lock();
  return (uint32_t)flags;
#else
  return enter_critical_section();
#endif
}

void rtos_exit_critical(uint32_t int_level)
{
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  bk7258_os_sdk_unlock();
  bk7258_os_local_irq_restore((irqstate_t)int_level);
#else
  leave_critical_section(int_level);
#endif
}

uint32_t rtos_before_sleep(void)
{
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  return rtos_disable_int();
#else
  return enter_critical_section();
#endif
}

void rtos_after_sleep(uint32_t int_level)
{
#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  rtos_enable_int(int_level);
#else
  leave_critical_section(int_level);
#endif
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

bk_err_t rtos_create_thread_static(beken_thread_t *thread, uint8_t priority,
                                   const char *name,
                                   beken_thread_function_t function,
                                   uint32_t stack_size,
                                   beken_thread_arg_t arg,
                                   void * const task_stack_buffer,
                                   void * const task_tcb_buffer,
                                   uint32_t core_id)
{
  bk_err_t ret;

  /* StaticTask_t and StackType_t are FreeRTOS-private storage types.  NuttX
   * owns its TCB and stack allocation, so the compatibility ABI intentionally
   * ignores those buffers while preserving the SDK-requested stack size.
   */

  (void)task_stack_buffer;
  (void)task_tcb_buffer;

  ret = rtos_create_thread(thread, priority, name, function, stack_size, arg);
  if (ret != BK_OK)
    {
      return ret;
    }

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  if (thread != NULL && core_id < CONFIG_SMP_NCPUS)
    {
      cpu_set_t cpuset = (cpu_set_t)1u << core_id;
      pid_t pid = (pid_t)(uintptr_t)*thread;

      if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) < 0)
        {
          task_delete(pid);
          *thread = NULL;
          return BK_FAIL;
        }
    }
#else
  (void)core_id;
#endif

  return BK_OK;
}

#ifdef CONFIG_BK7258_AP_CORE
void rtos_get_event_task_memory(void **task_tcb_buffer,
                                void **task_stack_buffer,
                                uint32_t *stack_size)
{
  /* See rtos_create_thread_static(): the two FreeRTOS storage pointers are
   * ABI placeholders only; the size remains the official AP v3.1.1.9 value.
   */

  if (task_tcb_buffer != NULL)
    {
      *task_tcb_buffer = NULL;
    }

  if (task_stack_buffer != NULL)
    {
      *task_stack_buffer = NULL;
    }

  if (stack_size != NULL)
    {
      *stack_size = BK7258_EVENT_TASK_STACKSIZE;
    }
}
#endif

bk_err_t rtos_smp_create_thread(beken_thread_t *thread, uint8_t priority,
                                const char *name,
                                beken_thread_function_t function,
                                uint32_t stack_size,
                                beken_thread_arg_t arg)
{
  beken_thread_t created = NULL;
  bk_err_t ret;

  ret = rtos_create_thread(&created, priority, name, function, stack_size,
                           arg);
  if (ret != BK_OK)
    {
      return ret;
    }

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_SMP)
  {
    cpu_set_t cpuset = (cpu_set_t)1u;
    pid_t pid = (pid_t)(uintptr_t)created;

    /* Vendor AP Wi-Fi callbacks and mailbox queues have one logical owner.
     * Keep them on AP logical CPU0 while native NuttX networking remains
     * free to schedule its own work on either logical CPU.
     */

    if (sched_setaffinity(pid, sizeof(cpuset), &cpuset) < 0)
      {
        wlerr("ERROR: Failed to pin SDK thread(%s) to AP CPU0\n", name);
        task_delete(pid);
        return BK_FAIL;
      }
  }
#endif

  if (thread)
    {
      *thread = created;
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

bk_err_t rtos_thread_set_priority(beken_thread_t *thread, int priority)
{
  struct sched_param param;
  pid_t pid;

  pid = thread ? (pid_t)(uintptr_t)*thread : nxsched_gettid();
  param.sched_priority = SCHED_PRIORITY_DEFAULT + 2 - priority;

  return sched_setparam(pid, &param) == OK ? BK_OK : BK_FAIL;
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
  pid_t tid = nxsched_gettid();

  return thread != NULL && tid == (pid_t)(uintptr_t)*thread;
}

beken_thread_t *rtos_get_current_thread(void)
{
  return (beken_thread_t *)(uintptr_t)nxsched_gettid();
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
#ifdef CONFIG_BK7258_BT_IPC
  bool static_sem = false;
  bool expected = false;

  if (max_count == 1 && init_count == 1 &&
      __atomic_load_n(&g_bk7258_bt_ipc_init_scope, __ATOMIC_ACQUIRE) &&
      __atomic_load_n(&g_bk7258_bt_ipc_init_pid, __ATOMIC_RELAXED) ==
        nxsched_gettid() &&
      __atomic_compare_exchange_n(&g_bk7258_bt_ipc_send_sem_active,
                                  &expected, true, false,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      sem = &g_bk7258_bt_ipc_send_sem;
      static_sem = true;
    }
#endif

  if (sem == NULL)
    {
      sem = kmm_malloc(sizeof(sem_t));
    }

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
#ifdef CONFIG_BK7258_BT_IPC
      if (static_sem)
        {
          __atomic_store_n(&g_bk7258_bt_ipc_send_sem_active, false,
                           __ATOMIC_RELEASE);
        }
      else
#endif
        {
          kmm_free(sem);
        }
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

#ifdef CONFIG_BK7258_BT_IPC
  if (sem == &g_bk7258_bt_ipc_send_sem)
    {
      __atomic_store_n(&g_bk7258_bt_ipc_send_sem_active, false,
                       __ATOMIC_RELEASE);
    }
  else
#endif
    {
      kmm_free(sem);
    }

  return beken_errno_trans(ret);
}

#ifdef CONFIG_BK7258_BT_IPC
void bk7258_os_bt_ipc_init_begin(void)
{
  __atomic_store_n(&g_bk7258_bt_ipc_init_pid, nxsched_gettid(),
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_bt_ipc_init_scope, true, __ATOMIC_RELEASE);
}

void bk7258_os_bt_ipc_init_end(void)
{
  __atomic_store_n(&g_bk7258_bt_ipc_init_scope, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_bt_ipc_init_pid, 0, __ATOMIC_RELAXED);
}
#endif

/* The immutable SDK lwIP port uses FreeRTOS binary-semaphore creation only
 * for its per-thread socket semaphore.  Map that narrow ABI to the same
 * NuttX semaphore object used by the normal rtos_* wrapper; reject any wider
 * FreeRTOS queue request instead of linking the SDK FreeRTOS kernel.
 */

void *xQueueGenericCreate(uint32_t length, uint32_t item_size,
                          uint8_t queue_type)
{
  beken_semaphore_t sem = NULL;

  (void)queue_type;
  if (length != 1 || item_size != 0 ||
      rtos_init_semaphore(&sem, 1) != BK_OK)
    {
      return NULL;
    }

  return sem;
}

void *xQueueCreateMutex(uint8_t queue_type)
{
  beken_semaphore_t sem = NULL;

  (void)queue_type;
  if (rtos_init_semaphore_ex(&sem, 1, 1) != BK_OK)
    {
      return NULL;
    }

  return sem;
}

int xQueueSemaphoreTake(void *queue, uint32_t ticks_to_wait)
{
  beken_semaphore_t sem = queue;
  uint32_t timeout_ms = ticks_to_wait;

  if (ticks_to_wait == UINT32_MAX)
    {
      timeout_ms = BEKEN_WAIT_FOREVER;
    }

  return rtos_get_semaphore(&sem, timeout_ms) == BK_OK ? 1 : 0;
}

int xQueueGenericSend(void *queue, const void *item,
                      uint32_t ticks_to_wait, int copy_position)
{
  beken_semaphore_t sem = queue;

  (void)item;
  (void)ticks_to_wait;
  (void)copy_position;
  return rtos_set_semaphore(&sem) == BK_OK ? 1 : 0;
}

void vQueueDelete(void *queue)
{
  beken_semaphore_t sem = queue;

  if (sem)
    {
      (void)rtos_deinit_semaphore(&sem);
    }
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

  if (timeout_ms == BEKEN_WAIT_FOREVER)
    {
      ret = file_mq_send(&mq_adpt->mq, (const char *)message,
                         mq_adpt->msgsize, 0);
      if (ret < 0)
        {
          wlerr("Failed to send message to mqueue error=%d\n", ret);
        }
    }
  else if (timeout_ms == 0)
    {
      /* BEKEN_NO_WAIT is zero in the official SDK.  file_mq_send() blocks
       * in task context when the queue is full, so use the tick-based kernel
       * API with a zero budget.  In interrupt context NuttX also fails with
       * -EAGAIN instead of sleeping.
       */

      ret = file_mq_ticksend(&mq_adpt->mq, (const char *)message,
                             mq_adpt->msgsize, 0, 0);
      if (ret < 0)
        {
          wlerr("Failed to send message to mqueue without waiting error=%d\n",
                ret);
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
  struct timer_adpt *timer_apt;
  int ret;

  if (timer == NULL)
    {
      return BK_FAIL;
    }

  ret = bk7258_timer_service_initialize();
  if (ret < 0)
    {
      wlerr("ERROR: Failed to create SDK timer service: %d\n", ret);
      return BK_FAIL;
    }

  timer_apt = kmm_zalloc(sizeof(struct timer_adpt));
  if (!timer_apt)
    {
      wlerr("ERROR: Failed to malloc struct timer_adpt\n");
      return BK_FAIL;
    }

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
  struct timer_adpt *timer_apt;
  irqstate_t flags;
  int ret = -EINVAL;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && !timer_apt->delete_pending)
    {
      timer_apt->enabled = true;
      if (timer_apt->queued || timer_apt->callback_running)
        {
          if (WDOG_ISACTIVE(&timer_apt->wdog))
            {
              (void)wd_cancel(&timer_apt->wdog);
            }

          timer_apt->restart_pending = true;
          ret = OK;
        }
      else
        {
          ret = bk7258_timer_start_locked(timer_apt);
        }
    }

  leave_critical_section(flags);

  if (ret != OK)
    {
      wlerr("ERROR: Failed to start timer:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_stop_timer(beken_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && !timer_apt->delete_pending)
    {
      timer_apt->enabled = false;
      timer_apt->deliver_pending = false;
      timer_apt->restart_pending = false;
      if (WDOG_ISACTIVE(&timer_apt->wdog))
        {
          (void)wd_cancel(&timer_apt->wdog);
        }
    }

  leave_critical_section(flags);
  return BK_OK;
}

bk_err_t rtos_reload_timer(beken_timer_t *timer)
{
  bk_err_t ret;

  (void)rtos_stop_timer(timer);
  ret = rtos_start_timer(timer);
  return ret;
}

bk_err_t rtos_deinit_timer(beken_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;

  if (timer == NULL)
    {
      return BK_OK;
    }

  flags = enter_critical_section();
  timer_apt = timer->handle;
  if (timer_apt != NULL)
    {
      /* Detach first, exactly as the official wrapper does before it queues
       * xTimerDelete().  The service owns the final free so self-deinit from
       * a callback cannot reinsert or free a live NuttX watchdog node.
       */

      timer->handle = NULL;
      bk7258_timer_delete_locked(timer_apt);
    }

  leave_critical_section(flags);

  return BK_OK;
}

bool rtos_is_timer_init(beken_timer_t *timer)
{
  irqstate_t flags;
  bool initialized;

  flags = enter_critical_section();
  initialized = timer != NULL && timer->handle != NULL;
  leave_critical_section(flags);
  return initialized;
}

bool rtos_is_timer_running(beken_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;
  bool running = false;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && !timer_apt->delete_pending)
    {
      running = timer_apt->enabled;
    }

  leave_critical_section(flags);
  return running;
}

bk_err_t rtos_init_oneshot_timer(beken2_timer_t *timer, uint32_t time_ms,
                                 timer_2handler_t function,
                                 void *larg, void *rarg)
{
  struct timer_adpt *timer_apt;
  int ret;

  if (timer == NULL)
    {
      return BK_FAIL;
    }

  ret = bk7258_timer_service_initialize();
  if (ret < 0)
    {
      wlerr("ERROR: Failed to create SDK timer service: %d\n", ret);
      return BK_FAIL;
    }

  timer_apt = kmm_zalloc(sizeof(struct timer_adpt));
  if (!timer_apt)
    {
      wlerr("ERROR: Failed to malloc struct timer_adpt\n");
      return BK_FAIL;
    }

  memset(timer, 0x0, sizeof(beken2_timer_t));

  timer_apt->delay  = MSEC2TICK(time_ms);
  timer_apt->priv   = timer;
  timer_apt->repeat = false;

  timer->handle   = timer_apt;
  timer->function = function;
  timer->left_arg = larg;
  timer->right_arg = rarg;
  timer->beken_magic = BEKEN_MAGIC_WORD;

  return BK_OK;
}

bk_err_t rtos_start_oneshot_timer(beken2_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;
  int ret = -EINVAL;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && !timer_apt->delete_pending)
    {
      timer_apt->enabled = true;
      if (timer_apt->queued || timer_apt->callback_running)
        {
          if (WDOG_ISACTIVE(&timer_apt->wdog))
            {
              (void)wd_cancel(&timer_apt->wdog);
            }

          timer_apt->restart_pending = true;
          ret = OK;
        }
      else
        {
          ret = bk7258_timer_start_locked(timer_apt);
        }
    }

  leave_critical_section(flags);

  if (ret != OK)
    {
      wlerr("ERROR: Failed to start timer:%d\n", ret);
    }

  return beken_errno_trans(ret);
}

bk_err_t rtos_stop_oneshot_timer(beken2_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && !timer_apt->delete_pending)
    {
      timer_apt->enabled = false;
      timer_apt->deliver_pending = false;
      timer_apt->restart_pending = false;
      if (WDOG_ISACTIVE(&timer_apt->wdog))
        {
          (void)wd_cancel(&timer_apt->wdog);
        }
    }

  leave_critical_section(flags);
  return BK_OK;
}

bk_err_t rtos_oneshot_reload_timer(beken2_timer_t *timer)
{
  bk_err_t ret;

  (void)rtos_stop_oneshot_timer(timer);
  ret = rtos_start_oneshot_timer(timer);
  return ret;
}

bk_err_t rtos_change_period(beken_timer_t *timer, uint32_t time_ms)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;

  (void)rtos_stop_timer(timer);
  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL)
    {
      timer_apt->delay = MSEC2TICK(time_ms);
    }

  leave_critical_section(flags);
  return timer_apt != NULL ? rtos_start_timer(timer) : BK_FAIL;
}

bk_err_t rtos_oneshot_reload_timer_ex(beken2_timer_t *timer,
                                      uint32_t time_ms,
                                      timer_2handler_t function,
                                      void *larg, void *rarg)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;

  (void)rtos_stop_oneshot_timer(timer);

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL)
    {
      timer_apt->delay  = MSEC2TICK(time_ms);
      timer_apt->priv   = timer;
      timer_apt->repeat = false;

      timer->function = function;
      timer->left_arg = larg;
      timer->right_arg = rarg;
      timer->beken_magic = BEKEN_MAGIC_WORD;
    }

  leave_critical_section(flags);

  return timer_apt != NULL ? rtos_start_oneshot_timer(timer) : BK_FAIL;
}

bk_err_t rtos_deinit_oneshot_timer(beken2_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;

  if (timer == NULL)
    {
      return BK_OK;
    }

  flags = enter_critical_section();
  timer_apt = timer->handle;
  if (timer_apt != NULL)
    {
      timer->handle = NULL;
      timer->function = NULL;
      timer->left_arg = NULL;
      timer->right_arg = NULL;
      timer->beken_magic = 0;
      bk7258_timer_delete_locked(timer_apt);
    }

  leave_critical_section(flags);

  return BK_OK;
}

bool rtos_is_oneshot_timer_running(beken2_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;
  bool running = false;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && !timer_apt->delete_pending)
    {
      running = timer_apt->enabled;
    }

  leave_critical_section(flags);
  return running;
}

bool rtos_is_oneshot_timer_init(beken2_timer_t *timer)
{
  irqstate_t flags;
  bool initialized;

  flags = enter_critical_section();
  initialized = timer != NULL && timer->handle != NULL;
  leave_critical_section(flags);
  return initialized;
}

uint32_t rtos_get_timer_expiry_time(beken_timer_t *timer)
{
  struct timer_adpt *timer_apt;
  irqstate_t flags;
  uint32_t remaining = 0;

  flags = enter_critical_section();
  timer_apt = timer != NULL ? timer->handle : NULL;
  if (timer_apt != NULL && WDOG_ISACTIVE(&timer_apt->wdog))
    {
      remaining = (uint32_t)wd_gettime(&timer_apt->wdog);
    }

  leave_critical_section(flags);
  return remaining;
}

#if defined(CONFIG_BK7258_WIFI_VNET) && !defined(CONFIG_BK7258_AP_CORE)
static bool g_bk7258_wifi_zero_malloc;
static pid_t g_bk7258_wifi_malloc_owner_pid;

extern void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
  void *mem = __real_malloc(size);

  /* The official v3.1.1.9 CP starts Wi-Fi against a fresh, zero-filled
   * FreeRTOS heap.  Some of its Wi-Fi objects, including the station table,
   * use malloc() and consume embedded list heads as the previous state
   * without first clearing the allocation.  NuttX starts Wi-Fi later, so a
   * returned block can contain payload left by an earlier allocation.
   * Preserve the official startup contract only for the thread executing
   * bk_wifi_init().  Other CP threads retain normal NuttX malloc semantics
   * even while that short initialization window is active.
   */

  if (mem != NULL &&
      __atomic_load_n(&g_bk7258_wifi_zero_malloc, __ATOMIC_ACQUIRE) &&
      __atomic_load_n(&g_bk7258_wifi_malloc_owner_pid,
                      __ATOMIC_RELAXED) == nxsched_gettid())
    {
      memset(mem, 0, size);
    }

  return mem;
}

void bk7258_os_wifi_malloc_zero_begin(void)
{
  __atomic_store_n(&g_bk7258_wifi_malloc_owner_pid, nxsched_gettid(),
                   __ATOMIC_RELAXED);
  __atomic_store_n(&g_bk7258_wifi_zero_malloc, true, __ATOMIC_RELEASE);
}

void bk7258_os_wifi_malloc_zero_end(void)
{
  __atomic_store_n(&g_bk7258_wifi_zero_malloc, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_wifi_malloc_owner_pid, 0,
                   __ATOMIC_RELAXED);
}
#endif

/* Memory functions — provided here for NuttX (libbk_rtos.a excluded to
 * avoid FreeRTOS-based implementations conflicting with ours). */

#ifdef CONFIG_BK7258_BT_IPC
static bool bk7258_os_bt_heap_call(const char *func_name)
{
  return func_name != NULL && strncmp(func_name, "bt_ipc_", 7) == 0;
}

static void bk7258_os_bt_heap_alloc(void *pointer, size_t size,
                                    uint32_t line)
{
  struct bk7258_bt_heap_track_entry_s *entry = NULL;
  struct bk7258_bt_heap_track_entry_s *free_entry = NULL;
  irqstate_t flags;
  uint32_t sequence;
  unsigned int i;
  bool duplicate = false;

  if (pointer == NULL)
    {
      return;
    }

  flags = enter_critical_section();
  sequence = ++g_bk7258_bt_heap_track.sequence;

  for (i = 0; i < BK7258_BT_HEAP_TRACK_SLOTS; i++)
    {
      struct bk7258_bt_heap_track_entry_s *candidate =
        &g_bk7258_bt_heap_track.entries[i];

      if (candidate->active && candidate->pointer == pointer)
        {
          entry = candidate;
          duplicate = true;
          break;
        }

      if (!candidate->active &&
          (free_entry == NULL || candidate->pointer == pointer))
        {
          free_entry = candidate;
          if (candidate->pointer == pointer)
            {
              /* Prefer the previous slot for the same recycled address. */

              continue;
            }
        }
    }

  if (entry == NULL)
    {
      entry = free_entry;
    }

  if (entry != NULL)
    {
      if (!entry->active)
        {
          g_bk7258_bt_heap_track.active++;
          if (g_bk7258_bt_heap_track.active >
              g_bk7258_bt_heap_track.high_water)
            {
              g_bk7258_bt_heap_track.high_water =
                g_bk7258_bt_heap_track.active;
            }
        }

      entry->pointer = pointer;
      entry->size = (uint32_t)size;
      entry->alloc_line = line;
      entry->free_line = 0;
      entry->alloc_sequence = sequence;
      entry->free_sequence = 0;
      entry->active = true;
    }

  if (duplicate)
    {
      g_bk7258_bt_heap_track.duplicate_allocations++;
    }

  leave_critical_section(flags);

  if (duplicate)
    {
      wlerr("ERROR: BT IPC duplicate allocation %p at line %" PRIu32 "\n",
            pointer, line);
    }
}

static bool bk7258_os_bt_heap_free(void *pointer, uint32_t line)
{
  struct bk7258_bt_heap_track_entry_s *entry = NULL;
  irqstate_t flags;
  uint32_t sequence;
  unsigned int i;

  if (pointer == NULL)
    {
      return true;
    }

  flags = enter_critical_section();
  sequence = ++g_bk7258_bt_heap_track.sequence;

  for (i = 0; i < BK7258_BT_HEAP_TRACK_SLOTS; i++)
    {
      if (g_bk7258_bt_heap_track.entries[i].active &&
          g_bk7258_bt_heap_track.entries[i].pointer == pointer)
        {
          entry = &g_bk7258_bt_heap_track.entries[i];
          break;
        }
    }

  if (entry != NULL)
    {
      entry->free_line = line;
      entry->free_sequence = sequence;
      entry->active = false;
      g_bk7258_bt_heap_track.active--;
      leave_critical_section(flags);
      return true;
    }

  g_bk7258_bt_heap_track.invalid_frees++;
  g_bk7258_bt_heap_track.last_invalid_pointer = pointer;
  g_bk7258_bt_heap_track.last_invalid_line = line;
  leave_critical_section(flags);

  wlerr("ERROR: Refusing stale BT IPC free %p at line %" PRIu32 "\n",
        pointer, line);
  return false;
}
#endif

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
#ifdef CONFIG_BK7258_PSRAM
  if (bk7258_psram_address(ptr))
    {
      if (bk7258_psram_heap_contains(ptr))
        {
          bk7258_psram_free(ptr);
        }
      else
        {
          wlerr("ERROR: Refusing foreign PSRAM free %p\n", ptr);
        }

      return;
    }
#endif

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
#ifdef CONFIG_BK7258_PSRAM
  if (bk7258_psram_address(ptr))
    {
      return bk7258_psram_realloc(ptr, size);
    }
#endif

  return kmm_realloc(ptr, size);
}

void *psram_malloc(size_t size)
{
#ifdef CONFIG_BK7258_PSRAM
  return bk7258_psram_malloc(size);
#else
  return kmm_malloc(size);
#endif
}

void *psram_zalloc(size_t size)
{
#ifdef CONFIG_BK7258_PSRAM
  return bk7258_psram_zalloc(size);
#else
  return kmm_zalloc(size);
#endif
}

void *psram_realloc(void *ptr, size_t size)
{
#ifdef CONFIG_BK7258_PSRAM
  return bk7258_psram_realloc(ptr, size);
#else
  return kmm_realloc(ptr, size);
#endif
}

void *bk_psram_realloc(void *ptr, size_t size)
{
  return psram_realloc(ptr, size);
}

void *os_malloc_debug(const char *func_name, int line, size_t size,
                      int need_zero)
{
  void *pointer;

  pointer = need_zero ? kmm_zalloc(size) : kmm_malloc(size);
#ifdef CONFIG_BK7258_BT_IPC
  if (bk7258_os_bt_heap_call(func_name))
    {
      bk7258_os_bt_heap_alloc(pointer, size, (uint32_t)line);
    }
#else
  (void)func_name;
  (void)line;
#endif

  return pointer;
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

  return need_zero ? psram_zalloc(size) : psram_malloc(size);
}

void *os_free_debug(const char *func_name, int line, void *ptr)
{
#ifdef CONFIG_BK7258_BT_IPC
  if (bk7258_os_bt_heap_call(func_name) &&
      !bk7258_os_bt_heap_free(ptr, (uint32_t)line))
    {
      return NULL;
    }
#else
  (void)func_name;
  (void)line;
#endif

  os_free(ptr);
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
#ifdef CONFIG_BK7258_PSRAM
  return (uint32_t)bk7258_psram_used_size();
#else
  return 0;
#endif
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
#ifdef CONFIG_BK7258_PSRAM
  return bk7258_psram_total_size();
#else
  return 0;
#endif
}

size_t rtos_get_psram_free_heap_size(void)
{
#ifdef CONFIG_BK7258_PSRAM
  return bk7258_psram_free_size();
#else
  return 0;
#endif
}

size_t rtos_get_psram_minimum_free_heap_size(void)
{
#ifdef CONFIG_BK7258_PSRAM
  return bk7258_psram_minimum_free_size();
#else
  return 0;
#endif
}

/* libos_source.a remains excluded; the N14 board heap supplies the SDK
 * psram statistics ABI above.
 */

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

static bool bk7258_sdk_log_is_sensitive(const char *fmt)
{
  return fmt != NULL &&
         (strstr(fmt, "password") != NULL ||
          strstr(fmt, "Password") != NULL ||
          strstr(fmt, "ssid") != NULL ||
          strstr(fmt, "SSID") != NULL ||
          strstr(fmt, "psk") != NULL ||
          strstr(fmt, "PSK") != NULL);
}

void bk_printf_ext(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  if (!g_bk7258_sdk_printf_enabled || bk7258_sdk_log_is_sensitive(fmt))
    {
      return;
    }

  (void)level;

  if (tag)
    {
      syslog(LOG_INFO, "[%s] ", tag);
    }

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_vprintf_ext(int level, char *tag, const char *fmt, va_list ap)
{
  if (!g_bk7258_sdk_printf_enabled || bk7258_sdk_log_is_sensitive(fmt))
    {
      return;
    }

  (void)level;

  if (tag)
    {
      syslog(LOG_INFO, "[%s] ", tag);
    }

  vsyslog(LOG_INFO, fmt, ap);
}

char *vTaskName(void)
{
  return (char *)getprogname();
}

/* The official PSRAM driver uses the early/static logging entry point while
 * it is bringing the controller up.  NuttX syslog is already synchronous,
 * so its wrapper has the same behavior as bk_printf_ext().
 */

void bk_printf_static_block(int level, char *tag, const char *fmt, ...)
{
  va_list ap;

  if (!g_bk7258_sdk_printf_enabled || bk7258_sdk_log_is_sensitive(fmt))
    {
      return;
    }

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

  if (!g_bk7258_sdk_printf_enabled || bk7258_sdk_log_is_sensitive(fmt))
    {
      return;
    }

  (void)level;
  (void)tag;

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_printf(const char *fmt, ...)
{
  va_list ap;

  if (!g_bk7258_sdk_printf_enabled || bk7258_sdk_log_is_sensitive(fmt))
    {
      return;
    }

  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
}

void bk_null_printf(const char *fmt, ...)
{
  /* Intentionally empty - suppress output */
}

void bk_set_printf_enable(uint8_t enable)
{
  g_bk7258_sdk_printf_enabled = enable != 0;
}

/* NuttX syslog writes synchronously from these SDK adapter callbacks.  The
 * official implementation only changes its async-shell policy, which is not
 * present in this image, so the NuttX equivalent is deliberately a no-op and
 * always reports synchronous mode.
 */

void bk_set_printf_sync(uint8_t enable)
{
  (void)enable;
}

int bk_get_printf_sync(void)
{
  return 1;
}

int bk_get_printf_port(void)
{
#ifndef CONFIG_BK7258_AP_CORE
  return BK7258_NUTTX_CONSOLE_UART_PORT;
#else
  return CONFIG_UART_PRINT_PORT;
#endif
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
