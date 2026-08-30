/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/bk7258/
 * bk7258_sdk_timer_selftest_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <arch/chip/bk7258_selftest.h>

int main(int argc, char *argv[])
{
  uint32_t iterations = 64u;
  char *end;
  unsigned long parsed;

  if (argc > 2)
    {
      printf("usage: bktimertest [iterations=64]\n");
      return EXIT_FAILURE;
    }

  if (argc == 2)
    {
      errno = 0;
      parsed = strtoul(argv[1], &end, 0);
      if (errno != 0 || argv[1][0] == '\0' || *end != '\0' ||
          parsed == 0 || parsed > 4096u)
        {
          printf("usage: bktimertest [iterations=64]\n");
          return EXIT_FAILURE;
        }

      iterations = (uint32_t)parsed;
    }

  return bk7258_sdk_timer_selftest(iterations) == OK ?
         EXIT_SUCCESS : EXIT_FAILURE;
}
