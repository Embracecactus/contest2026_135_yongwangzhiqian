/****************************************************************************
 * Contest 2026 team 135 BK7258 P29 GPIO edge IRQ NSH wrapper
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>

#include <arch/board/board.h>
#include <arch/chip/bk7258_gpio.h>

int main(int argc, char *argv[])
{
  (void)argv;

  if (argc != 1)
    {
      printf("Usage: bkgpioirq\n");
      return EXIT_FAILURE;
    }

  return bk7258_gpio_irq_test(&g_bk7258_board_gpio_config) == 0 ?
         EXIT_SUCCESS : EXIT_FAILURE;
}
