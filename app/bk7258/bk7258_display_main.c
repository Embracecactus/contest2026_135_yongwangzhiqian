/****************************************************************************
 * app/bk7258/bk7258_display_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible operator command for the AP-owned BKDisplay service.
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_display_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bkdisplay_usage(void)
{
  fprintf(stderr,
          "usage: bkdisplay status\n"
          "       bkdisplay mood <expression>\n"
          "       bkdisplay calibrate\n");
}

static const char *bkdisplay_state_name(uint32_t state)
{
  switch (state)
    {
      case BKDISPLAY_RPC_STATE_STOPPED:
        return "STOPPED";
      case BKDISPLAY_RPC_STATE_WAITING_DEVICES:
        return "WAITING_DEVICES";
      case BKDISPLAY_RPC_STATE_WAITING_ASSET:
        return "WAITING_ASSET";
      case BKDISPLAY_RPC_STATE_READY:
        return "READY";
      case BKDISPLAY_RPC_STATE_ERROR:
        return "ERROR";
      default:
        return "UNKNOWN";
    }
}

static int bkdisplay_copy_expression(char *target, size_t target_size,
                                     const char *source)
{
  size_t length = strlen(source);

  if (length == 0 || length >= target_size)
    {
      return -ENAMETOOLONG;
    }

  memcpy(target, source, length + 1u);
  return 0;
}

static int bkdisplay_request(uint16_t command, const char *expression,
                             struct bkdisplay_rpc_response_s *response)
{
  struct bkdisplay_rpc_request_s request;
  int ret;

  memset(&request, 0, sizeof(request));
  memset(response, 0, sizeof(*response));
  request.command = command;
  if (expression != NULL)
    {
      ret = bkdisplay_copy_expression(request.expression,
                                      sizeof(request.expression),
                                      expression);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkdisplay_rpc_exchange(&request, response,
                               BKDISPLAY_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      fprintf(stderr, "BKDISPLAY RPC FAIL ret=%d\n", ret);
      return ret;
    }

  return response->status;
}

static int bkdisplay_status(void)
{
  struct bkdisplay_rpc_response_s response;
  const char *expression;
  const char *pack_id;
  int ret;

  ret = bkdisplay_request(BKDISPLAY_RPC_STATUS, NULL, &response);
  if (ret < 0)
    {
      fprintf(stderr, "BKDISPLAY STATUS FAIL ret=%d\n", ret);
      return ret;
    }

  expression = response.expression[0] == '\0' ? "-" :
               response.expression;
  pack_id = response.pack_id[0] == '\0' ? "-" : response.pack_id;
  printf("BKDISPLAY STATUS service=%s state=%s(%lu) last_error=%ld "
         "screens=%lu expression=%s pack=%s revision=%lu "
         "mapping=%s sequence=%lu storage=/dev/mmcsd0\n",
         (response.flags & BKDISPLAY_STATUS_SERVICE_READY) != 0 ?
         "ready" : "not-ready",
         bkdisplay_state_name(response.state),
         (unsigned long)response.state, (long)response.last_error,
         (unsigned long)response.screen_count, expression, pack_id,
         (unsigned long)response.pack_revision,
         (response.flags & BKDISPLAY_STATUS_MAPPING_VERIFIED) != 0 ?
         "verified" : "unverified",
         (unsigned long)response.render_sequence);
  return 0;
}

static int bkdisplay_mood(const char *expression)
{
  struct bkdisplay_rpc_response_s response;
  int ret;

  ret = bkdisplay_request(BKDISPLAY_RPC_SET_EXPRESSION, expression,
                          &response);
  if (ret < 0)
    {
      fprintf(stderr,
              "BKDISPLAY MOOD FAIL ret=%d requested=%s state=%s(%lu) "
              "last_error=%ld\n",
              ret, expression, bkdisplay_state_name(response.state),
              (unsigned long)response.state, (long)response.last_error);
      return ret;
    }

  printf("BKDISPLAY MOOD PASS requested=%s applied=%s sequence=%lu "
         "screens=%lu mapping=%s\n",
         expression, response.expression,
         (unsigned long)response.render_sequence,
         (unsigned long)response.screen_count,
         (response.flags & BKDISPLAY_STATUS_MAPPING_VERIFIED) != 0 ?
         "verified" : "unverified");
  return 0;
}

static int bkdisplay_calibrate(void)
{
  struct bkdisplay_rpc_response_s response;
  int ret;

  ret = bkdisplay_request(BKDISPLAY_RPC_SHOW_MAPPING_TEST, NULL, &response);
  if (ret < 0)
    {
      fprintf(stderr,
              "BKDISPLAY CALIBRATE FAIL ret=%d state=%s(%lu) "
              "last_error=%ld\n",
              ret, bkdisplay_state_name(response.state),
              (unsigned long)response.state, (long)response.last_error);
      return ret;
    }

  printf("BKDISPLAY CALIBRATE PASS fb0=cyan fb1=magenta "
         "mapping=unverified sequence=%lu "
         "next=report-physical-left-color\n",
         (unsigned long)response.render_sequence);
  return 0;
}

int main(int argc, char **argv)
{
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = bkdisplay_status();
    }
  else if (argc == 3 && strcmp(argv[1], "mood") == 0)
    {
      ret = bkdisplay_mood(argv[2]);
    }
  else if (argc == 2 && strcmp(argv[1], "calibrate") == 0)
    {
      ret = bkdisplay_calibrate();
    }
  else
    {
      bkdisplay_usage();
      return EXIT_FAILURE;
    }

  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
