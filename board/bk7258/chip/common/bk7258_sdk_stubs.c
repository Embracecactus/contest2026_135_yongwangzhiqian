/****************************************************************************
 * board/bk7258/chip/common/bk7258_sdk_stubs.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stub implementations for SDK symbols not needed in NuttX bring-up.
 * These are referenced by prebuilt SDK libraries (libdriver.a, libbk_*.a)
 * but not used for basic WDT/flash/clock functionality.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <nuttx/arch.h>

#ifdef CONFIG_BK7258_AP_CORE
#  include <driver/mailbox_types.h>
#endif

/****************************************************************************
 * IPC Mailbox stubs
 ****************************************************************************/

/* __start_ipc_chan_reg / __stop_ipc_chan_reg are linker-provided boundary
 * symbols for the kept .ipc_chan_reg section (see ld_ap.script).  Earlier
 * revisions stubbed them as NULL globals, which masked the real boundaries
 * and left every BK_IPC_CHANNEL_REGISTER entry unbound; they must stay
 * linker-generated. */

#ifdef CONFIG_BK7258_AP_CORE
void __attribute__((weak)) crosscore_mb_rx_isr(mailbox_data_t *data)
{
  (void)data;
}

/* libbk_startup.a owns the CPU-stop notification registry, but NuttX owns
 * the AP core lifecycle; bk_ipc_init() only needs the hook to exist. */

typedef int (*stop_cpu1_notification)(void *);

void stop_cpu1_register_notification(stop_cpu1_notification notification,
                                     void *param)
{
  (void)notification;
  (void)param;
}

void stop_cpu1_unregister_notification(stop_cpu1_notification notification)
{
  (void)notification;
}

void shell_log_flush(void)
{
}

int shell_assert_out(bool bContinue, char *format, ...)
{
  (void)bContinue;
  (void)format;
  return 0;
}
#endif

/****************************************************************************
 * FreeRTOS heap stubs
 ****************************************************************************/

/* _heap_start / _heap_end are linker symbols for the FreeRTOS heap.
 * NuttX uses its own heap; provide dummy values. */

uint8_t _heap_start_dummy[4] __attribute__((aligned(16)));
uint8_t _heap_end_dummy[4] __attribute__((aligned(16)));
const void *_heap_start = &_heap_start_dummy;
const void *_heap_end = &_heap_end_dummy;

/****************************************************************************
 * Log stubs
 ****************************************************************************/

/* bk_printf_nonblock() lives in libbk_system printf.c, which is not linked.
 * pwr_clk.c references it on the CPU1 shutdown path once the CP IPC chain
 * is pulled in.  Drop the message. */

void bk_printf_nonblock(int level, char *tag, const char *fmt, ...)
{
  (void)level;
  (void)tag;
  (void)fmt;
}

/* shell_assert_out, shell_log_flush provided by libbk_cli.a */
/* SDK exception reporting expects the application-generated build string.
 * NuttX has no SDK project_elf_src.c, so publish a board-owned equivalent.
 */

volatile const uint8_t build_version[] = "openvela-bk7258";

/* save_net_info/get_net_info provided by libbk_system.a */

/****************************************************************************
 * SDK PHY stubs (used by libbk_phy.a)
 ****************************************************************************/

#if !defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_AP_CORE)
int phy_cca_busy_test(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 0;
}
#endif

int tx_evm_cmd_test(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 0;
}

int rx_sens_cmd_test(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  return 0;
}

/****************************************************************************
 * SDK Bluetooth stubs (used by libbluetooth_*.a)
 ****************************************************************************/

uint32_t gapc_get_conidx(uint8_t conidx)
{
  (void)conidx;
  return 0;
}

#if !defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_AP_CORE)
void rs_deinit(void)
{
}
#endif

/* sys_hal_aud_*, sys_hal_apll_en, sys_hal_psram_* provided by libbk7258.a */

/* sys_hal_* functions provided by libbk7258.a (sys_hal.c) */
/* mpu_soc_cfg provided by libcm33.a */

/* aon_pmu_hal_set_r0 provided by libbk7258.a (aon_pmu_hal.c) */
/* sys_hal_early_init provided by libbk7258.a (sys_hal.c) */

/****************************************************************************
 * SDK PM stubs (used by libbk_pm.a)
 ****************************************************************************/

#if !defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_AP_CORE)
void phy_wakeup_reinit(void)
{
  /* Stub: phy reinit not needed without wifi */
}
#endif

#ifndef CONFIG_BK7258_WIFI_VNET
void rwnxl_set_wifi_low_vol_flag(void)
{
  /* The immutable SDK low-voltage leaf was built with Wi-Fi enabled and
   * keeps this wake-debug hook behind its private CONFIG_WIFI_ENABLE.  A
   * NuttX image without the Wi-Fi service has no MAC state to update.
   */
}
#endif

/****************************************************************************
 * SDK reboot/timer stubs
 ****************************************************************************/

void bk_reboot_ex(uint32_t param)
{
  (void)param;
  up_systemreset();
}

void bk_reboot(void)
{
  bk_reboot_ex(1); /* RESET_SOURCE_REBOOT in the SDK ABI */
}

void delay(int32_t count)
{
  volatile int32_t i;
  volatile int32_t j;

  /* Preserve the v3.1.1.9 bk_system/delay.c ABI exactly.  Despite the
   * tempting name, delay() is not a millisecond sleep: immutable timer,
   * audio, I2S, touch and USB objects use it as a short hardware-settle busy
   * loop.  A scheduler sleep is both too long and unsafe for early driver
   * initialization.
   */

  for (i = 0; i < count; i++)
    {
      for (j = 0; j < 100; j++)
        {
        }
    }
}

void bk_delay_us(unsigned int us)
{
  /* The SDK Bluetooth/PHY libraries use this as a real busy-wait primitive.
   * Keep the wrapper semantics equivalent when those prebuilt libraries run
   * on NuttX instead of silently dropping timing-sensitive delays.
   */

  up_udelay(us);
}

/****************************************************************************
 * CMSIS startup stubs (used by libcmsis.a if linked)
 ****************************************************************************/

/* Stack/linker symbols for CMSIS startup. NuttX provides its own stack. */

uint8_t __StackLimit_dummy[4] __attribute__((aligned(16)));
uint8_t __StackTop_dummy[4] __attribute__((aligned(16)));
const void *__StackLimit = &__StackLimit_dummy;
const void *__StackTop = &__StackTop_dummy;
const void *__copy_table_start__ = NULL;
const void *__copy_table_end__ = NULL;
const void *__zero_table_start__ = NULL;
const void *__zero_table_end__ = NULL;

/****************************************************************************
 * Coredump stubs (used by libcoredump.a)
 ****************************************************************************/

const void *_sstack = NULL;

/****************************************************************************
 * SDK reset_reason stubs (used by libcommon.a wdt_hal.c)
 ****************************************************************************/

void set_reboot_tag(uint32_t tag)
{
  (void)tag;
}

uint32_t get_reboot_tag(void)
{
  return 0;
}

#ifdef CONFIG_BK7258_WIFI_VNET
/****************************************************************************
 * SDK Wi-Fi optional-service compatibility
 ****************************************************************************/

uint8_t iperf_get_state(void)
{
  /* N16 does not run the SDK iperf service on CP. */

  return 0;
}

/* Newlib-compatible ASCII character flags used by the immutable SDK lwIP
 * and WPA archives.  NuttX normally exposes this table only with its
 * toolchain C++ runtime enabled; N16 remains a C-only image.
 */

#define BK7258_CTYPE_U  01
#define BK7258_CTYPE_L  02
#define BK7258_CTYPE_N  04
#define BK7258_CTYPE_S  010
#define BK7258_CTYPE_P  020
#define BK7258_CTYPE_C  040
#define BK7258_CTYPE_X  0100
#define BK7258_CTYPE_B  0200

const char _ctype_[] =
{
  0,
  040, 040, 040, 040, 040, 040, 040, 040,
  040, 050, 050, 050, 050, 050, 040, 040,
  040, 040, 040, 040, 040, 040, 040, 040,
  040, 040, 040, 040, 040, 040, 040, 040,
  0210, 020, 020, 020, 020, 020, 020, 020,
  020, 020, 020, 020, 020, 020, 020, 020,
  04, 04, 04, 04, 04, 04, 04, 04,
  04, 04, 020, 020, 020, 020, 020, 020,
  020, 0101, 0101, 0101, 0101, 0101, 0101, 01,
  01, 01, 01, 01, 01, 01, 01, 01,
  01, 01, 01, 01, 01, 01, 01, 01,
  01, 01, 01, 020, 020, 020, 020, 020,
  020, 0102, 0102, 0102, 0102, 0102, 0102, 02,
  02, 02, 02, 02, 02, 02, 02, 02,
  02, 02, 02, 02, 02, 02, 02, 02,
  02, 02, 02, 020, 020, 020, 020, 040,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif
