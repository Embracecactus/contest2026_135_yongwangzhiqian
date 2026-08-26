/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_wdt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 hardware watchdog NuttX lower-half driver — SDK wrapper.
 *
 * Calls bk_wdt_* / bk_aon_wdt_* SDK APIs.  Zero register access.
 *
 * The bootloader arms both APB + AON WDTs (~8 s).  The CP reset entry closes
 * both before nx_start(); this driver is registered after bounded AP
 * autostart and manages the APB WDT through the NuttX automonitor.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_WDT

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/timers/watchdog.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>

#include "bk7258_wdt.h"

/* SDK API headers */

#include <driver/wdt.h>
#include <components/system.h>
#include <driver/aon_wdt.h>
#include <driver/timer.h>

/* Present in the manifest-pinned official v3.1.1.9 driver archive but not
 * exported by its reduced public header bundle. */

extern void aon_pmu_drv_wdt_change_not_rosc_clk(void);
extern void aon_pmu_drv_wdt_rst_dev_enable(void);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WDT_DEFAULT_TIMEOUT_MS  8000u
#define BK7258_WDT_MAX_TIMEOUT_MS      0xFFFFu

/* The BK7258 SDK exposes no pre-timeout interrupt for either watchdog, so
 * the xTS watchdog contract (panic with context before the hardware reset)
 * is delivered through a NuttX software timer armed slightly ahead of the
 * hardware expiry.  The handler prints the context and routes the reset
 * through the AON watchdog so the recorded reset reason stays in the WDT
 * family.  A hard irq-disabled spin (xTS case -r 1) cannot wake a software
 * timer; the plain hardware reset then still reports SYS_RWDT. */

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC

#define BK7258_WDT_PRETIMEOUT_ARM_GUARD_MS 100u

/* Flash flag sector: reserved_data tail, untouched by OTA/boot flows. */

#define BK7258_WDT_FLAG_ADDR   0x509000u /* usr_config tail, SDK-writable */
#define BK7258_WDT_FLAG_SECTOR 4096u /* BK7258 flash sector size */
#define BK7258_WDT_FLAG_MAGIC  0x57445447u /* "WTDG" */

#endif

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
#  define BK7258_WDT_FAULT_MAGIC       0x46445742u /* "BWDF" */
#  define BK7258_WDT_FAULT_VERSION     1u
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_wdt_lowerhalf_s
{
  struct watchdog_lowerhalf_s wdt_lh;  /* Must be first */
  mutex_t lock;                        /* Serializes the two-key feed/start
                                        * sequences against the second
                                        * feeder context (automonitor vs
                                        * client ioctl); the SDK assumes a
                                        * single feeder owner. */
  xcpt_t handler;                      /* Pre-expiry capture callback */
  struct work_s work;                  /* Deferred capture notification */
  uint32_t timeout;                    /* Current timeout in ms */
  clock_t  last_feed;                  /* Tick of the most recent feed */
  bool     started;                    /* WDT is armed */
};

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
enum bk7258_wdt_fault_e
{
  BK7258_WDT_FAULT_NONE = 0,
  BK7258_WDT_FAULT_TIMER_STOP,
  BK7258_WDT_FAULT_AON_STOP,
  BK7258_WDT_FAULT_WDT_STOP,
  BK7258_WDT_FAULT_WDT_START,
};

struct bk7258_wdt_fault_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t result;
  int32_t init_timer_stop;
  int32_t init_aon_stop;
  int32_t init_retry;
  int32_t failed_stop;
  uint32_t active_after_failed_stop;
  int32_t retry_stop;
  uint32_t active_after_retry_stop;
  int32_t restart;
  uint32_t active_after_failed_restore;
  int32_t recovery_start;
  uint32_t final_active;
};
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status);
static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout);
static xcpt_t bk7258_wdt_capture(struct watchdog_lowerhalf_s *lower,
                                 CODE xcpt_t newhandler);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_bk7258_wdt_ops =
{
  .start      = bk7258_wdt_start,
  .stop       = bk7258_wdt_stop,
  .keepalive  = bk7258_wdt_keepalive,
  .getstatus  = bk7258_wdt_getstatus,
  .settimeout = bk7258_wdt_settimeout,
  .capture    = bk7258_wdt_capture,
};

static struct bk7258_wdt_lowerhalf_s g_bk7258_wdt;
static bool g_bk7258_wdt_pm_resume;

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
volatile struct bk7258_wdt_fault_diag_s g_bk7258_wdt_fault_diag;
static volatile enum bk7258_wdt_fault_e g_bk7258_wdt_fault_next;

static bool bk7258_wdt_fault_take(enum bk7258_wdt_fault_e fault)
{
  irqstate_t flags;
  bool take;

  flags = up_irq_save();
  take = g_bk7258_wdt_fault_next == fault;
  if (take)
    {
      g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_NONE;
    }

  up_irq_restore(flags);
  return take;
}

static bk_err_t bk7258_wdt_sdk_timer_stop(timer_id_t timer_id)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_TIMER_STOP))
    {
      return (bk_err_t)-1;
    }

  return bk_timer_stop(timer_id);
}

static bk_err_t bk7258_wdt_sdk_aon_stop(void)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_AON_STOP))
    {
      return (bk_err_t)-1;
    }

  return bk_aon_wdt_stop();
}

static bk_err_t bk7258_wdt_sdk_stop(void)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_WDT_STOP))
    {
      return (bk_err_t)-1;
    }

  return bk_wdt_stop();
}

static bk_err_t bk7258_wdt_sdk_start(uint32_t timeout)
{
  if (bk7258_wdt_fault_take(BK7258_WDT_FAULT_WDT_START))
    {
      return (bk_err_t)-1;
    }

  return bk_wdt_start(timeout);
}
#else
#  define bk7258_wdt_sdk_timer_stop bk_timer_stop
#  define bk7258_wdt_sdk_aon_stop   bk_aon_wdt_stop
#  define bk7258_wdt_sdk_stop       bk_wdt_stop
#  define bk7258_wdt_sdk_start      bk_wdt_start
#endif

/****************************************************************************
 * Private: pre-timeout panic hook
 ****************************************************************************/

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC

static struct wdog_s g_bk7258_wdt_pretimeout;

static void bk7258_wdt_capture_notify(void *arg)
{
  FAR struct bk7258_wdt_lowerhalf_s *priv =
    (FAR struct bk7258_wdt_lowerhalf_s *)arg;

  if (priv != NULL && priv->handler != NULL)
    {
      priv->handler(0, NULL, priv);
    }
}

static void bk7258_wdt_pretimeout_expired(wdparm_t arg)
{
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;
  uint32_t timeout = (uint32_t)arg;

  if (priv->handler != NULL)
    {
      /* A capture client registered through WDIOC_CAPTURE: defer the
       * notification to the LPWORK queue - the client callback may take
       * semaphores, which is illegal in this timer-interrupt context.
       * The armed APB watchdog remains the last-resort reset source. */

      work_queue(LPWORK, &priv->work, bk7258_wdt_capture_notify,
                 priv, 0);
      return;
    }

  /* Runs in timer-interrupt context: no flash access is possible here.
   * The death cause was already recorded by the task-context keepalive
   * path (mirroring the SDK AP-side feed stamping); this dump plus the
   * imminent hardware expiry completes the xTS watchdog contract. */

  syslog(LOG_CRIT,
         "BK7258 WDT PRETIMEOUT panic: no keepalive, timeout=%" PRIu32
         " ms; forcing whole-device reset\n", timeout);

  /* This is a deliberate direct-syslog crash-path exception: debug macros
   * can be compiled out, while the xTS contract requires this final reason.
   * Flush the interrupt buffer through the channel's non-blocking force
   * operation before the AON watchdog takes the whole device down. */

  syslog_flush();

  /* The plain APB expiry resets only the CP core and wedges the next
   * bring-up behind a surviving AP image, so finish with the AON watchdog
   * PMU route: a clean whole-device reset.  The death cause is already in
   * the reserved flash sector from the keepalive path. */

  bk7258_wdt_force_system_reset();
}

static void bk7258_wdt_pretimeout_arm(uint32_t timeout)
{
  uint32_t margin = CONFIG_BK7258_WDT_PRETIMEOUT_MARGIN_MS;

  if (timeout <= margin + BK7258_WDT_PRETIMEOUT_ARM_GUARD_MS)
    {
      /* Too short to split; rely on the plain hardware reset. */

      wd_cancel(&g_bk7258_wdt_pretimeout);
      return;
    }

  wd_start(&g_bk7258_wdt_pretimeout, MSEC2TICK(timeout - margin),
           bk7258_wdt_pretimeout_expired, (wdparm_t)timeout);
}

static void bk7258_wdt_pretimeout_cancel(void)
{
  wd_cancel(&g_bk7258_wdt_pretimeout);
}

#else

#define bk7258_wdt_pretimeout_arm(timeout)   ((void)(timeout))
#define bk7258_wdt_pretimeout_cancel()       ((void)0)

#endif

static xcpt_t bk7258_wdt_capture(struct watchdog_lowerhalf_s *lower,
                                 CODE xcpt_t newhandler)
{
  FAR struct bk7258_wdt_lowerhalf_s *priv =
    (FAR struct bk7258_wdt_lowerhalf_s *)lower;
  irqstate_t flags;
  CODE xcpt_t oldhandler;

  flags = up_irq_save();
  oldhandler = priv->handler;
  priv->handler = newhandler;
  up_irq_restore(flags);
  return oldhandler;
}

/****************************************************************************
 * Private: pending-cause flash flag (task context only)
 ****************************************************************************/

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC

static bool s_flag_stamped;

static void bk7258_wdt_flag_stamp(void)
{
  extern int bk7258_ota_flash_initialize(void);
  uint32_t page[2] = { BK7258_WDT_FLAG_MAGIC, RESET_SOURCE_WATCHDOG };
  int ret;

  /* Hot path: once the sector carries the magic, skip all flash access so
   * the keepalive latency stays inside the xTS -r3 tolerance. */

  if (s_flag_stamped)
    {
      return;
    }

  ret = bk7258_ota_flash_initialize();
  if (ret == OK)
    {
      ret = bk_flash_erase_sector(BK7258_WDT_FLAG_ADDR);
    }

  if (ret == OK)
    {
      ret = bk_flash_write_bytes(BK7258_WDT_FLAG_ADDR,
                                 (const uint8_t *)page, sizeof(page));
    }

  if (ret == OK)
    {
      s_flag_stamped = true;
      syslog(LOG_INFO,
             "BOTA FLAG stamped @%08lx magic=%08x cause=%08x\n",
             (unsigned long)BK7258_WDT_FLAG_ADDR, page[0], page[1]);
    }
  else
    {
      syslog(LOG_ERR, "BOTA FLAG stamp FAILED ret=%d addr=%08lx\n",
             ret, (unsigned long)BK7258_WDT_FLAG_ADDR);
    }
}

static void bk7258_wdt_flag_withdraw(void)
{
  uint32_t current;

  if (bk_flash_read_bytes(BK7258_WDT_FLAG_ADDR, (uint8_t *)&current,
                          sizeof(current)) == BK_OK &&
      current == BK7258_WDT_FLAG_MAGIC)
    {
      (void)bk_flash_erase_sector(BK7258_WDT_FLAG_ADDR);
    }

  s_flag_stamped = false;
}

#else

#define bk7258_wdt_flag_stamp()    ((void)0)
#define bk7258_wdt_flag_withdraw() ((void)0)

#endif

/****************************************************************************
 * Private: lower-half operations
 ****************************************************************************/

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;
  int ret = OK;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return -EINVAL;
    }

  if (bk7258_wdt_sdk_start(priv->timeout) != BK_OK)
    {
      wderr("ERROR: bk_wdt_start failed\n");
      ret = -EIO;
    }
  else
    {
      priv->last_feed = clock_systime_ticks();
      priv->started = true;
      bk7258_wdt_flag_stamp();
      wdinfo("started, timeout=%" PRIu32 " ms\n", priv->timeout);
    }

  bk7258_wdt_pretimeout_arm(priv->timeout);
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;
  int ret = OK;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return -EINVAL;
    }

  if (bk7258_wdt_sdk_stop() != BK_OK)
    {
      wderr("ERROR: bk_wdt_stop failed\n");
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  bk7258_wdt_pretimeout_cancel();
  bk7258_wdt_flag_withdraw();

  /* The NuttX lower half owns the APB watchdog.  Commit its stopped state
   * after that hardware transition succeeds, even if the independent AON
   * watchdog subsequently reports an error.
   */

  priv->started = false;
  if (bk7258_wdt_sdk_aon_stop() != BK_OK)
    {
      wderr("ERROR: bk_aon_wdt_stop failed\n");
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  priv->last_feed = 0;
  wdinfo("stopped\n");
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return -EINVAL;
    }

  /* The SDK feed performs a two-key register sequence; concurrent access
   * from a second feeder context can corrupt one attempt, and a lost feed
   * cascades into starvation because later key writes stay unbalanced.
   * Retry once before reporting failure. */

  if (bk_wdt_feed() != BK_OK && bk_wdt_feed() != BK_OK)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  /* Hot path: no flash access here.  The death-cause flag was already
   * written once at arm time; stamping per-feed would stall the LPWORK
   * thread past the SYSTICK compensation window and panic the timer
   * proxy's phase check. */

  priv->last_feed = clock_systime_ticks();
  bk7258_wdt_pretimeout_arm(priv->timeout);
  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (status == NULL)
    {
      return -EINVAL;
    }

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return -EINVAL;
    }

  status->flags = WDFLAGS_RESET;
  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  status->timeout = priv->timeout;

  /* The APB WDT counter is not readable; derive timeleft from the tick of
   * the most recent keepalive so xTS -r 3 sees a monotonic countdown. */

  if (priv->started && priv->last_feed != 0)
    {
      /* Pure 32-bit tick math: avoid libgcc's __udivmoddi4, which has been
       * observed faulting when the division runs while the system timer
       * interrupt is also active on this profile. */

      uint32_t elapsed_ticks = (uint32_t)(clock_systime_ticks() -
                                          priv->last_feed);
      uint32_t elapsed_ms = elapsed_ticks *
                            (1000u / CLOCKS_PER_SEC);

      status->timeleft = elapsed_ms >= priv->timeout ?
                         0 : priv->timeout - elapsed_ms;
    }
  else
    {
      status->timeleft = priv->timeout;
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;
  uint32_t previous;

  if (nxmutex_lock(&priv->lock) < 0)
    {
      return -EINVAL;
    }

  if (timeout == 0)
    {
      nxmutex_unlock(&priv->lock);
      return -EINVAL;
    }

  if (timeout > BK7258_WDT_MAX_TIMEOUT_MS)
    {
      timeout = BK7258_WDT_MAX_TIMEOUT_MS;
    }

  previous = priv->timeout;
  priv->timeout = timeout;

  /* If already running, re-arm with new period.
   * bk_wdt_start() internally does soft_reset + key sequence. */

  if (priv->started)
    {
      if (bk7258_wdt_sdk_start(priv->timeout) != BK_OK)
        {
          priv->timeout = previous;
          nxmutex_unlock(&priv->lock);
          return -EIO;
        }

      priv->last_feed = clock_systime_ticks();
      bk7258_wdt_pretimeout_arm(priv->timeout);
      bk7258_wdt_flag_stamp();
    }

  wdinfo("timeout set to %" PRIu32 " ms\n", priv->timeout);
  nxmutex_unlock(&priv->lock);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_wdt_initialize
 *
 * Description:
 *   Initialize the BK7258 WDT and register as /dev/watchdog0.
 *
 *   Keep AON WDT disabled and register the APB WDT for NuttX automonitor.
 *   The CP reset entry has already closed both bootloader watchdogs before
 *   nx_start(), so registration establishes a fresh OS-owned timeout.
 *
 *   APB WDT is managed by NuttX automonitor (CONFIG_WATCHDOG_AUTOMONITOR):
 *   register triggers start + periodic keepalive via work queue.
 *
 ****************************************************************************/

int bk7258_wdt_initialize(void)
{
  static bool s_inited;
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;
  void *handle;
  bk_err_t err;
  int ret;

  if (s_inited)
    {
      return OK;
    }

  ret = nxmutex_init(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Initialize the SDK WDT state, then stop its TIMER_ID2 feeder.  NuttX
   * automonitor owns periodic keepalive through bk_wdt_feed(). */

  err = bk_timer_driver_init();
  if (err != BK_OK)
    {
      wderr("ERROR: bk_timer_driver_init failed\n");
      return -EIO;
    }

  if (!bk_wdt_is_driver_inited())
    {
      err = bk_wdt_driver_init();
      if (err != BK_OK)
        {
          wderr("ERROR: bk_wdt_driver_init failed\n");
          return -EIO;
        }
    }

  err = bk7258_wdt_sdk_timer_stop(TIMER_ID2);
  if (err != BK_OK)
    {
      wderr("ERROR: failed to stop SDK WDT feeder timer\n");
      return -EIO;
    }

  /* Stop the bootloader's AON WDT before registering the NuttX lower-half.
   * AON WDT is not managed by the NuttX watchdog framework. */

  err = bk7258_wdt_sdk_aon_stop();
  if (err != BK_OK)
    {
      wderr("ERROR: failed to stop boot AON watchdog\n");
      return -EIO;
    }

  priv->wdt_lh.ops = &g_bk7258_wdt_ops;
  priv->timeout    = BK7258_WDT_DEFAULT_TIMEOUT_MS;
  priv->started    = false;

  handle = watchdog_register("/dev/watchdog0",
                             (struct watchdog_lowerhalf_s *)priv);
  if (handle == NULL)
    {
      wderr("ERROR: watchdog_register failed\n");
      return -ENOMEM;
    }

  s_inited = true;

  wdinfo("BK7258 WDT registered, default timeout=%" PRIu32 " ms\n",
         priv->timeout);
  return OK;
}

int bk7258_wdt_service(void)
{
  if (!g_bk7258_wdt.started)
    {
      return OK;
    }

  return bk_wdt_feed() == BK_OK ? OK : -EIO;
}

void bk7258_wdt_force_system_reset(void)
{
  irqstate_t flags;

  /* CONFIG_NMI_WDT_EN makes the SDK bk_wdt_force_reboot() deliberately raise
   * an NMI so FreeRTOS can dump state before resetting.  NuttX owns that NMI
   * vector, so calling it only records a fault and never reaches a whole-SoC
   * reset.  Use the SDK's AON watchdog and PMU reset routing directly: this
   * is the same hardware reset source without depending on a FreeRTOS trap. */

  flags = up_irq_save();
  (void)flags;
  aon_pmu_drv_wdt_change_not_rosc_clk();
  aon_pmu_drv_wdt_rst_dev_enable();
  (void)bk_aon_wdt_set_period(10u);

  for (;;)
    {
    }
}

#ifdef CONFIG_BK7258_WDT_PRETIMEOUT_PANIC
int bk7258_wdt_take_pending_reset_cause(uint32_t *reason)
{
  uint32_t page[2];
  int pending;

  if (bk_flash_read_bytes(BK7258_WDT_FLAG_ADDR, (uint8_t *)page,
                          sizeof(page)) != BK_OK)
    {
      return 0;
    }

  pending = page[0] == BK7258_WDT_FLAG_MAGIC;
  if (pending)
    {
      *reason = page[1];
      (void)bk_flash_erase_sector(BK7258_WDT_FLAG_ADDR);
    }

  return pending;
}
#endif

void bk7258_wdt_pm_prepare(void)
{
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;

  g_bk7258_wdt_pm_resume = priv->started;
  if (priv->started)
    {
      /* Feed immediately before the immutable low-voltage leaf closes the
       * APB watchdog behind the NuttX lower half.
       */

      if (bk_wdt_feed() != BK_OK)
        {
          wderr("ERROR: watchdog feed before PM transition failed\n");
        }
    }
}

void bk7258_wdt_pm_restore(void)
{
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;

  if (g_bk7258_wdt_pm_resume)
    {
      if (bk7258_wdt_sdk_start(priv->timeout) != BK_OK)
        {
          /* The low-voltage leaf closed the hardware watchdog.  Do not
           * continue advertising ACTIVE after a failed restore.
           */

          priv->started = false;
          wderr("ERROR: watchdog restore after PM transition failed\n");
        }
    }

  g_bk7258_wdt_pm_resume = false;
}

#ifdef CONFIG_BK7258_WDT_FAULT_INJECTION
int bk7258_wdt_fault_validate(void)
{
  volatile struct bk7258_wdt_fault_diag_s *diag =
    &g_bk7258_wdt_fault_diag;
  struct watchdog_status_s status;
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;
  int ret;

  memset((void *)diag, 0, sizeof(*diag));
  diag->magic = BK7258_WDT_FAULT_MAGIC;
  diag->version = BK7258_WDT_FAULT_VERSION;
  diag->size = sizeof(*diag);
  diag->state = 1;

  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_TIMER_STOP;
  ret = bk7258_wdt_initialize();
  diag->init_timer_stop = ret;
  if (ret != -EIO)
    {
      goto failed;
    }

  diag->state = 2;
  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_AON_STOP;
  ret = bk7258_wdt_initialize();
  diag->init_aon_stop = ret;
  if (ret != -EIO)
    {
      goto failed;
    }

  diag->state = 3;
  ret = bk7258_wdt_initialize();
  diag->init_retry = ret;
  if (ret < 0)
    {
      goto failed;
    }

  diag->state = 4;
  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_WDT_STOP;
  ret = bk7258_wdt_stop(&priv->wdt_lh);
  diag->failed_stop = ret;
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->active_after_failed_stop =
    (status.flags & WDFLAGS_ACTIVE) != 0;
  if (ret != -EIO || diag->active_after_failed_stop == 0)
    {
      goto failed;
    }

  diag->state = 5;
  ret = bk7258_wdt_stop(&priv->wdt_lh);
  diag->retry_stop = ret;
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->active_after_retry_stop =
    (status.flags & WDFLAGS_ACTIVE) != 0;
  if (ret < 0 || diag->active_after_retry_stop != 0)
    {
      goto failed;
    }

  diag->state = 6;
  ret = bk7258_wdt_start(&priv->wdt_lh);
  diag->restart = ret;
  if (ret < 0)
    {
      goto failed;
    }

  bk7258_wdt_pm_prepare();
  ret = bk_wdt_stop();
  if (ret != BK_OK)
    {
      ret = -EIO;
      goto failed;
    }

  g_bk7258_wdt_fault_next = BK7258_WDT_FAULT_WDT_START;
  bk7258_wdt_pm_restore();
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->active_after_failed_restore =
    (status.flags & WDFLAGS_ACTIVE) != 0;
  if (diag->active_after_failed_restore != 0)
    {
      ret = -EIO;
      goto failed;
    }

  diag->state = 7;
  ret = bk7258_wdt_start(&priv->wdt_lh);
  diag->recovery_start = ret;
  (void)bk7258_wdt_getstatus(&priv->wdt_lh, &status);
  diag->final_active = (status.flags & WDFLAGS_ACTIVE) != 0;
  if (ret < 0 || diag->final_active == 0)
    {
      goto failed;
    }

  diag->result = OK;
  diag->state = 8;
  return OK;

failed:
  diag->result = ret < 0 ? ret : -EIO;
  diag->state |= 0x80000000u;
  return diag->result;
}
#endif

#endif /* CONFIG_BK7258_WDT */
