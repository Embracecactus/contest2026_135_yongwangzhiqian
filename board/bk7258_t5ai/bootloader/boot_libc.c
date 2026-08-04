/*
 * boot_libc.c - minimal freestanding libc subset for shared portable cores.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t len)
{
  unsigned char *output = destination;
  const unsigned char *input = source;

  while (len-- != 0)
    {
      *output++ = *input++;
    }

  return destination;
}

void *memset(void *destination, int value, size_t len)
{
  unsigned char *output = destination;

  while (len-- != 0)
    {
      *output++ = (unsigned char)value;
    }

  return destination;
}

int memcmp(const void *left, const void *right, size_t len)
{
  const unsigned char *a = left;
  const unsigned char *b = right;

  while (len-- != 0)
    {
      if (*a != *b)
        {
          return *a < *b ? -1 : 1;
        }

      a++;
      b++;
    }

  return 0;
}

int strcmp(const char *left, const char *right)
{
  while (*left != '\0' && *left == *right)
    {
      left++;
      right++;
    }

  return (unsigned char)*left - (unsigned char)*right;
}
