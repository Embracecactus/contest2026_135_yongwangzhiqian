/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_BOOTSTRAP_H
#define __APP_BK7258_PROVISION_BOOTSTRAP_H
#include <stdbool.h>
#include "bk7258_provision_identity.h"
int bkprov_bootstrap_start(void);
/* 0: legacy supplied identity; 1: committed native identity; negative: not ready. */
int bkprov_bootstrap_status(void);
bool bkprov_bootstrap_busy(void);
void bkprov_bootstrap_cancel(void);
/* Called by the single product owner, never a GATT callback. */
int bkprov_bootstrap_window(bool open, unsigned char secret[32], void *identity);
#endif
