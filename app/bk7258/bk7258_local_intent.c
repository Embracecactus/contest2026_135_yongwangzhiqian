/****************************************************************************
 * apps/bk7258/bk7258_local_intent.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_local_intent.h"

#include <stddef.h>
#include <string.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bklocal_expression_s
{
  const char *name;
  const char *expression;
};

struct bklocal_number_s
{
  const char *name;
  int value;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bklocal_expression_s g_expressions[] =
{
  { "中性", "neutral" },
  { "开心", "happy" },
  { "害羞", "shy" },
  { "难过", "sad" },
  { "惊讶", "surprised" },
  { "思考", "thinking" },
  { "倾听", "listening" },
  { "说话", "speaking" },
  { "睡觉", "sleepy" }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bklocal_space(char value)
{
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool bklocal_equal(const char *text, size_t size, const char *word)
{
  size_t word_size = strlen(word);

  return size == word_size && memcmp(text, word, size) == 0;
}

static bool bklocal_has(const char *text, size_t size, const char *word)
{
  size_t word_size = strlen(word);

  if (word_size > size)
    {
      return false;
    }

  for (size_t i = 0; i + word_size <= size; i++)
    {
      if (memcmp(text + i, word, word_size) == 0)
        {
          return true;
        }
    }

  return false;
}

static int bklocal_number(const char *text, size_t size, int *value)
{
  static const struct bklocal_number_s numbers[] =
  {
    { "零", 0 },
    { "一", 1 },
    { "二", 2 },
    { "两", 2 },
    { "三", 3 },
    { "四", 4 },
    { "五", 5 },
    { "六", 6 },
    { "七", 7 },
    { "八", 8 },
    { "九", 9 },
    { "十", 10 },
    { "十一", 11 },
    { "十二", 12 },
    { "二十", 20 },
    { "三十", 30 },
    { "四十", 40 },
    { "五十", 50 },
    { "六十", 60 },
    { "七十", 70 },
    { "八十", 80 },
    { "九十", 90 },
    { "一百", 100 }
  };

  int parsed = 0;

  if (size == 0 || value == NULL)
    {
      return 0;
    }

  for (size_t i = 0; i < size; i++)
    {
      if (text[i] < '0' || text[i] > '9')
        {
          parsed = -1;
          break;
        }

      parsed = parsed * 10 + text[i] - '0';
      if (parsed > 100)
        {
          return 0;
        }
    }

  if (parsed >= 0)
    {
      *value = parsed;
      return 1;
    }

  for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
    {
      if (bklocal_equal(text, size, numbers[i].name))
        {
          *value = numbers[i].value;
          return 1;
        }
    }

  /* The finite remainder of normal 0--100 Chinese number spelling. */

  for (int tens = 2; tens <= 9; tens++)
    {
      char prefix[7];
      static const char *digits[] =
      {
        "", "一", "二", "三", "四", "五", "六", "七", "八", "九"
      };

      size_t prefix_size;

      memcpy(prefix, digits[tens], strlen(digits[tens]));
      memcpy(prefix + strlen(digits[tens]), "十", strlen("十") + 1);
      prefix_size = strlen(prefix);
      if (size >= prefix_size && memcmp(text, prefix, prefix_size) == 0)
        {
          if (size == prefix_size)
            {
              *value = tens * 10;
              return 1;
            }

          for (size_t one = 1; one <= 9; one++)
            {
              if (bklocal_equal(text + prefix_size, size - prefix_size,
                                digits[one]))
                {
                  *value = tens * 10 + (int)one;
                  return 1;
                }
            }
        }
    }

  if (size > strlen("十") && memcmp(text, "十", strlen("十")) == 0)
    {
      static const char *ones[] =
      {
        "", "一", "二", "三", "四", "五", "六", "七", "八", "九"
      };

      for (size_t one = 1; one <= 9; one++)
        {
          if (bklocal_equal(text + strlen("十"), size - strlen("十"),
                            ones[one]))
            {
              *value = 10 + (int)one;
              return 1;
            }
        }
    }

  return 0;
}

static int bklocal_volume(const char *text, size_t size,
                          struct bklocal_intent_s *intent)
{
  static const char *const prefixes[] =
  {
    "音量调到", "把音量调到", "将音量调到", "音量"
  };

  const char *number = NULL;
  size_t number_size = 0;
  int value;

  if (bklocal_equal(text, size, "音量加一点") ||
      bklocal_equal(text, size, "音量大一点") ||
      bklocal_equal(text, size, "把音量调大一点"))
    {
      intent->kind = BKLOCAL_VOLUME;
      intent->value = 10;
      intent->relative = true;
      intent->expression = NULL;
      return 1;
    }

  if (bklocal_equal(text, size, "音量减一点") ||
      bklocal_equal(text, size, "音量小一点") ||
      bklocal_equal(text, size, "把音量调小一点"))
    {
      intent->kind = BKLOCAL_VOLUME;
      intent->value = -10;
      intent->relative = true;
      intent->expression = NULL;
      return 1;
    }

  for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
      size_t prefix_size = strlen(prefixes[i]);

      if (size > prefix_size && memcmp(text, prefixes[i], prefix_size) == 0)
        {
          number = text + prefix_size;
          number_size = size - prefix_size;
          break;
        }
    }

  if (number == NULL)
    {
      return 0;
    }

  if (number_size >= strlen("百分之") &&
      memcmp(number, "百分之", strlen("百分之")) == 0)
    {
      number += strlen("百分之");
      number_size -= strlen("百分之");
    }
  else if (number_size > 1 && number[number_size - 1] == '%')
    {
      number_size--;
    }
  else if (number_size >= strlen("百分号") &&
           memcmp(number + number_size - strlen("百分号"), "百分号",
                  strlen("百分号")) == 0)
    {
      number_size -= strlen("百分号");
    }
  else if (number_size >= strlen("百分") &&
           memcmp(number + number_size - strlen("百分"), "百分",
                  strlen("百分")) == 0)
    {
      number_size -= strlen("百分");
    }

  if (!bklocal_number(number, number_size, &value) || value < 0 ||
      value > 100)
    {
      return 0;
    }

  intent->kind = BKLOCAL_VOLUME;
  intent->value = value;
  intent->relative = false;
  intent->expression = NULL;
  return 1;
}

static int bklocal_eyes(const char *text, size_t size,
                        struct bklocal_intent_s *intent)
{
  static const char lead[] = "换成";
  static const char tail[] = "表情";
  size_t lead_size = strlen(lead);
  size_t tail_size = strlen(tail);

  if (size <= lead_size + tail_size || memcmp(text, lead, lead_size) != 0 ||
      memcmp(text + size - tail_size, tail, tail_size) != 0)
    {
      return 0;
    }

  for (size_t i = 0; i < sizeof(g_expressions) / sizeof(g_expressions[0]);
       i++)
    {
      if (bklocal_equal(text + lead_size, size - lead_size - tail_size,
                        g_expressions[i].name))
        {
          intent->kind = BKLOCAL_EYES;
          intent->value = 0;
          intent->relative = false;
          intent->expression = g_expressions[i].expression;
          return 1;
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bklocal_intent_parse(const char *text, struct bklocal_intent_s *intent)
{
  static const char *const stops[] =
  {
    "停止", "停止说话", "停止回答", "取消回答", "结束对话",
    "结束当前对话"
  };

  static const char *const polite[] =
  {
    "请帮我", "帮我", "请"
  };

  size_t begin = 0;
  size_t end;
  size_t size;
  struct bklocal_intent_s parsed;

  if (text == NULL || intent == NULL)
    {
      return 0;
    }

  size = strnlen(text, 256);
  if (size == 0 || size == 256)
    {
      return 0;
    }

  while (begin < size && bklocal_space(text[begin]))
    {
      begin++;
    }

  end = size;
  while (end > begin && bklocal_space(text[end - 1]))
    {
      end--;
    }

  if (end == begin || bklocal_has(text + begin, end - begin, "?") ||
      bklocal_has(text + begin, end - begin, "？") ||
      bklocal_has(text + begin, end - begin, "\"") ||
      bklocal_has(text + begin, end - begin, "“") ||
      bklocal_has(text + begin, end - begin, "”") ||
      bklocal_has(text + begin, end - begin, "不") ||
      bklocal_has(text + begin, end - begin, "别") ||
      bklocal_has(text + begin, end - begin, "没") ||
      bklocal_has(text + begin, end - begin, "如果") ||
      bklocal_has(text + begin, end - begin, "要是"))
    {
      return 0;
    }

  if (text[end - 1] == '!')
    {
      end--;
    }
  else if (end - begin >= strlen("。") &&
           (!memcmp(text + end - strlen("。"), "。", strlen("。")) ||
            !memcmp(text + end - strlen("！"), "！", strlen("！"))))
    {
      end -= strlen("。");
    }

  while (end > begin && bklocal_space(text[end - 1]))
    {
      end--;
    }

  if (end == begin)
    {
      return 0;
    }

  text += begin;
  size = end - begin;
  for (size_t i = 0; i < sizeof(polite) / sizeof(polite[0]); i++)
    {
      size_t prefix_size = strlen(polite[i]);

      if (size > prefix_size && memcmp(text, polite[i], prefix_size) == 0)
        {
          text += prefix_size;
          size -= prefix_size;
          break;
        }
    }

  for (size_t i = 0; i < sizeof(stops) / sizeof(stops[0]); i++)
    {
      if (bklocal_equal(text, size, stops[i]))
        {
          parsed.kind = BKLOCAL_STOP;
          parsed.value = 0;
          parsed.relative = false;
          parsed.expression = NULL;
          *intent = parsed;
          return 1;
        }
    }

  if (bklocal_volume(text, size, &parsed) ||
      bklocal_eyes(text, size, &parsed))
    {
      *intent = parsed;
      return 1;
    }

  return 0;
}
