/****************************************************************************
 * app/bk7258/bk7258_motion_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible one-shot accelerometer command.
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_motion_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  struct bkmotion_rpc_request_s request;
  struct bkmotion_rpc_response_s response;
  int ret;

  if (argc != 2 || strcmp(argv[1], "sample") != 0)
    {
      fprintf(stderr, "usage: bkmotion sample\n");
      return EXIT_FAILURE;
    }

  memset(&request, 0, sizeof(request));
  memset(&response, 0, sizeof(response));
  request.command = BKMOTION_RPC_SAMPLE;
  ret = bkmotion_rpc_exchange(&request, &response, BKMOTION_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "BKMOTION RPC FAIL ret=%d\n", ret);
      return EXIT_FAILURE;
    }

  if (response.rpc_status < 0 || response.operation_status < 0 ||
      response.flags != BKMOTION_FLAG_SAMPLE_VALID ||
      response.timestamp_us == 0)
    {
      fprintf(stderr, "BKMOTION SAMPLE FAIL rpc=%ld operation=%ld\n",
              (long)response.rpc_status, (long)response.operation_status);
      return EXIT_FAILURE;
    }

  printf("BKMOTION SAMPLE PASS timestamp_us=%llu x_mms2=%ld y_mms2=%ld "
         "z_mms2=%ld status=%ld unit=mm_s2 privacy=telemetry-only\n",
         (unsigned long long)response.timestamp_us,
         (long)response.x_mms2, (long)response.y_mms2,
         (long)response.z_mms2, (long)response.sensor_status);
  return EXIT_SUCCESS;
}
