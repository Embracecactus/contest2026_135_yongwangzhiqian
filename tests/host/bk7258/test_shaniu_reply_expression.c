/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_reply_expression.h"

static unsigned int starts;
static unsigned int cancels;
static bool active;
static bool busy;
static char expression[16];

static int start(const char *value, uint32_t *id)
{
  strcpy(expression, value);
  *id = ++starts;
  return 0;
}

static int cancel(uint32_t id)
{
  assert(id == starts);
  cancels++;
  return busy ? -EBUSY : 0;
}

static void set_active(bool value)
{
  active = value;
}

int main(void)
{
  const struct bkreply_expression_ops_s ops = {start, cancel, set_active};
  bkreply_expression_init(&ops);
  assert(bkreply_expression_begin(1) == 0);
  assert(bkreply_expression_set(1, "happy") == 0);
  assert(starts == 0 && !active);
  assert(bkreply_expression_begin(1) == 0);
  assert(bkreply_expression_start(1) == 0);
  assert(active && starts == 1 && !strcmp(expression, "happy"));
  assert(bkreply_expression_start(1) == 0 && starts == 1);
  assert(bkreply_expression_set(1, "sorry") == -EBUSY);
  bkreply_expression_finish(1);
  assert(!active && cancels == 1);
  assert(bkreply_expression_set(1, "happy") == -ESTALE);
  assert(bkreply_expression_begin(1) == -ESTALE);

  assert(bkreply_expression_begin(2) == 0);
  assert(bkreply_expression_set(2, "invalid") == -EINVAL);
  assert(bkreply_expression_set(2, "happy") == 0);
  bkreply_expression_finish(2);
  assert(bkreply_expression_start(2) == -ESTALE && starts == 1);

  assert(bkreply_expression_begin(3) == 0);
  assert(bkreply_expression_start(3) == 0 && starts == 1);
  bkreply_expression_finish(3);
  assert(bkreply_expression_begin(4) == 0);
  assert(bkreply_expression_set(4, "sorry") == 0);
  assert(bkreply_expression_start(4) == 0 && !strcmp(expression, "sad"));
  busy = true;
  bkreply_expression_finish(4);
  assert(!active);
  bkreply_expression_step();
  assert(bkreply_expression_begin(5) == 0);
  assert(bkreply_expression_set(5, "thinking") == 0);
  busy = false;
  bkreply_expression_step();
  assert(bkreply_expression_start(5) == 0 && active);
  bkreply_expression_finish(4);
  assert(active);
  assert(bkreply_expression_begin(6) == 0 && !active);
  assert(bkreply_expression_set(5, "happy") == -ESTALE);
  assert(bkreply_expression_set(6, "confused") == 0);
  assert(bkreply_expression_start(6) == 0 && active);
  assert(!strcmp(expression, "thinking"));
  bkreply_expression_finish(6);
  assert(!active);
  puts("REPLY_EXPRESSION_PASS");
  return 0;
}
