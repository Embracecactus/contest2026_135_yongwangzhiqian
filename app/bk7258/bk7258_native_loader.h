/****************************************************************************
 * apps/bk7258/bk7258_native_loader.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __APP_BK7258_NATIVE_LOADER_H
#define __APP_BK7258_NATIVE_LOADER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_abi.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bknative_loader_open(const char *path, void **handle,
                         const struct bknative_program_s **program);
int bknative_loader_close(void *handle);

#endif
