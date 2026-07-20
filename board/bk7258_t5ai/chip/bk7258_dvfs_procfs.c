/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_dvfs_procfs.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * /proc/dvfs interface for the BK7258 DVFS lower half.  Exposes the current
 * CPU frequency tier and accepts "cur_freq <tier>" to step it at runtime,
 * giving userspace a way to verify the DVFS path is live (not just a boot-
 * time one-shot).
 *
 * Modelled on arch/arm/src/lc823450/lc823450_procfs_dvfs.c (lc823450 is the
 * NuttX OSS precedent for chip-local DVFS without the PM state machine).
 *
 * Contract:
 *   read  -> "cur_freq <tier>\n"  (tier = BK7258_FREQ_* integer 0..5)
 *   write -> "cur_freq <tier>\n"  steps the core clock to <tier>
 *           (out-of-range values are rejected by bk7258_dvfs_set_freq)
 *
 * Requires CONFIG_FS_PROCFS (independently mounted at /proc by
 * bk7258_bringup.c(board_app_initialize).  bk7258_dvfs_procfs_register()
 * must be called *before* the procfs mount (per fs_procfs.c NOTE).
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>

#include "bk7258_dvfs.h"

#define BK7258_DVFS_LINELEN  128

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_dvfs_file_s
{
  struct procfs_file_s base;     /* Base open file structure */
  unsigned int         linesize; /* Valid chars in line[] */
  char                 line[BK7258_DVFS_LINELEN];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     bk7258_dvfs_open(struct file *filep, const char *relpath,
                                int oflags, mode_t mode);
static int     bk7258_dvfs_close(struct file *filep);
static ssize_t bk7258_dvfs_read(struct file *filep, char *buffer,
                                size_t buflen);
static ssize_t bk7258_dvfs_write(struct file *filep, const char *buffer,
                                 size_t buflen);
static int     bk7258_dvfs_stat(const char *relpath, struct stat *buf);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct procfs_operations g_bk7258_dvfs_ops =
{
  bk7258_dvfs_open,
  bk7258_dvfs_close,
  bk7258_dvfs_read,
  bk7258_dvfs_write,
  NULL,            /* poll */
  NULL,            /* opendir */
  NULL,            /* closedir */
  NULL,            /* readdir */
  NULL,            /* rewinddir */
  bk7258_dvfs_stat
};

static const struct procfs_entry_s g_bk7258_dvfs_procfs =
{
  "dvfs",
  &g_bk7258_dvfs_ops
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_dvfs_open(struct file *filep, const char *relpath,
                           int oflags, mode_t mode)
{
  struct bk7258_dvfs_file_s *priv;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  filep->f_priv = priv;
  return OK;
}

static int bk7258_dvfs_close(struct file *filep)
{
  struct bk7258_dvfs_file_s *priv = filep->f_priv;

  if (priv != NULL)
    {
      kmm_free(priv);
    }

  filep->f_priv = NULL;
  return OK;
}

static ssize_t bk7258_dvfs_read(struct file *filep, char *buffer,
                                size_t buflen)
{
  struct bk7258_dvfs_file_s *priv = filep->f_priv;
  size_t totalsize;
  size_t copysize;
  size_t remaining;
  off_t  offset = filep->f_pos;

  if (priv == NULL)
    {
      return -EIO;
    }

  remaining = buflen;
  totalsize = 0;

  priv->linesize = snprintf(priv->line, BK7258_DVFS_LINELEN,
                            "cur_freq %d\n", bk7258_dvfs_get_freq());
  copysize = procfs_memcpy(priv->line, priv->linesize, buffer, remaining,
                           &offset);
  totalsize += copysize;

  if (totalsize > 0)
    {
      filep->f_pos += totalsize;
    }

  return totalsize;
}

static ssize_t bk7258_dvfs_write(struct file *filep, const char *buffer,
                                 size_t buflen)
{
  char line[BK7258_DVFS_LINELEN];
  char cmd[16];
  int  n;
  int  tier;

  n = MIN(buflen, sizeof(line) - 1);
  strlcpy(line, buffer, (size_t)n + 1);

  n = strcspn(line, " ");                    /* index of the space separator */
  n = MIN((unsigned)n, sizeof(cmd) - 1);
  strlcpy(cmd, line, (size_t)n + 1);

  if (strcmp(cmd, "cur_freq") == 0)
    {
      tier = atoi(line + n + 1);             /* parse integer after the space */
      bk7258_dvfs_set_freq(tier);
    }

  return (ssize_t)buflen;
}

static int bk7258_dvfs_stat(const char *relpath, struct stat *buf)
{
  buf->st_mode    =
    S_IFREG |
    S_IROTH | S_IWOTH |
    S_IRGRP | S_IWGRP |
    S_IRUSR | S_IWUSR;
  buf->st_size    = 0;
  buf->st_blksize = 0;
  buf->st_blocks  = 0;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_dvfs_procfs_register(void)
{
  return procfs_register(&g_bk7258_dvfs_procfs);
}