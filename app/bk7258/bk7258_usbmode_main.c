/****************************************************************************
 * app/bk7258/bk7258_usbmode_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Operator command for dynamic BK7258 USB0 CDC/MSC selection.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arch/chip/bk7258_ota_rpmsg.h>

#define USBMODE_TIMEOUT_MS 10000u

static const char *usbmode_name(enum bk7258_usbmode_e mode)
{
  switch (mode)
    {
      case BK7258_USBMODE_CDC:
        return "cdc";
      case BK7258_USBMODE_MSC:
        return "msc";
      default:
        return "none";
    }
}

static void usbmode_usage(void)
{
  fprintf(stderr, "usage: usbmode status | cdc | msc\n");
}

int main(int argc, char **argv)
{
  enum bk7258_usbmode_e mode;
  enum bk7258_usbmode_e requested;
  int ret;

  if (argc != 2)
    {
      usbmode_usage();
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "status") == 0)
    {
      ret = bk7258_ota_rpmsg_usbmode_get(&mode, USBMODE_TIMEOUT_MS);
      if (ret < 0)
        {
          fprintf(stderr, "usbmode: status failed: %d\n", ret);
          return EXIT_FAILURE;
        }

      printf("mode=%s blockdev=/dev/mmcsd0\n", usbmode_name(mode));
      return EXIT_SUCCESS;
    }
  else if (strcmp(argv[1], "cdc") == 0)
    {
      mode = BK7258_USBMODE_CDC;
      fprintf(stderr,
              "usbmode: safely eject the host MSC disk before switching to CDC\n");
    }
  else if (strcmp(argv[1], "msc") == 0)
    {
      mode = BK7258_USBMODE_MSC;
      fprintf(stderr,
              "usbmode: exporting /dev/mmcsd0; keep it unmounted on AP while MSC is active\n");
    }
  else
    {
      usbmode_usage();
      return EXIT_FAILURE;
    }

  requested = mode;
  ret = bk7258_ota_rpmsg_usbmode_set(requested, &mode,
                                     USBMODE_TIMEOUT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "usbmode: switch to %s failed: %d\n",
              usbmode_name(requested), ret);
      if (ret == -EBUSY && requested == BK7258_USBMODE_MSC)
        {
          fprintf(stderr, "usbmode: close /dev/ttyGS0 and retry\n");
        }

      return EXIT_FAILURE;
    }

  printf("mode=%s\n", usbmode_name(mode));
  return EXIT_SUCCESS;
}
