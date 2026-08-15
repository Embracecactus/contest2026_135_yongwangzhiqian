/****************************************************************************
 * board/bk7258/chip/include/bk7258_sdk_abi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private ABI boundary for immutable Beken BK7258 SDK archives.
 *
 * The declarations in this file are intentionally limited to v3.1.1.9
 * symbols or layouts which cannot be obtained from the exported SDK header
 * bundle.  Normal public SDK APIs must continue to use their SDK headers.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_SDK_ABI_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_SDK_ABI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <common/bk_err.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* This boundary is pinned to the sole active runtime SDK.  Bundle selection
 * and archive checksum verification remain owned by bk_idk/sdk-bundles.* and
 * build_dual_image.sh; this value identifies the C ABI described below.
 */

#define BK7258_SDK_ABI_VERSION_V3119       0x03010109u
#define BK7258_SDK_ABI_VERSION             BK7258_SDK_ABI_VERSION_V3119

/****************************************************************************
 * AP-local cross-core mailbox ABI
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_IPI
#  include <driver/mailbox_types.h>

extern bk_err_t bk_mailbox_cc_init(void);
extern bk_err_t bk_mailbox_cc_init_on_current_core(int id);
extern bk_err_t bk_mailbox_master_send(mailbox_data_t *data, uint8_t src,
                                       uint8_t dst);

/* v3.1.1.9 bk_mailbox_master_send() transmits only param2.  Its receive-side
 * crosscore_smp_cmd_handler() reconstructs param0 from the physical mailbox
 * source endpoint and param1 from the AP-local SDK core index before invoking
 * crosscore_mb_rx_isr().  Do not treat param0/param1 as sender payload, and
 * do not confuse the logical 0/1 core index with physical endpoints 1/2.
 */

static inline bool
bk7258_sdk_mailbox_rx_is(const mailbox_data_t *data,
                         mailbox_endpoint_t src,
                         uint32_t local_cpu)
{
  return data != NULL && data->param0 == (uint32_t)src &&
         data->param1 == local_cpu;
}

_Static_assert(sizeof(mailbox_data_t) == 16u,
               "v3.1.1.9 mailbox_data_t ABI changed");
_Static_assert(MAILBOX_CPU1 == 1 && MAILBOX_CPU2 == 2,
               "v3.1.1.9 AP mailbox endpoint ABI changed");
#endif

/****************************************************************************
 * AP SDIO host transaction ABI
 ****************************************************************************/

#if defined(CONFIG_BK7258_AP_CORE) && defined(CONFIG_BK7258_SDIO) && \
    defined(CONFIG_SDIO_V2P0)
extern void bk_sdio_clk_gate_config(uint32_t enable);
extern void bk_sdio_tx_fifo_clk_gate_config(uint32_t enable);
extern void bk_sdio_host_reset_sd_state(void);
extern void bk_sdio_host_discard_previous_receive_data_sema(void);
#endif

/****************************************************************************
 * AP CAN ABI
 ****************************************************************************/

#ifdef CONFIG_BK7258_CAN
#  include <driver/hal/hal_can_types.h>

extern bk_err_t bk_can_driver_init(void);
extern bk_err_t bk_can_driver_deinit(void);
extern bk_err_t bk_can_receive(uint8_t *data, uint32_t expect_size,
                               uint32_t *recv_size, uint32_t timeout);
extern bk_err_t bk_can_send_ptb(can_frame_s *frame);
extern void bk_can_register_isr_callback(can_callback_des_t *rx_cb,
                                         can_callback_des_t *tx_cb);
extern void bk_can_register_err_callback(can_callback_des_t *err_cb);
extern bk_err_t can_driver_bit_rate_config(can_bit_rate_e s_speed,
                                            can_bit_rate_e f_speed);
extern bk_err_t bk_can_abort_ptb(void);
extern bk_err_t bk_can_abort_all(void);

extern void can_hal_set_lbmi(uint32_t value);
extern uint32_t can_hal_get_lbmi(void);
extern bk_err_t can_hal_ctrl(uint32_t command, void *parameter);
#endif

/****************************************************************************
 * AP AON RTC ABI
 ****************************************************************************/

#ifdef CONFIG_BK7258_RTC
extern uint64_t bk_aon_rtc_get_us(void);
#endif

/****************************************************************************
 * CP Bluetooth/Wi-Fi shared-PHY and calibration ABI
 ****************************************************************************/

#if !defined(CONFIG_BK7258_AP_CORE) && \
    (defined(CONFIG_BK7258_BT_IPC) || defined(CONFIG_BK7258_WIFI_VNET))

#define BK7258_SDK_MAC_RECORD_AREA_OFFSET    0x00000e00u
#define BK7258_SDK_MAC_RECORD_AREA_SIZE      512u
#define BK7258_SDK_MAC_RECORD_COUNT          51u
#define BK7258_SDK_MAC_RECORD_MAGIC          0x4d41u
#define BK7258_SDK_MAC_ADDRESS_SIZE          6u
#define BK7258_SDK_MAC_OUI0                  0xc8u
#define BK7258_SDK_MAC_OUI1                  0x47u
#define BK7258_SDK_MAC_OUI2                  0x8cu

#define BK7258_WIFI_CAL_WIFI_PLL_SLOT        5u
#define BK7258_WIFI_DELAY_US_SLOT            66u
#define BK7258_WIFI_SET_OFDM_PWD_SLOT        73u
#define BK7258_WIFI_GET_OFDM_PWD_SLOT        74u
#define BK7258_WIFI_DISABLE_INT_SLOT          140u
#define BK7258_WIFI_ENTER_LOW_ANALOG_SLOT    205u

struct bk7258_sdk_partition_s
{
  uint32_t    owner;
  const char *description;
  uint32_t    start;
  uint32_t    length;
  uint32_t    options;
};

struct bk7258_bt_mac_record_s
{
  uint16_t magic;
  uint8_t  data_crc;
  uint8_t  header_crc;
  uint8_t  mac[BK7258_SDK_MAC_ADDRESS_SIZE];
};

struct bk7258_bt_wifi_phy_funcs_s
{
  uintptr_t reserved0[BK7258_WIFI_CAL_WIFI_PLL_SLOT];
  int (*cal_set_wifi_pll)(void);
  uintptr_t reserved1[BK7258_WIFI_DELAY_US_SLOT -
                      BK7258_WIFI_CAL_WIFI_PLL_SLOT - 1u];
  void (*delay_us)(uint32_t us);
  uintptr_t reserved2[BK7258_WIFI_SET_OFDM_PWD_SLOT -
                      BK7258_WIFI_DELAY_US_SLOT - 1u];
  void (*set_ofdm_pwd)(uint32_t value);
  uint32_t (*get_ofdm_pwd)(void);
  uintptr_t reserved3[BK7258_WIFI_DISABLE_INT_SLOT -
                      BK7258_WIFI_GET_OFDM_PWD_SLOT - 1u];
  uint32_t (*disable_int)(void);
  void (*enable_int)(uint32_t int_level);
  uintptr_t reserved4[BK7258_WIFI_ENTER_LOW_ANALOG_SLOT -
                      BK7258_WIFI_DISABLE_INT_SLOT - 2u];
  void (*enter_low_analog)(void);
  void (*exit_low_analog)(void);
};

#ifdef CONFIG_BK7258_WIFI_VNET
extern uint8_t g_wifi_os_funcs;
extern uint8_t g_wifi_os_variable;
#endif

#ifdef CONFIG_BK7258_BT_IPC
extern int32_t bt_ipc_init(void);
extern int __real_bk_bluetooth_init(void);
extern int __real_bk_bluetooth_deinit(void);
#endif

extern void bk_phy_adapter_init(void);
extern void bk_rf_adapter_init(void);
extern int bk_cal_if_init(void);
extern bk_err_t bk_adc_driver_init(void);
extern void *g_wifi_funcs;
extern int rwnx_cal_set_rfconfig_WIFIPLL(void);
extern void bk_delay_us(uint32_t us);
extern uint32_t rtos_disable_int(void);
extern void rtos_enable_int(uint32_t int_level);
extern void sys_hal_enter_low_analog(void);
extern void sys_hal_exit_low_analog(void);

extern struct bk7258_sdk_partition_s *
  bk_flash_partition_get_info(uint32_t partition);
extern bk_err_t bk_flash_driver_init(void);
extern bk_err_t bk_flash_partition_read(uint32_t partition,
                                        uint8_t *buffer, uint32_t offset,
                                        uint32_t length);
extern bk_err_t bk_flash_partition_write(uint32_t partition,
                                         const uint8_t *buffer,
                                         uint32_t offset, uint32_t length);
extern bk_err_t bk_spec_flash_write_bytes(uint32_t partition,
                                          const uint8_t *buffer,
                                          uint32_t length, uint32_t offset);
extern bk_err_t bk_trng_driver_init(void);
extern int bk_rand(void);

_Static_assert(sizeof(uintptr_t) == 4u,
               "v3.1.1.9 private callback ABI requires 32-bit pointers");
_Static_assert(sizeof(struct bk7258_sdk_partition_s) == 20u,
               "v3.1.1.9 partition ABI changed");
_Static_assert(sizeof(struct bk7258_bt_mac_record_s) == 10u,
               "Beken base-MAC record ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s,
                        cal_set_wifi_pll) == 0x014u,
               "SDK wifi_os_funcs_t Wi-Fi PLL ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s, delay_us) ==
               0x108u,
               "SDK wifi_os_funcs_t delay ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s, set_ofdm_pwd) ==
               0x124u,
               "SDK wifi_os_funcs_t OFDM power ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s, get_ofdm_pwd) ==
               0x128u,
               "SDK wifi_os_funcs_t OFDM power ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s, disable_int) ==
               0x230u,
               "SDK wifi_os_funcs_t interrupt ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s,
                        enter_low_analog) == 0x334u,
               "SDK wifi_os_funcs_t analog-power ABI changed");
_Static_assert(offsetof(struct bk7258_bt_wifi_phy_funcs_s, exit_low_analog) ==
               0x338u,
               "SDK wifi_os_funcs_t analog-power ABI changed");
#endif

/****************************************************************************
 * CP on-die temperature ABI
 ****************************************************************************/

#if defined(CONFIG_BK7258_TEMPERATURE) && \
    !defined(CONFIG_BK7258_AP_CORE)
/* v3.1.1.9 exports this one-shot API from libtemp_detect.a but omits its
 * public header from the immutable CP bundle.  It returns an averaged ADC raw
 * code, not degrees Celsius.
 */

extern int temp_detect_get_temperature(uint32_t *temperature);
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_SDK_ABI_H */
