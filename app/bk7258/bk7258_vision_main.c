/****************************************************************************
 * app/bk7258/bk7258_vision_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_vision_protocol.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
  struct bkvision_rpc_request_s request;
  struct bkvision_rpc_response_s response;
  unsigned long seconds = 0;
  unsigned int timeout = BKVISION_RPC_REPLY_WAIT_MS;
  char *end;
  int ret;

  memset(&request, 0, sizeof(request));
  if (argc == 4 && strcmp(argv[1], "check-record") == 0)
    {
      uint32_t target[2];
      for (unsigned int i = 0; i < 2; i++)
        {
          errno = 0;
          unsigned long value = strtoul(argv[i + 2], &end, 16);
          if (errno != 0 || strlen(argv[i + 2]) != 8 ||
              end != argv[i + 2] + 8 || value == 0 || value > UINT32_MAX ||
              strspn(argv[i + 2], "0123456789abcdefABCDEF") != 8)
            {
              fprintf(stderr, "record identity must be two nonzero 8-digit hex values\n");
              return EXIT_FAILURE;
            }
          target[i] = value;
        }
      request.command = BKVISION_RPC_CHECK_RECORD;
      timeout = 180000;
      request.duration_ms = target[0];
      request.reserved = target[1];
    }
  else if (argc == 2 && strcmp(argv[1], "snapshot") == 0)
    {
      request.command = BKVISION_RPC_SNAPSHOT;
    }
  else if (argc == 3 && strcmp(argv[1], "record") == 0)
    {
      errno = 0;
      seconds = strtoul(argv[2], &end, 10);
      if (errno != 0 || end == argv[2] || *end != '\0' ||
          seconds < 1 || seconds > BKVISION_RECORD_MAX_SECONDS)
        {
          fprintf(stderr, "record duration must be 1..60 seconds\n");
          return EXIT_FAILURE;
        }
      request.command = BKVISION_RPC_RECORD;
      request.duration_ms = seconds * 1000;
      timeout += request.duration_ms;
    }
  else
    {
      fprintf(stderr, "usage: bkvision snapshot | record <seconds:1..60> | check-record <session:8hex> <sequence:8hex>\n");
      return EXIT_FAILURE;
    }

  memset(&response, 0, sizeof(response));
  ret = bkvision_rpc_exchange(&request, &response, timeout);
  if (ret < 0 || response.rpc_status < 0 || response.operation_status < 0)
    {
      fprintf(stderr, "BKVISION %s FAIL ret=%d rpc=%ld operation=%ld\n",
              request.command == BKVISION_RPC_CHECK_RECORD ? "CHECK" :
              seconds ? "RECORD" : "SNAPSHOT", ret,
              (long)response.rpc_status, (long)response.operation_status);
      return EXIT_FAILURE;
    }

  if (request.command == BKVISION_RPC_CHECK_RECORD)
    {
      printf("BKVISION CHECK bytes=%lu header_frames=%lu fnv1a=%08lx "
             "width=%lu height=%lu file=/recordings/"
             BKVISION_RECORD_NAME_FORMAT " mount=read-only\n",
             (unsigned long)response.bytes_used,
             (unsigned long)response.reserved[0],
             (unsigned long)response.reserved[1],
             (unsigned long)response.width, (unsigned long)response.height,
             (unsigned long)request.duration_ms, (unsigned long)request.reserved);
      return EXIT_SUCCESS;
    }

  if (seconds)
    {
      printf("BKVISION RECORD frames=%lu elapsed_ms=%lu bytes=%lu "
             "width=%lu height=%lu file=/recordings/"
             BKVISION_RECORD_NAME_FORMAT " audio=none\n",
             (unsigned long)response.reserved[0],
             (unsigned long)response.reserved[1],
             (unsigned long)response.bytes_used,
             (unsigned long)response.width, (unsigned long)response.height,
             (unsigned long)response.session_id,
             (unsigned long)response.sequence);
      return EXIT_SUCCESS;
    }

  printf("BKVISION SNAPSHOT width=%lu height=%lu fourcc=%08lx bytes_used=%lu "
         "capture_sequence=%lu soi=%s eoi=%s v4l2_error=%s "
         "privacy=metadata-only storage=disabled\n",
         (unsigned long)response.width, (unsigned long)response.height,
         (unsigned long)response.pixel_format, (unsigned long)response.bytes_used,
         (unsigned long)response.capture_sequence,
         (response.flags & BKVISION_FLAG_JPEG_SOI) != 0 ? "yes" : "no",
         (response.flags & BKVISION_FLAG_JPEG_EOI) != 0 ? "yes" : "no",
         (response.flags & BKVISION_FLAG_V4L2_ERROR) != 0 ? "yes" : "no");
  return EXIT_SUCCESS;
}
