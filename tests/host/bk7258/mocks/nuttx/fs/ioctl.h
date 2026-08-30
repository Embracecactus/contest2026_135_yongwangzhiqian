/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/nuttx/fs/ioctl.h
 *
 * Host stand-in for <nuttx/fs/ioctl.h>: the _IOC() encoding used by the
 * board driver ioctl command definitions.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_FS_IOCTL_H
#define __MOCK_NUTTX_FS_IOCTL_H

#define _IOC(type, nr)  ((int)((type) | (nr)))

#endif /* __MOCK_NUTTX_FS_IOCTL_H */
