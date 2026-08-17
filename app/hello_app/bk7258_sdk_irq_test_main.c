/****************************************************************************
 * Contest 2026 team 135 - BK7258 SDK IRQ bridge timer test command
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <arch/chip/bk7258_selftest.h>

int main(int argc, char *argv[])
{
  (void)argv;

  if (argc != 1)
    {
      printf("Usage: bkirqtest\n");
      return EXIT_FAILURE;
    }

  return bk7258_sdk_irq_timer_test() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
