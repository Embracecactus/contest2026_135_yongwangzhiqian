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

/* A single worker owns this zero-initialized object and the mounted volume
 * for its complete lifetime. Append performs I/O; never call it from a short
 * control callback. No operation here authenticates a remote peer.
 */

#define BKDISPLAY_UPLOAD_CHUNK_MAX 4096u

struct bkdisplay_upload_s
{
  char root[BKDISPLAY_PACK_PATH_SIZE];
  char temporary[BKDISPLAY_PACK_PATH_SIZE];
  size_t expected;
  size_t received;
  int fd;
  int state;
  int error;
};

int bkdisplay_upload_begin(struct bkdisplay_upload_s *upload,
                           const char *root, size_t size);
int bkdisplay_upload_append(struct bkdisplay_upload_s *upload, size_t offset,
                            const void *data, size_t size);

/* Installs a validated pack, without changing the persistent selection.
 * Commit is synchronous and cannot be canceled concurrently. A failed close
 * is latched; the owner must not treat it as a confirmed resource exit.
 */

int bkdisplay_upload_finish(struct bkdisplay_upload_s *upload,
                            struct bkdisplay_store_selection_s *selection);
int bkdisplay_upload_cancel(struct bkdisplay_upload_s *upload);
/* Zero confirms no live or uncertain descriptor owned by this upload. */

int bkdisplay_upload_quiesced(const struct bkdisplay_upload_s *upload);

/* The root is a mounted FAT volume, not /dev/mmcsd0 itself. */

int bkdisplay_store_ensure(const char *root);
/* Open exactly this installed immutable pack for a volatile render. Never
 * consult/change active.json or fall back on validation error.
 * The caller owns the mounted-volume lease and closes the returned object.
 */

int bkdisplay_store_open_installed(
  const char *root, const char *filename,
  struct bkdisplay_store_selection_s *selection,
  struct bkdisplay_pack_s **pack);

/* Resolves and returns one fully validated pack.  The caller owns the pack
 * and must close it before releasing the mounted volume lease. */
int bkdisplay_store_resolve_open(const char *root,
                                 struct bkdisplay_store_selection_s *selection,
                                 struct bkdisplay_pack_s **pack);
int bkdisplay_store_resolve(const char *root,
                            struct bkdisplay_store_selection_s *selection);
int bkdisplay_store_activate(const char *root, const char *filename,
                             struct bkdisplay_store_selection_s *selection);
/* Forget only the user's selected pack. Installed factory/resource packs and
 * staging remain intact; directory entry removal is synchronized. */
int bkdisplay_store_reset_selection(const char *root);
int bkdisplay_store_install(const char *root, const char *filename,
                            struct bkdisplay_store_selection_s *selection);
int bkdisplay_store_import(const char *root, const void *data, size_t size,
                           struct bkdisplay_store_selection_s *selection);

#endif /* __APP_BK7258_BK7258_DISPLAY_STORE_H */
