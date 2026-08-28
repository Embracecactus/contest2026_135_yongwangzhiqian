/****************************************************************************
 * chips/bk7258/common/bk7258_sdk_abi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private ABI boundary for immutable Beken BK7258 SDK archives.
 *
 * The declarations in this file are intentionally limited to v3.1.1.9
 * symbols or layouts which cannot be obtained from the exported SDK header
 * bundle.  Normal public SDK APIs must continue to use their SDK headers.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_COMMON_BK7258_SDK_ABI_H
#define __CHIPS_BK7258_COMMON_BK7258_SDK_ABI_H

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
 * and bundle tree verification remain owned by bk7258.py sdk and
 * bk7258.py build; this value identifies the C ABI described below.
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
 * AP LIN ABI
 *
 * The exported driver/lin_types.h pulls SDK-private gpio_driver.h, which is
 * not part of the immutable bundle.  Keep the same layout/ABI here and
 * assert it against the v3.1.1.9 bundle with static assertions.
 ****************************************************************************/

#ifdef CONFIG_BK7258_LIN

typedef enum
{
  LIN_SLAVE = 0,
  LIN_MASTER = 1
} lin_dev_t;

typedef enum
{
  LIN_CHAN_0 = 0,
  LIN_CHAN_1,
  LIN_CHAN_2,
  LIN_CHAN_MAX
} lin_channel_t;

typedef enum
{
  LIN_DATA_LEN_MIN = 0,
  LIN_DATA_LEN_1BYTES,
  LIN_DATA_LEN_2BYTES,
  LIN_DATA_LEN_3BYTES,
  LIN_DATA_LEN_4BYTES,
  LIN_DATA_LEN_5BYTES,
  LIN_DATA_LEN_6BYTES,
  LIN_DATA_LEN_7BYTES,
  LIN_DATA_LEN_8BYTES,
  LIN_DATA_LEN_MAX
} lin_data_len_t;

typedef enum
{
  LIN_CLASSIC = 0,
  LIN_ENHANCED
} lin_checksum_t;

typedef enum
{
  LIN_BUS_INACTIVITY_4S,
  LIN_BUS_INACTIVITY_6S,
  LIN_BUS_INACTIVITY_8S,
  LIN_BUS_INACTIVITY_10S
} lin_bus_inactivity_time_t;

typedef enum
{
  LIN_WUP_REPEAT_180MS,
  LIN_WUP_REPEAT_200MS,
  LIN_WUP_REPEAT_220MS,
  LIN_WUP_REPEAT_240MS
} lin_wup_repeat_time_t;

typedef enum
{
  LIN_IDENT_MIN = -1,
  LIN_IDENT0,
  LIN_IDENT1,
  LIN_IDENT2,
  LIN_IDENT3,
  LIN_IDENT4,
  LIN_IDENT5,
  LIN_IDENT6,
  LIN_IDENT7,
  LIN_IDENT8,
  LIN_IDENT9,
  LIN_IDENT10,
  LIN_IDENT11,
  LIN_IDENT12,
  LIN_IDENT13,
  LIN_IDENT14,
  LIN_IDENT15,
  LIN_IDENT_MAX
} lin_id_t;

typedef struct
{
  uint32_t id;
  uint32_t dev;
} lin_gpio_map_t;

typedef struct
{
  lin_gpio_map_t tx_gpio;
  lin_gpio_map_t rx_gpio;
  lin_gpio_map_t sleep_gpio;
} lin_gpio_t;

typedef struct
{
  lin_channel_t chn;
  lin_dev_t dev;
  lin_data_len_t length;
  lin_checksum_t checksum;
  double rate;
  lin_bus_inactivity_time_t bus_inactiv_time;
  lin_wup_repeat_time_t wup_repeat_time;
} lin_config_t;

#define BK_ERR_LIN_NOT_INIT              (BK_ERR_LIN_BASE - 1)
#define BK_ERR_LIN_INT_TYPE              (BK_ERR_LIN_BASE - 2)
#define BK_ERR_LIN_BIT_ERROR             (BK_ERR_LIN_BASE - 3)
#define BK_ERR_LIN_CHK_ERROR             (BK_ERR_LIN_BASE - 4)
#define BK_ERR_LIN_TIMEOUT_ERROR         (BK_ERR_LIN_BASE - 5)
#define BK_ERR_LIN_PARITY_ERROR          (BK_ERR_LIN_BASE - 6)
#define BK_ERR_LIN_HAL_INVALID_ADDR      (BK_ERR_LIN_BASE - 7)
#define BK_ERR_LIN_HAL_INVALID_ARG       (BK_ERR_LIN_BASE - 8)

extern bk_err_t bk_lin_driver_init(void);
extern bk_err_t bk_lin_driver_deinit(void);
extern bk_err_t bk_lin_gpio_init(lin_channel_t chn);
extern bk_err_t bk_lin_cfg(lin_config_t *cfg);
extern bk_err_t bk_lin_set_dev(lin_dev_t dev);
extern bk_err_t bk_lin_set_rate(double rate);
extern bk_err_t bk_lin_set_data_length(lin_data_len_t len);
extern bk_err_t bk_lin_set_enh_check(lin_checksum_t check);
extern bk_err_t bk_lin_interrupt_enable(void);
extern bk_err_t bk_lin_interrupt_disable(void);
extern bk_err_t bk_lin_send(uint8_t *buf, uint32_t len);
extern bk_err_t bk_lin_recv(uint8_t *buf, uint32_t len);
extern bk_err_t bk_lin_tx(lin_id_t id, uint8_t *tx, uint32_t len);
extern bk_err_t bk_lin_rx(lin_id_t id, uint8_t *rx, uint32_t len,
                          uint32_t timeout);

_Static_assert(sizeof(lin_gpio_map_t) == 8u,
               "v3.1.1.9 LIN gpio map ABI changed");
_Static_assert(sizeof(lin_gpio_t) == 24u,
               "v3.1.1.9 LIN gpio ABI changed");
/* The ARM EABI toolchain builds with short enums, so the four one-byte
 * enum fields pack before the 8-byte-aligned double rate field.
 */
_Static_assert(sizeof(lin_config_t) == 24u,
               "v3.1.1.9 LIN config ABI changed");

#endif /* CONFIG_BK7258_LIN */

/****************************************************************************
 * AP segment-LCD (SLCD) ABI
 *
 * The exported driver/slcd_types.h pulls SDK-private gpio_map.h, so keep
 * the v3.1.1.9 types and entry points here.
 ****************************************************************************/

#ifdef CONFIG_BK7258_SLCD

typedef enum
{
  SLCD_SEG_0 = 0,
  SLCD_SEG_1,
  SLCD_SEG_2,
  SLCD_SEG_3,
  SLCD_SEG_4,
  SLCD_SEG_5,
  SLCD_SEG_6,
  SLCD_SEG_7,
  SLCD_SEG_8,
  SLCD_SEG_9,
  SLCD_SEG_10,
  SLCD_SEG_11,
  SLCD_SEG_12,
  SLCD_SEG_13,
  SLCD_SEG_14,
  SLCD_SEG_15,
  SLCD_SEG_16,
  SLCD_SEG_17,
  SLCD_SEG_18,
  SLCD_SEG_19,
  SLCD_SEG_20,
  SLCD_SEG_21,
  SLCD_SEG_22,
  SLCD_SEG_23,
  SLCD_SEG_24,
  SLCD_SEG_25,
  SLCD_SEG_26,
  SLCD_SEG_27,
  SLCD_SEG_28,
  SLCD_SEG_29,
  SLCD_SEG_30,
  SLCD_SEG_31
} slcd_seg_id_t;

typedef enum
{
  SLCD_COM_NUM_4 = 0,
  SLCD_COM_NUM_8
} slcd_com_num_t;

typedef enum
{
  SLCD_BIAS_1_PER_OF_3 = 0,
  SLCD_BIAS_1_PER_OF_4
} slcd_bias_t;

typedef enum
{
  SLCD_RATE_LEVEL_0 = 0,
  SLCD_RATE_LEVEL_1,
  SLCD_RATE_LEVEL_2,
  SLCD_RATE_LEVEL_3
} slcd_rate_t;

typedef struct
{
  slcd_com_num_t com_num;
  slcd_bias_t slcd_bias;
  slcd_rate_t slcd_rate;
} slcd_config_t;

extern void bk_slcd_driver_init(slcd_config_t slcd_config);
extern void bk_slcd_driver_deinit(void);
extern void bk_slcd_set_seg_value(slcd_seg_id_t seg_id, uint8_t value);
extern void bk_slcd_set_com_port_enable(uint8_t com_enable);
extern void bk_slcd_set_seg_port_enable(uint32_t seg_enable);
extern void bk_slcd_set_seg00_03_value(uint32_t value);
extern void bk_slcd_set_seg04_07_value(uint32_t value);
extern void bk_slcd_set_seg08_11_value(uint32_t value);
extern void bk_slcd_set_seg12_15_value(uint32_t value);
extern void bk_slcd_set_seg16_19_value(uint32_t value);
extern void bk_slcd_set_seg20_23_value(uint32_t value);
extern void bk_slcd_set_seg24_27_value(uint32_t value);
extern void bk_slcd_set_seg28_31_value(uint32_t value);

_Static_assert(sizeof(slcd_config_t) == 3u,
               "v3.1.1.9 SLCD config ABI changed");

#endif /* CONFIG_BK7258_SLCD */

/****************************************************************************
 * AP 8080-LCD data-plane ABI
 *
 * The public driver/lcd.h exposes bk_lcd_8080_* control and command APIs but
 * not the pixel data path.  The immutable bundle exports the two HAL
 * functions below from libcommon.a; declare them here for the framebuffer
 * lower half.
 ****************************************************************************/

#ifdef CONFIG_BK7258_LCD_8080

extern void lcd_hal_8080_data_send(uint32_t command, uint16_t *data,
                                   uint32_t len);
extern void lcd_hal_8080_ram_write(uint32_t command);

#endif /* CONFIG_BK7258_LCD_8080 */

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
extern bk_err_t bk_flash_partition_read(uint32_t partition,
                                        uint8_t *buffer, uint32_t offset,
                                        uint32_t length);
extern bk_err_t bk_flash_partition_write(uint32_t partition,
                                         const uint8_t *buffer,
                                         uint32_t offset, uint32_t length);
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

#endif /* __CHIPS_BK7258_COMMON_BK7258_SDK_ABI_H */
