/****************************************************************************
 * apps/bk7258/test_shaniu_local_intent.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_local_intent.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct sample_s
{
  const char *text;
  int matched;
  enum bklocal_intent_kind_e kind;
  int value;
  bool relative;
  const char *expression;
};

static void expect_miss(const char *text)
{
  struct bklocal_intent_s intent =
  {
    BKLOCAL_STOP, 731, true, "unchanged"
  };

  assert(bklocal_intent_parse(text, &intent) == 0);
  assert(intent.kind == BKLOCAL_STOP && intent.value == 731 &&
         intent.relative && strcmp(intent.expression, "unchanged") == 0);
}

static void chinese_number(unsigned int value, char *text, size_t capacity)
{
  static const char *const digits[] =
  {
    "零", "一", "二", "三", "四", "五", "六", "七", "八", "九"
  };

  assert(capacity >= 16);
  if (value < 10)
    {
      snprintf(text, capacity, "%s", digits[value]);
    }
  else if (value == 10)
    {
      snprintf(text, capacity, "十");
    }
  else if (value < 20)
    {
      snprintf(text, capacity, "十%s", digits[value - 10]);
    }
  else if (value < 100)
    {
      snprintf(text, capacity, "%s十%s", digits[value / 10],
               value % 10 == 0 ? "" : digits[value % 10]);
    }
  else
    {
      snprintf(text, capacity, "一百");
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
  static const struct sample_s samples[] =
  {
    { "音量0", 1, BKLOCAL_VOLUME, 0, false, NULL },
    { " 音量百分之100。 ", 1, BKLOCAL_VOLUME, 100, false, NULL },
    { "把音量调到42%！", 1, BKLOCAL_VOLUME, 42, false, NULL },
    { "音量调到25百分号", 1, BKLOCAL_VOLUME, 25, false, NULL },
    { "将音量调到一百", 1, BKLOCAL_VOLUME, 100, false, NULL },
    { "音量九十九", 1, BKLOCAL_VOLUME, 99, false, NULL },
    { "请帮我音量加一点", 1, BKLOCAL_VOLUME, 10, true, NULL },
    { "帮我把音量调小一点", 1, BKLOCAL_VOLUME, -10, true, NULL },
    { "换成中性表情", 1, BKLOCAL_EYES, 0, false, "neutral" },
    { "换成开心表情", 1, BKLOCAL_EYES, 0, false, "happy" },
    { "换成害羞表情", 1, BKLOCAL_EYES, 0, false, "shy" },
    { "换成难过表情", 1, BKLOCAL_EYES, 0, false, "sad" },
    { "换成惊讶表情", 1, BKLOCAL_EYES, 0, false, "surprised" },
    { "换成思考表情", 1, BKLOCAL_EYES, 0, false, "thinking" },
    { "换成倾听表情", 1, BKLOCAL_EYES, 0, false, "listening" },
    { "换成说话表情", 1, BKLOCAL_EYES, 0, false, "speaking" },
    { "换成睡觉表情", 1, BKLOCAL_EYES, 0, false, "sleepy" },
    { "停止", 1, BKLOCAL_STOP, 0, false, NULL },
    { "停止说话！", 1, BKLOCAL_STOP, 0, false, NULL },
    { "取消回答", 1, BKLOCAL_STOP, 0, false, NULL },
    { "结束当前对话", 1, BKLOCAL_STOP, 0, false, NULL },
    { "音量101", 0, BKLOCAL_NONE, 0, false, NULL },
    { "音量一百零一", 0, BKLOCAL_NONE, 0, false, NULL },
    { "音量", 0, BKLOCAL_NONE, 0, false, NULL },
    { "音量加一点然后停止", 0, BKLOCAL_NONE, 0, false, NULL },
    { "不要音量加一点", 0, BKLOCAL_NONE, 0, false, NULL },
    { "如果音量加一点", 0, BKLOCAL_NONE, 0, false, NULL },
    { "“换成开心表情”", 0, BKLOCAL_NONE, 0, false, NULL },
    { "换成开心表情？", 0, BKLOCAL_NONE, 0, false, NULL },
    { "你能换成开心表情吗", 0, BKLOCAL_NONE, 0, false, NULL },
    { "我喜欢开心表情", 0, BKLOCAL_NONE, 0, false, NULL },
    { "停止后拍照识物", 0, BKLOCAL_NONE, 0, false, NULL }
  };

  unsigned int hits = 0;
  unsigned int false_hits = 0;
  unsigned int numeric_hits = 0;
  char number[16];
  char text[32];
  char too_long[257];
  char truncated[] =
  {
    (char)0xe9,
    (char)0x9f,
    '\0'
  };

  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
    {
      struct bklocal_intent_s intent =
      {
        BKLOCAL_STOP, 731, true, "unchanged"
      };

      int ret = bklocal_intent_parse(samples[i].text, &intent);

      if (ret == 1)
        {
          hits++;
        }

      if (ret != samples[i].matched)
        {
          false_hits++;
        }

      assert(ret == samples[i].matched);
      if (ret == 0)
        {
          assert(intent.kind == BKLOCAL_STOP && intent.value == 731 &&
                 intent.relative &&
                 strcmp(intent.expression, "unchanged") == 0);
          continue;
        }

      assert(intent.kind == samples[i].kind &&
             intent.value == samples[i].value &&
             intent.relative == samples[i].relative);
      if (samples[i].expression == NULL)
        assert(intent.expression == NULL);
      else
        assert(strcmp(intent.expression, samples[i].expression) == 0);
    }

  for (unsigned int value = 0; value <= 100; value++)
    {
      struct bklocal_intent_s intent =
      {
        BKLOCAL_NONE,
        -1,
        true,
        NULL
      };

      snprintf(text, sizeof(text), "音量%u", value);
      assert(bklocal_intent_parse(text, &intent) == 1);
      assert(intent.kind == BKLOCAL_VOLUME && intent.value == (int)value &&
             !intent.relative && intent.expression == NULL);
      numeric_hits++;

      chinese_number(value, number, sizeof(number));
      assert(snprintf(text, sizeof(text), "音量%s", number) <
             (int)sizeof(text));
      assert(bklocal_intent_parse(text, &intent) == 1);
      assert(intent.kind == BKLOCAL_VOLUME && intent.value == (int)value &&
             !intent.relative && intent.expression == NULL);
      numeric_hits++;
    }

  expect_miss("音量-1");
  expect_miss("音量1.0");
  expect_miss("音量101");
  expect_miss("音量999999999999999999999999999999");
  expect_miss("音量十百");
  expect_miss("音量1!x");
  expect_miss("音量10。！");
  expect_miss("音量10。然后停止");
  expect_miss("换成开心表情!！");
  expect_miss("停止.");
  expect_miss(NULL);

  memset(text, '1', sizeof(text));
  text[sizeof(text) - 1] = '\0';
  expect_miss(text);

  memset(too_long, '1', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  expect_miss(too_long);
  expect_miss(truncated);

  printf("samples=%u hits=%u false_hits=%u numeric_hits=%u\n",
         (unsigned int)(sizeof(samples) / sizeof(samples[0])), hits,
         false_hits, numeric_hits);
  return false_hits == 0 ? 0 : 1;
}
