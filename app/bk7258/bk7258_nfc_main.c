/****************************************************************************
 * app/bk7258/bk7258_nfc_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible NFC-presence command.  It never prints or receives a UID.
 ****************************************************************************/
#include <nuttx/config.h>

#include "bk7258_nfc_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  struct bknfc_rpc_request_s request;
  struct bknfc_rpc_response_s response;
  int ret;

  if (argc != 2 || (strcmp(argv[1], "scan") != 0 &&
                    strcmp(argv[1], "hce") != 0))
    {
      fprintf(stderr, "usage: bknfc scan|hce\n");
      return EXIT_FAILURE;
    }

  memset(&request, 0, sizeof(request));
  memset(&response, 0, sizeof(response));
  request.command = strcmp(argv[1], "hce") == 0 ?
                    BKNFC_RPC_HCE : BKNFC_RPC_SCAN;
  ret = bknfc_rpc_exchange(&request, &response,
                           request.command == BKNFC_RPC_HCE ? 20000 :
                           BKNFC_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "BKNFC RPC FAIL ret=%d\n", ret);
      return EXIT_FAILURE;
    }

  if (response.rpc_status < 0 || response.operation_status < 0)
    {
      fprintf(stderr, "BKNFC %s FAIL rpc=%ld operation=%ld\n",
              request.command == BKNFC_RPC_HCE ? "HCE" : "SCAN",
              (long)response.rpc_status, (long)response.operation_status);
      return EXIT_FAILURE;
    }

  printf("BKNFC %s present=%s privacy=uid-not-exported\n",
         request.command == BKNFC_RPC_HCE ? "HCE" : "SCAN",
         response.present ? "yes" : "no");
  return EXIT_SUCCESS;
}
