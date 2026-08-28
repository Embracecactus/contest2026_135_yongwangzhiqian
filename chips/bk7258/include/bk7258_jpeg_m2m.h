/****************************************************************************
 * chips/bk7258/include/bk7258_jpeg_m2m.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 JPEG decoder V4L2 memory-to-memory adapter.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_M2M_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_M2M_H

#include <nuttx/compiler.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Register a single-planar V4L2 M2M decoder at devpath.  The output queue
 * accepts one complete JPEG image per USERPTR buffer and the capture queue
 * returns tightly packed YUYV USERPTR buffers.  Capture USERPTR addresses
 * use data-cache-line alignment when D-cache is enabled and natural pointer
 * alignment with the maintained non-cacheable AP handoff.  Capture
 * sizeimage includes any trailing cache-line ownership padding; DQBUF
 * bytesused reports only the YUYV payload.  Formats are conservatively frozen
 * after the first REQBUFS size query because this NuttX M2M upper half does
 * not expose REQBUFS teardown to the lower half; close and reopen to
 * renegotiate. */

int bk7258_jpeg_m2m_register(FAR const char *devpath);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_M2M_H */
