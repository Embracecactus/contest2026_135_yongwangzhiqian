/****************************************************************************
 * app/bk7258/bk7258_reset_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Short reset alias for the standard NSH reboot command.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/boardctl.h>

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  puts("reset: rebooting BK7258");
  fflush(stdout);
  (void)boardctl(BOARDIOC_RESET, 0);
  fputs("reset: BOARDIOC_RESET returned unexpectedly\n", stderr);
  return EXIT_FAILURE;
}
