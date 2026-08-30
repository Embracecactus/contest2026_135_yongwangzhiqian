/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/nuttx/mtd/mtd.h
 *
 * Minimal host ABI for the MTD operations exercised by BK7258 board tests.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_MTD_MTD_H
#define __MOCK_NUTTX_MTD_MTD_H

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <sys/types.h>
#include <stdint.h>

#include <nuttx/fs/ioctl.h>

#ifndef CODE
#  define CODE
#endif

#ifndef NAME_MAX
#  define NAME_MAX 255
#endif

#ifndef _MTDIOC
#  define _MTDIOC(nr) _IOC(0x0600, (nr))
#endif

#define MTDIOC_GEOMETRY   _MTDIOC(0x0001)
#define MTDIOC_ERASESTATE _MTDIOC(0x000a)

struct mtd_geometry_s
{
  uint32_t blocksize;
  uint32_t erasesize;
  uint32_t neraseblocks;
  char model[NAME_MAX + 1];
};

struct mtd_dev_s
{
  CODE int (*erase)(FAR struct mtd_dev_s *dev, off_t startblock,
                    size_t nblocks);
  CODE ssize_t (*bread)(FAR struct mtd_dev_s *dev, off_t startblock,
                        size_t nblocks, FAR uint8_t *buffer);
  CODE ssize_t (*bwrite)(FAR struct mtd_dev_s *dev, off_t startblock,
                         size_t nblocks, FAR const uint8_t *buffer);
  CODE ssize_t (*read)(FAR struct mtd_dev_s *dev, off_t offset,
                       size_t nbytes, FAR uint8_t *buffer);
#ifdef CONFIG_MTD_BYTE_WRITE
  CODE ssize_t (*write)(FAR struct mtd_dev_s *dev, off_t offset,
                        size_t nbytes, FAR const uint8_t *buffer);
#endif
  CODE int (*ioctl)(FAR struct mtd_dev_s *dev, int cmd, unsigned long arg);
  CODE int (*isbad)(FAR struct mtd_dev_s *dev, off_t block);
  CODE int (*markbad)(FAR struct mtd_dev_s *dev, off_t block);
  FAR const char *name;
};

#endif /* __MOCK_NUTTX_MTD_MTD_H */
