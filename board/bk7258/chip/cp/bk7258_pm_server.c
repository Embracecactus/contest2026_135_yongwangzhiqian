/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/
 * bk7258_pm_server.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP side of the BK7258 peripheral clock service.  CP is the only owner of
 * sys_ctrl clock-power bits; AP requests are allow-listed and reference
 * counted per RPTUN generation.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PM_CLOCK

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/rpmsg/rpmsg.h>

#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_pm_ipc.h"

/* The immutable v3.1.1.9 CP libdriver archive exports this function.  Its
 * enum parameters use the ordinary C int ABI.  Keep the verified numeric
 * IDs private here instead of exposing raw SDK IDs across RPMsg.
 */

extern void sys_drv_dev_clk_pwr_up(int dev, int power_up);
extern void sys_drv_module_power_ctrl(int module, int power_state);
extern int32_t sys_drv_module_power_state_get(int module);
extern void smem_reset_lastblock(void);
extern void sys_hal_set_auxs_cis_clk_sel(uint32_t value);
extern void sys_hal_set_auxs_cis_clk_div(uint32_t value);
extern void sys_hal_set_cis_auxs_clk_en(uint32_t value);

#define BK7258_SDK_CLOCK_POWER_DOWN 0
#define BK7258_SDK_CLOCK_POWER_UP   1
#define BK7258_SDK_CLOCK_I2C1       0
#define BK7258_SDK_CLOCK_SPI1       1
#define BK7258_SDK_CLOCK_UART0      2
#define BK7258_SDK_CLOCK_PWM1       3
#define BK7258_SDK_CLOCK_TIMER1     4
#define BK7258_SDK_CLOCK_SARADC     5
#define BK7258_SDK_CLOCK_IRDA       6
#define BK7258_SDK_CLOCK_EFUSE      7
#define BK7258_SDK_CLOCK_I2C2       8
#define BK7258_SDK_CLOCK_SPI2       9
#define BK7258_SDK_CLOCK_UART1      10
#define BK7258_SDK_CLOCK_UART2      11
#define BK7258_SDK_CLOCK_PWM2       12
#define BK7258_SDK_CLOCK_TIMER2     13
#define BK7258_SDK_CLOCK_TIMER3     14
#define BK7258_SDK_CLOCK_OTP        15
#define BK7258_SDK_CLOCK_I2S1       16
#define BK7258_SDK_CLOCK_USB        17
#define BK7258_SDK_CLOCK_CAN        18
#define BK7258_SDK_CLOCK_PSRAM      19
#define BK7258_SDK_CLOCK_QSPI0      20
#define BK7258_SDK_CLOCK_QSPI1      21
#define BK7258_SDK_CLOCK_SDIO       22
#define BK7258_SDK_CLOCK_AUXS       23
#define BK7258_SDK_CLOCK_BTDM       24
#define BK7258_SDK_CLOCK_XVR        25
#define BK7258_SDK_CLOCK_MAC        26
#define BK7258_SDK_CLOCK_PHY        27
#define BK7258_SDK_CLOCK_JPEG       28
#define BK7258_SDK_CLOCK_DISPLAY    29
#define BK7258_SDK_CLOCK_AUDIO      30
#define BK7258_SDK_CLOCK_WATCHDOG   31
#define BK7258_SDK_CLOCK_H264       32
#define BK7258_SDK_CLOCK_I2S2       33
#define BK7258_SDK_CLOCK_I2S3       34
#define BK7258_SDK_CLOCK_YUV        35
#define BK7258_SDK_CLOCK_SLCD       36
#define BK7258_SDK_CLOCK_LIN        37

#define BK7258_SDK_POWER_MEM3       2
#define BK7258_SDK_POWER_VIDP       7
#define BK7258_SDK_POWER_ON         0
#define BK7258_SDK_POWER_OFF        1

struct bk7258_pm_server_s
{
  struct rpmsg_endpoint ept;
  volatile bool initialized;
  volatile bool endpoint_created;
  uint32_t generation;
  uint16_t refs[BK7258_PM_CLOCK_COUNT];
};

static struct bk7258_pm_server_s g_bk7258_pm_server;

static bool bk7258_pm_is_video_clock(enum bk7258_pm_clock_e clock)
{
  return clock == BK7258_PM_CLOCK_JPEG ||
         clock == BK7258_PM_CLOCK_DISPLAY ||
         clock == BK7258_PM_CLOCK_H264 ||
         clock == BK7258_PM_CLOCK_YUV ||
         clock == BK7258_PM_CLOCK_DMA2D ||
         clock == BK7258_PM_CLOCK_JPEG_DECODER;
}

static bool bk7258_pm_video_active(struct bk7258_pm_server_s *priv)
{
  return priv->refs[BK7258_PM_CLOCK_JPEG] != 0 ||
         priv->refs[BK7258_PM_CLOCK_DISPLAY] != 0 ||
         priv->refs[BK7258_PM_CLOCK_H264] != 0 ||
         priv->refs[BK7258_PM_CLOCK_YUV] != 0 ||
         priv->refs[BK7258_PM_CLOCK_DMA2D] != 0 ||
         priv->refs[BK7258_PM_CLOCK_JPEG_DECODER] != 0;
}

static void bk7258_pm_set_video_power(bool enable)
{
  if (enable &&
      sys_drv_module_power_state_get(BK7258_SDK_POWER_MEM3) ==
      BK7258_SDK_POWER_OFF)
    {
      /* v3.1.1.9 restores MEM3 before a powered-down VIDP domain.  DMA2D
       * source reads and JPEG decode both depend on this memory domain. */

      sys_drv_module_power_ctrl(BK7258_SDK_POWER_MEM3,
                                BK7258_SDK_POWER_ON);
      smem_reset_lastblock();
    }

  sys_drv_module_power_ctrl(BK7258_SDK_POWER_VIDP,
                            enable ? BK7258_SDK_POWER_ON :
                                     BK7258_SDK_POWER_OFF);
}

static void bk7258_pm_set_camera_mclk_24m(bool enable)
{
  irqstate_t flags = enter_critical_section();

  if (enable)
    {
      sys_hal_set_auxs_cis_clk_sel(3);
      sys_hal_set_auxs_cis_clk_div(19);
    }

  sys_hal_set_cis_auxs_clk_en(enable ? 1 : 0);
  __asm volatile ("dmb sy" ::: "memory");
  leave_critical_section(flags);
}

static void bk7258_pm_set_clock(enum bk7258_pm_clock_e clock, bool enable)
{
  int state = enable ? BK7258_SDK_CLOCK_POWER_UP :
                       BK7258_SDK_CLOCK_POWER_DOWN;

  switch (clock)
    {
      case BK7258_PM_CLOCK_SDIO:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_SDIO, state);
        break;
      case BK7258_PM_CLOCK_QSPI0:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_QSPI0, state);
        break;
      case BK7258_PM_CLOCK_QSPI1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_QSPI1, state);
        break;
      case BK7258_PM_CLOCK_CAN:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_CAN, state);
        break;
      case BK7258_PM_CLOCK_USB:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_USB, state);
        break;
      case BK7258_PM_CLOCK_ETHERNET:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_MAC, state);
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_PHY, state);
        break;
      case BK7258_PM_CLOCK_JPEG:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_JPEG, state);
        break;
      case BK7258_PM_CLOCK_DISPLAY:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_DISPLAY, state);
        break;
      case BK7258_PM_CLOCK_AUDIO:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_AUDIO, state);
        break;
      case BK7258_PM_CLOCK_I2C1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_I2C1, state);
        break;
      case BK7258_PM_CLOCK_SPI1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_SPI1, state);
        break;
      case BK7258_PM_CLOCK_UART0:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_UART0, state);
        break;
      case BK7258_PM_CLOCK_PWM1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_PWM1, state);
        break;
      case BK7258_PM_CLOCK_TIMER1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_TIMER1, state);
        break;
      case BK7258_PM_CLOCK_SARADC:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_SARADC, state);
        break;
      case BK7258_PM_CLOCK_IRDA:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_IRDA, state);
        break;
      case BK7258_PM_CLOCK_EFUSE:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_EFUSE, state);
        break;
      case BK7258_PM_CLOCK_I2C2:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_I2C2, state);
        break;
      case BK7258_PM_CLOCK_SPI2:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_SPI2, state);
        break;
      case BK7258_PM_CLOCK_UART1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_UART1, state);
        break;
      case BK7258_PM_CLOCK_UART2:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_UART2, state);
        break;
      case BK7258_PM_CLOCK_PWM2:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_PWM2, state);
        break;
      case BK7258_PM_CLOCK_TIMER2:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_TIMER2, state);
        break;
      case BK7258_PM_CLOCK_TIMER3:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_TIMER3, state);
        break;
      case BK7258_PM_CLOCK_OTP:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_OTP, state);
        break;
      case BK7258_PM_CLOCK_I2S1:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_I2S1, state);
        break;
      case BK7258_PM_CLOCK_PSRAM:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_PSRAM, state);
        break;
      case BK7258_PM_CLOCK_AUXS:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_AUXS, state);
        break;
      case BK7258_PM_CLOCK_BTDM:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_BTDM, state);
        break;
      case BK7258_PM_CLOCK_XVR:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_XVR, state);
        break;
      case BK7258_PM_CLOCK_MAC:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_MAC, state);
        break;
      case BK7258_PM_CLOCK_PHY:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_PHY, state);
        break;
      case BK7258_PM_CLOCK_WATCHDOG:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_WATCHDOG, state);
        break;
      case BK7258_PM_CLOCK_H264:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_H264, state);
        break;
      case BK7258_PM_CLOCK_I2S2:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_I2S2, state);
        break;
      case BK7258_PM_CLOCK_I2S3:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_I2S3, state);
        break;
      case BK7258_PM_CLOCK_YUV:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_YUV, state);
        break;
      case BK7258_PM_CLOCK_SEGMENT_LCD:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_SLCD, state);
        break;
      case BK7258_PM_CLOCK_LIN:
        sys_drv_dev_clk_pwr_up(BK7258_SDK_CLOCK_LIN, state);
        break;
      case BK7258_PM_CLOCK_CAMERA_MCLK_24M:
        /* v3.1.1.9 dvp_camera_mclk_enable(MCLK_24M): AUXS_CIS uses
         * source 3 and divider 19.  sys_ctrl is CP-owned in this port,
         * so reproduce those official SDK operations here rather than
         * allowing the immutable AP helper to write shared registers. */

        bk7258_pm_set_camera_mclk_24m(enable);
        break;
      case BK7258_PM_CLOCK_DMA2D:
        /* The immutable DMA2D driver programs its controller clocks after
         * requesting PM_POWER_SUB_MODULE_NAME_VIDP_DMA2D.  The shared VIDP
         * domain is the CP-owned resource represented by this logical vote;
         * bk7258_pm_is_video_clock() handles its first/last power edge. */

        break;
      case BK7258_PM_CLOCK_JPEG_DECODER:
        /* The immutable JPEG decoder driver configures its controller clock
         * after requesting PM_POWER_SUB_MODULE_NAME_VIDP_JPEG_DE.  The CP
         * service owns only the shared VIDP/MEM3 power edge here. */

        break;
      default:
        break;
    }
}

static void bk7258_pm_release_generation(
  struct bk7258_pm_server_s *priv)
{
  bool video_active = bk7258_pm_video_active(priv);
  unsigned int i;

  for (i = 0; i < BK7258_PM_CLOCK_COUNT; i++)
    {
      if (priv->refs[i] != 0)
        {
          bk7258_pm_set_clock((enum bk7258_pm_clock_e)i, false);
          priv->refs[i] = 0;
        }
    }

  if (video_active)
    {
      bk7258_pm_set_video_power(false);
    }

  priv->generation = 0;
}

static int bk7258_pm_server_cb(FAR struct rpmsg_endpoint *ept,
                               FAR void *data, size_t len,
                               uint32_t src, FAR void *priv_)
{
  struct bk7258_pm_server_s *priv = priv_;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_pm_wire_s reply;
  FAR const struct bk7258_pm_wire_s *request = data;
  enum bk7258_pm_clock_e clock;
  int status = OK;

  (void)ept;
  (void)src;

  if (len != sizeof(*request) || request->magic != BK7258_PM_MAGIC ||
      request->version != BK7258_PM_VERSION || request->generation == 0 ||
      request->generation != control->generation ||
      request->clock >= BK7258_PM_CLOCK_COUNT)
    {
      return -EINVAL;
    }

  if (priv->generation != request->generation)
    {
      bk7258_pm_release_generation(priv);
      priv->generation = request->generation;
    }

  clock = (enum bk7258_pm_clock_e)request->clock;
  if (request->command == BK7258_PM_COMMAND_CLOCK_GET)
    {
      if (priv->refs[clock] == UINT16_MAX)
        {
          status = -EOVERFLOW;
        }
      else
        {
          if (priv->refs[clock] == 0)
            {
              if (bk7258_pm_is_video_clock(clock) &&
                  !bk7258_pm_video_active(priv))
                {
                  bk7258_pm_set_video_power(true);
                }

              bk7258_pm_set_clock(clock, true);
            }

          priv->refs[clock]++;
        }
    }
  else if (request->command == BK7258_PM_COMMAND_CLOCK_PUT)
    {
      if (priv->refs[clock] == 0)
        {
          status = -EALREADY;
        }
      else
        {
          priv->refs[clock]--;
          if (priv->refs[clock] == 0)
            {
              bk7258_pm_set_clock(clock, false);
              if (bk7258_pm_is_video_clock(clock) &&
                  !bk7258_pm_video_active(priv))
                {
                  bk7258_pm_set_video_power(false);
                }
            }
        }
    }
  else
    {
      status = -ENOTSUP;
    }

  memcpy(&reply, request, sizeof(reply));
  reply.command = BK7258_PM_COMMAND_RESPONSE;
  reply.status = status;
  reply.refcount = priv->refs[clock];

  /* The callback owns the RPTUN RX worker.  Never wait for a TX buffer here;
   * a busy transport is reported to AP as its bounded request timeout.
   */

  return rpmsg_trysend(&priv->ept, &reply, sizeof(reply));
}

static void bk7258_pm_device_created(FAR struct rpmsg_device *rdev,
                                     FAR void *priv_)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }
}

static bool bk7258_pm_ns_match(FAR struct rpmsg_device *rdev,
                               FAR void *priv_, FAR const char *name,
                               uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL && strcmp(cpuname, "ap") == 0 &&
         strcmp(name, BK7258_PM_EPT_NAME) == 0;
}

static void bk7258_pm_ns_bind(FAR struct rpmsg_device *rdev,
                              FAR void *priv_, FAR const char *name,
                              uint32_t dest)
{
  struct bk7258_pm_server_s *priv = priv_;
  int ret;

  priv->ept.priv = priv;
  ret = rpmsg_create_ept(&priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
                         bk7258_pm_server_cb, NULL);
  if (ret >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
}

static void bk7258_pm_device_destroy(FAR struct rpmsg_device *rdev,
                                     FAR void *priv_)
{
  struct bk7258_pm_server_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  bk7258_pm_release_generation(priv);
  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

int bk7258_pm_initialize(void)
{
  struct bk7258_pm_server_s *priv = &g_bk7258_pm_server;
  bool expected = false;
  int ret;

  if (!__atomic_compare_exchange_n(&priv->initialized, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return OK;
    }

  ret = rpmsg_register_callback(priv, bk7258_pm_device_created,
                                bk7258_pm_device_destroy,
                                bk7258_pm_ns_match, bk7258_pm_ns_bind);
  if (ret < 0)
    {
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
    }

  return ret;
}

#endif /* CONFIG_BK7258_PM_CLOCK */
