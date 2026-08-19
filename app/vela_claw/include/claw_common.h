/****************************************************************************
 * app/vela_claw/include/claw_common.h
 *
 * Vela-Claw — openvela/NuttX native "Chat Coding" AI agent framework.
 * Common types and error codes.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_COMMON_H
#define VELA_CLAW_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VELA_CLAW_VERSION "0.1.0"

/* Error model: 0 == OK, negative == errno-style code. */
typedef int claw_err_t;

#define CLAW_OK           0
#define CLAW_ENOMEM      (-ENOMEM)
#define CLAW_EINVAL      (-EINVAL)
#define CLAW_EAGAIN      (-EAGAIN)
#define CLAW_ETIMEDOUT   (-ETIMEDOUT)
#define CLAW_ENOENT      (-ENOENT)
#define CLAW_EIO         (-EIO)
#define CLAW_ENOSYS      (-ENOSYS)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_COMMON_H */
