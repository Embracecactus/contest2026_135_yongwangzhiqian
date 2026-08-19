/*
 * mock nuttx/board.h - host shim for the NuttX boardctl boot-image ABI.
 * Only the FAR macro and the board_boot_image prototype are required by
 * bk7258_bl2_mcuboot_boot.c.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MOCK_NUTTX_BOARD_H
#define MOCK_NUTTX_BOARD_H

#include <stdint.h>

#ifndef FAR
#define FAR
#endif

int board_boot_image(FAR const char *path, uint32_t hdr_size);

#endif /* MOCK_NUTTX_BOARD_H */