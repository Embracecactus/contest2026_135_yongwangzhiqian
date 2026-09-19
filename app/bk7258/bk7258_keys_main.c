/****************************************************************************
 * app/bk7258/bk7258_keys_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <syslog.h>

#include "bk7258_voice_button.h"

int main(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;
  if (bkvoice_button_running()) return 0;
  ret = bkvoice_button_start("/dev/buttons", true);
  syslog(ret < 0 ? LOG_ERR : LOG_INFO,
         "BKKEYS source=/dev/buttons started=%d result=%d\n", ret == 0, ret);
  return ret < 0 ? 1 : 0;
}
