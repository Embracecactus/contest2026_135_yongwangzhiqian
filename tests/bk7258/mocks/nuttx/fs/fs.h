/****************************************************************************
 * tests/bk7258/mocks/nuttx/fs.h
 *
 * Host shim for the NuttX character-driver surface used by bk7258_irda.c:
 * struct file / struct file_operations and register_driver.  The
 * implementation lives in framework/mock_sdk_irda.c.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_FS_H
#define __MOCK_NUTTX_FS_H

#include <stddef.h>
#include <sys/types.h>

#ifndef FAR
#define FAR
#endif

struct file
{
  int f_unused;
};

struct file_operations
{
  int (*open)(FAR struct file *filep);
  int (*close)(FAR struct file *filep);
  ssize_t (*read)(FAR struct file *filep, FAR char *buffer, size_t buflen);
  ssize_t (*write)(FAR struct file *filep, FAR const char *buffer,
                   size_t buflen);
  int (*ioctl)(FAR struct file *filep, int cmd, unsigned long arg);
};

int register_driver(FAR const char *path,
                    FAR const struct file_operations *fops, mode_t mode,
                    FAR void *priv);

#endif /* __MOCK_NUTTX_FS_H */
