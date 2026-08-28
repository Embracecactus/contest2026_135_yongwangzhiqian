/****************************************************************************
 * tests/bk7258/mocks/bk7258_sdk_abi.h
 *
 * Host mock of chips/bk7258/common/bk7258_sdk_abi.h, restricted to
 * the CAN ABI block (the real one is guarded by CONFIG_BK7258_CAN, which
 * mocks/nuttx/config.h keeps undefined so the driver TU sees these
 * declarations instead of the real ones).
 *
 * Function names/signatures mirror the v3.1.1.9 SDK; the suite mock
 * implements them.
 ****************************************************************************/

#ifndef __MOCK_BK7258_SDK_ABI_H
#define __MOCK_BK7258_SDK_ABI_H

#include <stdbool.h>
#include <stdint.h>

#include <driver/hal/hal_can_types.h>
#include <common/bk_err.h>

bk_err_t bk_can_driver_init(void);
bk_err_t bk_can_driver_deinit(void);
bk_err_t bk_can_receive(uint8_t *data, uint32_t expect_size,
                        uint32_t *recv_size, uint32_t timeout);
bk_err_t bk_can_send_ptb(can_frame_s *frame);
void bk_can_register_isr_callback(can_callback_des_t *rx_cb,
                                  can_callback_des_t *tx_cb);
void bk_can_register_err_callback(can_callback_des_t *err_cb);
bk_err_t can_driver_bit_rate_config(can_bit_rate_e s_speed,
                                    can_bit_rate_e f_speed);
bk_err_t bk_can_abort_ptb(void);
bk_err_t bk_can_abort_all(void);

void can_hal_set_lbmi(uint32_t value);
uint32_t can_hal_get_lbmi(void);
bk_err_t can_hal_ctrl(uint32_t command, void *parameter);

#endif /* __MOCK_BK7258_SDK_ABI_H */
