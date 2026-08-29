/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_camera_glue.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARDS_BK7258_AIDK_AI_TOY_SRC_BK7258_AIDK_CAMERA_GLUE_H
#define __BOARDS_BK7258_AIDK_AI_TOY_SRC_BK7258_AIDK_CAMERA_GLUE_H

#include <nuttx/compiler.h>

struct bk7258_dvp_binding_s;

FAR const struct bk7258_dvp_binding_s *bk7258_aidk_camera_binding(void);

#endif /* __BOARDS_BK7258_AIDK_AI_TOY_SRC_BK7258_AIDK_CAMERA_GLUE_H */
