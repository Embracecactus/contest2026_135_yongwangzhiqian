/****************************************************************************
 * arch/arm/include/bk7258/bk7258_pm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP-owned peripheral clock service.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_PM_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_PM_H

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* These are project-owned logical clock IDs, not raw SDK register indices.
 * CP maps each allowed ID to the v3.1.1.9 sys_ctrl implementation.
 */

enum bk7258_pm_clock_e
{
  BK7258_PM_CLOCK_SDIO = 0,
  BK7258_PM_CLOCK_QSPI0,
  BK7258_PM_CLOCK_QSPI1,
  BK7258_PM_CLOCK_CAN,
  BK7258_PM_CLOCK_USB,
  BK7258_PM_CLOCK_ETHERNET,
  BK7258_PM_CLOCK_JPEG,
  BK7258_PM_CLOCK_DISPLAY,
  BK7258_PM_CLOCK_AUDIO,
  BK7258_PM_CLOCK_I2C1,
  BK7258_PM_CLOCK_SPI1,
  BK7258_PM_CLOCK_UART0,
  BK7258_PM_CLOCK_PWM1,
  BK7258_PM_CLOCK_TIMER1,
  BK7258_PM_CLOCK_SARADC,
  BK7258_PM_CLOCK_IRDA,
  BK7258_PM_CLOCK_EFUSE,
  BK7258_PM_CLOCK_I2C2,
  BK7258_PM_CLOCK_SPI2,
  BK7258_PM_CLOCK_UART1,
  BK7258_PM_CLOCK_UART2,
  BK7258_PM_CLOCK_PWM2,
  BK7258_PM_CLOCK_TIMER2,
  BK7258_PM_CLOCK_TIMER3,
  BK7258_PM_CLOCK_OTP,
  BK7258_PM_CLOCK_I2S1,
  BK7258_PM_CLOCK_PSRAM,
  BK7258_PM_CLOCK_AUXS,
  BK7258_PM_CLOCK_BTDM,
  BK7258_PM_CLOCK_XVR,
  BK7258_PM_CLOCK_MAC,
  BK7258_PM_CLOCK_PHY,
  BK7258_PM_CLOCK_WATCHDOG,
  BK7258_PM_CLOCK_H264,
  BK7258_PM_CLOCK_I2S2,
  BK7258_PM_CLOCK_I2S3,
  BK7258_PM_CLOCK_YUV,
  BK7258_PM_CLOCK_SEGMENT_LCD,
  BK7258_PM_CLOCK_LIN,
  BK7258_PM_CLOCK_CAMERA_MCLK_24M,
  BK7258_PM_CLOCK_DMA2D,
  BK7258_PM_CLOCK_JPEG_DECODER,
  BK7258_PM_CLOCK_SCALE0,
  BK7258_PM_CLOCK_SCALE1,
  BK7258_PM_CLOCK_ROTATOR,
  BK7258_PM_CLOCK_COUNT
};

/* Stable board-owned frequency clients.  These deliberately do not expose
 * the role-dependent v3.1.1.9 pm_dev_id_e values over RPMsg. */

enum bk7258_pm_freq_client_e
{
  BK7258_PM_FREQ_CLIENT_DEFAULT = 0,
  BK7258_PM_FREQ_CLIENT_VIDEO_ENCODER,
  BK7258_PM_FREQ_CLIENT_VIDEO_DECODER,
  BK7258_PM_FREQ_CLIENT_DISPLAY,
  BK7258_PM_FREQ_CLIENT_AUDIO,
  BK7258_PM_FREQ_CLIENT_WIFI,
  BK7258_PM_FREQ_CLIENT_BLUETOOTH,
  BK7258_PM_FREQ_CLIENT_USB,
  BK7258_PM_FREQ_CLIENT_PWM,
  BK7258_PM_FREQ_CLIENT_SECURE,
  BK7258_PM_FREQ_CLIENT_CPU1,
  BK7258_PM_FREQ_CLIENT_APP,
  BK7258_PM_FREQ_CLIENT_COUNT
};

/* Values match v3.1.1.9 pm_cpu_freq_e. */

enum bk7258_pm_cpu_freq_e
{
  BK7258_PM_CPU_FREQ_26M = 0,
  BK7258_PM_CPU_FREQ_60M,
  BK7258_PM_CPU_FREQ_80M,
  BK7258_PM_CPU_FREQ_120M,
  BK7258_PM_CPU_FREQ_240M,
  BK7258_PM_CPU_FREQ_320M,
  BK7258_PM_CPU_FREQ_480M,
  BK7258_PM_CPU_FREQ_DEFAULT
};

struct bk7258_pm_frequency_status_s
{
  uint32_t current;
  uint32_t peak;
  uint32_t transitions;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_pm_initialize(void);
int bk7258_pm_frequency_vote(enum bk7258_pm_freq_client_e client,
                             enum bk7258_pm_cpu_freq_e frequency);
int bk7258_pm_frequency_get_status(
  struct bk7258_pm_frequency_status_s *status);

#ifndef CONFIG_BK7258_AP_CORE
bool bk7258_pm_frequency_votes_idle(void);
bool bk7258_pm_server_resources_idle(void);
#endif

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_pm_clock_get(enum bk7258_pm_clock_e clock);
int bk7258_pm_clock_put(enum bk7258_pm_clock_e clock);
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_PM_H */
