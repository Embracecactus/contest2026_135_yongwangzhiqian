/****************************************************************************
 * board/bk7258/chip/include/bk7258_irda_abi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private ABI boundary for the CP-side BK7258 IrDA driver exported by the
 * immutable v3.1.1.9 SDK archive (cp/libs/libdriver.a, irda.c.obj).
 *
 * The v3.1.1.9 CP SDK compiles middleware/driver/irda/irda.c
 * (CONFIG_SUPPORT_IRDA=y) and exports irda_init()/irda_exit()/irda_isr()/
 * set_irda_usrcode()/Irda_init_app()/IR_get_key().  The SDK does not publish
 * the corresponding header in the exported bundle, so the declarations below
 * are reproduced from the authorized SDK source
 *   cp/middleware/driver/include/bk_private/bk_irda.h
 * unchanged, with only the Beken integer typedefs mapped to C99 stdint.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_IRDA_ABI_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_IRDA_ABI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define IRDA_FAILURE                (1)
#define IRDA_SUCCESS                (0)

#define IRDA_DEV_NAME                "irda"

#define IRDA_CMD_MAGIC              (0xe290000)

#define USERCODE_MASK                0xffff

#define KEY_CODE_MASK                0xff0000
#define KEY_CODE_SHIFT               16

#define KEY_CODE_INVERS_MASK         0xff000000
#define KEY_CODE_INVERS_SHIFT        24

#define KEY_SHORT_CNT                3
#define KEY_LONG_CNT                 8
#define KEY_HOLD_CNT                 11

#define GENERATE_KEY(type, value)    (((type) << 24) | (value))
#define GET_KEY_TYPE(msg)            (((msg) >> 24) & 0xff)
#define GET_KEY_VALUE(msg)           ((msg) & 0xff)

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum
{
  IRDA_CMD_ACTIVE = IRDA_CMD_MAGIC + 1,
  IRDA_CMD_SET_POLARITY,
  IRDA_CMD_SET_CLK,
  IRDA_CMD_SET_INT_MASK,
};

enum
{
  IR_KEY_TYPE_SHORT = 0,
  IR_KEY_TYPE_LONG,
  IR_KEY_TYPE_HOLD,
  IR_KEY_TYPE_MAX,
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void irda_init(void);
void irda_exit(void);
void irda_isr(void);
void Irda_init_app(void);
void set_irda_usrcode(uint16_t ir_usercode);
long IR_get_key(void *buffer, unsigned long size, int32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_IRDA_ABI_H */
