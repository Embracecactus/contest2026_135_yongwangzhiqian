/****************************************************************************
 * app/bk7258/bk7258_identity_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <arch/chip/bk7258_identity.h>

int main(int argc, FAR char *argv[])
{
  uint8_t identity[BK7258_IDENTITY_BYTES];
  unsigned int i;
  int ret;

  if (argc != 1)
    {
      fprintf(stderr, "usage: %s\n", argv[0]);
      return 1;
    }

  memset(identity, 0, sizeof(identity));
  ret = bk7258_identity_read(identity);
  if (ret < 0)
    {
      fprintf(stderr, "BKIDENTITY FAIL status=%d\n", ret);
      explicit_bzero(identity, sizeof(identity));
      return 1;
    }

  printf("BKIDENTITY PASS sha256=");
  for (i = 0; i < sizeof(identity); i++)
    {
      printf("%02x", identity[i]);
    }

  printf("\n");
  explicit_bzero(identity, sizeof(identity));
  return 0;
}
