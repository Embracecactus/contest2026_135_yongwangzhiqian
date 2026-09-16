/****************************************************************************
 * app/bk7258/bk7258_haptic_main.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include "bk7258_haptic_protocol.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  struct bkhaptic_rpc_request_s request = {0};
  struct bkhaptic_rpc_response_s response;
  unsigned long duration;
  char *end;
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      request.command = BKHAPTIC_RPC_STATUS;
    }
  else if (argc == 2 && strcmp(argv[1], "stop") == 0)
    {
      request.command = BKHAPTIC_RPC_STOP;
    }
  else if (argc == 3 && strcmp(argv[1], "pulse") == 0)
    {
      errno = 0;
      duration = strtoul(argv[2], &end, 10);
      if (errno || end == argv[2] || *end || argv[2][0] == '-' ||
          duration < 1 || duration > 32767)
        {
          goto usage;
        }

      request.command = BKHAPTIC_RPC_PULSE;
      request.duration_ms = duration;
    }
  else
    {
      goto usage;
    }

  ret = bkhaptic_rpc_exchange(&request, &response,
                             BKHAPTIC_RPC_REPLY_WAIT_MS);
  if (ret < 0 || response.status < 0)
    {
      fprintf(stderr, "BKHAPTIC FAIL ret=%d\n",
              ret < 0 ? ret : (int)response.status);
      return EXIT_FAILURE;
    }

  if (request.command == BKHAPTIC_RPC_PULSE)
    {
      printf("BKHAPTIC accepted_ms=%lu\n",
             (unsigned long)response.accepted_ms);
    }
  else if (request.command == BKHAPTIC_RPC_STATUS)
    {
      printf("BKHAPTIC ready=%lu\n", (unsigned long)response.ready);
    }
  else
    {
      printf("BKHAPTIC stop accepted\n");
    }

  return EXIT_SUCCESS;

usage:
  fprintf(stderr, "usage: bkhaptic status | pulse <ms:1..32767> | stop\n");
  return EXIT_FAILURE;
}
