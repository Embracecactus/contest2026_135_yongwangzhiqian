/****************************************************************************
 * Contest 2026 team 135 BK7258 GPIO foundation C0 NSH wrapper
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <arch/chip/bk7258_gpio.h>

int main(int argc, char *argv[])
{
  (void)argv;

  if (argc != 1)
    {
      printf("Usage: bkgpioc0\n");
      return EXIT_FAILURE;
    }

  return bk7258_gpio_foundation_test() == 0 ?
         EXIT_SUCCESS : EXIT_FAILURE;
}
