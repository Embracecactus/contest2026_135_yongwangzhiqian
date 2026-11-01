/****************************************************************************
 * apps/bk7258/bk7258_reply_expression.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_REPLY_EXPRESSION_H
#define __APP_BK7258_REPLY_EXPRESSION_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bkreply_expression_ops_s
{
  int (*start)(const char *, uint32_t *);
  int (*cancel)(uint32_t);
  void (*active)(bool);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bkreply_expression_init(const struct bkreply_expression_ops_s *ops);
int bkreply_expression_begin(uint64_t request);
int bkreply_expression_set(uint64_t request, const char *emotion);
int bkreply_expression_start(uint64_t request);
void bkreply_expression_finish(uint64_t request);
void bkreply_expression_step(void);

#endif
