/****************************************************************************
 * chips/bk7258/include/bk7258_jpeg_m2m_validation.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debugger-visible contract for bounded BK7258 JPEG V4L2 M2M validation.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_M2M_VALIDATION_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_M2M_VALIDATION_H

#include <nuttx/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_JPEG_M2M_VALIDATION_MAGIC    0x564d4a42u /* "BJMV" */
#define BK7258_JPEG_M2M_VALIDATION_VERSION  1u

#define BK7258_JPEG_M2M_VALIDATION_RUNNING  1u
#define BK7258_JPEG_M2M_VALIDATION_PASSED   2u
#define BK7258_JPEG_M2M_VALIDATION_FAILED   3u

enum bk7258_jpeg_m2m_validation_stage_e
{
  BK7258_JPEG_M2M_VALIDATION_STAGE_INIT = 1,
  BK7258_JPEG_M2M_VALIDATION_STAGE_OPEN,
  BK7258_JPEG_M2M_VALIDATION_STAGE_SECOND_OPEN,
  BK7258_JPEG_M2M_VALIDATION_STAGE_QUERYCAP,
  BK7258_JPEG_M2M_VALIDATION_STAGE_ENUM_FMT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_TRY_FMT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_S_FMT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_G_FMT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_MMAP_REJECT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_USERPTR_REQBUFS,
  BK7258_JPEG_M2M_VALIDATION_STAGE_LATE_S_FMT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_ALLOCATE,
  BK7258_JPEG_M2M_VALIDATION_STAGE_VALID_QBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_STREAMON,
  BK7258_JPEG_M2M_VALIDATION_STAGE_VALID_DQBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_VALID_RESULT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_NEGATIVE_QBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_NEGATIVE_DQBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_NEGATIVE_RESULT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_RECOVERY_QBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_RECOVERY_DQBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_RECOVERY_RESULT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_QBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_STREAMOFF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_DQBUF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_DRAIN_RESULT,
  BK7258_JPEG_M2M_VALIDATION_STAGE_CAPTURE_STREAMOFF,
  BK7258_JPEG_M2M_VALIDATION_STAGE_CLOSE,
  BK7258_JPEG_M2M_VALIDATION_STAGE_COMPLETE,
};

/* This layout is versioned and contains only fixed-width scalar fields so a
 * non-halting debugger can consume it without target type information.  CRC
 * fields are observations from the current hardware run, not predeclared
 * golden values.
 */

struct bk7258_jpeg_m2m_validation_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t  result;
  uint32_t stage;
  uint32_t checks;
  uint32_t capabilities;
  uint32_t device_caps;
  uint32_t output_fourcc;
  uint32_t capture_fourcc;
  uint32_t width;
  uint32_t height;
  uint32_t fixture_bytes;
  uint32_t fixture_crc32;
  uint32_t capture_size; /* G_FMT sizeimage, including cache-line padding */
  uint32_t poll_calls;
  uint32_t dqbuf_count;
  uint32_t completed_pairs;
  uint32_t valid_output_flags;
  uint32_t valid_capture_flags;
  uint32_t valid_capture_bytesused;
  uint32_t valid_sequence;
  uint32_t valid_crc32;
  uint32_t valid_nonzero_bytes;
  uint32_t valid_min_byte;
  uint32_t valid_max_byte;
  uint32_t negative_output_flags;
  uint32_t negative_capture_flags;
  uint32_t negative_capture_bytesused;
  uint32_t recovery_capture_flags;
  uint32_t recovery_capture_bytesused;
  uint32_t recovery_sequence;
  uint32_t recovery_crc32;
  uint32_t recovery_nonzero_bytes;
  uint32_t recovery_min_byte;
  uint32_t recovery_max_byte;
  uint32_t drain_output_flags;
  uint32_t drain_output_bytesused;
};

extern volatile struct bk7258_jpeg_m2m_validation_diag_s
  g_bk7258_jpeg_m2m_validation_diag;

int bk7258_jpeg_m2m_validation_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_JPEG_M2M_VALIDATION_H */
