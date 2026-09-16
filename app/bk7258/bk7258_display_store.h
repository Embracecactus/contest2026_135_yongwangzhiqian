/****************************************************************************
 * app/bk7258/bk7258_display_store.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable SD NAND transaction layer for Shaniu display packs.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_STORE_H
#define __APP_BK7258_BK7258_DISPLAY_STORE_H

#include <stdbool.h>

#include "bk7258_display_pack.h"

#define BKDISPLAY_STORE_FILENAME_SIZE  40u
#define BKDISPLAY_STORE_DEFAULT_PACK   "shaniu-default-v1.bkep"

struct bkdisplay_store_selection_s
{
  char filename[BKDISPLAY_STORE_FILENAME_SIZE];
  char path[BKDISPLAY_PACK_PATH_SIZE];
  bool fallback;
  struct bkdisplay_pack_info_s info;
};

/* The root is a mounted FAT volume, not /dev/mmcsd0 itself. */

int bkdisplay_store_ensure(const char *root);
int bkdisplay_store_resolve(const char *root,
                            struct bkdisplay_store_selection_s *selection);
int bkdisplay_store_activate(const char *root, const char *filename,
                             struct bkdisplay_store_selection_s *selection);
int bkdisplay_store_install(const char *root, const char *filename,
                            struct bkdisplay_store_selection_s *selection);

#endif /* __APP_BK7258_BK7258_DISPLAY_STORE_H */
