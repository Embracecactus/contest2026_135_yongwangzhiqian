/****************************************************************************
 * tests/mocks/mock_sdk.h
 *
 * Test-facing control surface for the mocked Beken SDK mailbox transport and
 * boot-state.  The unit tests drive the notify state machine through these
 * hooks:
 *
 *   - mock_mbox_set_busy()        force mb_chnl_write() to return BK_ERR_BUSY
 *                                 (the -EAGAIN retry path)
 *   - mock_mbox_set_tx_complete() choose whether a successful write fires the
 *                                 TX-complete callback (simulate a lost
 *                                 completion interrupt by disabling it)
 *   - mock_mbox_inject_rx()       deliver an incoming mailbox command, exactly
 *                                 as the SDK RX ISR would
 *   - mock_mbox_get_last_write() / mock_mbox_write_count() / wait_for_write()
 *                                 observe what the wrapper actually sent
 *   - mock_mbox_set_boot_generation() / set_boot_state()
 *                                 drive the dispatch() generation gate
 *   - mock_mbox_fini()            stop the worker kthread
 ****************************************************************************/

#ifndef __MOCK_SDK_H
#define __MOCK_SDK_H

#include <stdint.h>
#include <stdbool.h>

#include <common/bk_err.h>
#include <driver/mailbox_channel.h>
#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

void mock_mbox_set_busy(bool busy);
void mock_mbox_set_tx_complete(bool enabled);
void mock_mbox_reset(void);

void mock_mbox_get_last_write(uint32_t *type, uint32_t *generation,
                              uint32_t *value);
int  mock_mbox_write_count(void);
int  mock_mbox_wait_for_write(int64_t timeout_ms);

void mock_mbox_inject_rx(uint32_t type, uint32_t generation, uint32_t value);

void mock_mbox_set_boot_generation(uint32_t generation);
void mock_mbox_set_boot_state(uint32_t state);

void mock_mbox_fini(void);

#endif /* __MOCK_SDK_H */
