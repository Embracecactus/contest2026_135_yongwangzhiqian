/****************************************************************************
 * app/bk7258/bk7258_display_rpc_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable AP display request validation and dispatch.
 ****************************************************************************/

#include "bk7258_display_rpc_core.h"
#include "bk7258_display_service.h"

#include <errno.h>
#include <string.h>

_Static_assert((int)BKDISPLAY_SERVICE_STOPPED ==
               (int)BKDISPLAY_RPC_STATE_STOPPED,
               "display stopped-state ABI drift");
_Static_assert((int)BKDISPLAY_SERVICE_WAITING_DEVICES ==
               (int)BKDISPLAY_RPC_STATE_WAITING_DEVICES,
               "display device-wait state ABI drift");
_Static_assert((int)BKDISPLAY_SERVICE_WAITING_ASSET ==
               (int)BKDISPLAY_RPC_STATE_WAITING_ASSET,
               "display asset-wait state ABI drift");
_Static_assert((int)BKDISPLAY_SERVICE_READY ==
               (int)BKDISPLAY_RPC_STATE_READY,
               "display ready-state ABI drift");
_Static_assert((int)BKDISPLAY_SERVICE_ERROR ==
               (int)BKDISPLAY_RPC_STATE_ERROR,
               "display error-state ABI drift");

static void bkdisplay_rpc_copy_string(char *target, size_t target_size,
                                      const char *source)
{
  size_t length = strnlen(source, target_size - 1u);

  memcpy(target, source, length);
  target[length] = '\0';
}

bool bkdisplay_rpc_request_valid(
  const struct bkdisplay_rpc_request_s *request)
{
  bool terminated;

  if (request == NULL || request->magic != BKDISPLAY_RPC_MAGIC ||
      request->version != BKDISPLAY_RPC_VERSION || request->session == 0 ||
      request->sequence == 0 || request->reserved != 0)
    {
      return false;
    }

  terminated = memchr(request->expression, '\0',
                      sizeof(request->expression)) != NULL;
  if (!terminated)
    {
      return false;
    }

  if (request->command == BKDISPLAY_RPC_STATUS)
    {
      return request->expression[0] == '\0';
    }

  if (request->command == BKDISPLAY_RPC_SHOW_MAPPING_TEST)
    {
      return request->expression[0] == '\0';
    }

  return request->command == BKDISPLAY_RPC_SET_EXPRESSION &&
         request->expression[0] != '\0';
}

void bkdisplay_rpc_make_response(
  struct bkdisplay_rpc_response_s *response,
  const struct bkdisplay_rpc_request_s *request, int status)
{
  memset(response, 0, sizeof(*response));
  response->magic = BKDISPLAY_RPC_MAGIC;
  response->version = BKDISPLAY_RPC_VERSION;
  response->command = BKDISPLAY_RPC_RESPONSE;
  response->session = request->session;
  response->sequence = request->sequence;
  response->status = status;
}

static void bkdisplay_rpc_copy_status(
  struct bkdisplay_rpc_response_s *response,
  const struct bkdisplay_service_status_s *status)
{
  response->state = (uint32_t)status->state;
  response->last_error = status->last_error;
  response->screen_count = status->screen_count;
  response->render_sequence = status->render_sequence;
  response->pack_revision = status->pack_revision;
  if (status->state == BKDISPLAY_SERVICE_READY)
    {
      response->flags |= BKDISPLAY_STATUS_SERVICE_READY;
    }

  if (status->physical_mapping_verified)
    {
      response->flags |= BKDISPLAY_STATUS_MAPPING_VERIFIED;
    }

  if (status->pack_id[0] != '\0')
    {
      response->flags |= BKDISPLAY_STATUS_PACK_SELECTED;
    }

  bkdisplay_rpc_copy_string(response->expression,
                            sizeof(response->expression),
                            status->expression);
  bkdisplay_rpc_copy_string(response->pack_id, sizeof(response->pack_id),
                            status->pack_id);
}

int bkdisplay_rpc_handle_request(
  const struct bkdisplay_rpc_request_s *request,
  struct bkdisplay_rpc_response_s *response)
{
  struct bkdisplay_service_status_s display_status;
  int status_ret;
  int ret;

  if (request == NULL || response == NULL)
    {
      return -EINVAL;
    }

  bkdisplay_rpc_make_response(response, request, 0);
  if (!bkdisplay_rpc_request_valid(request))
    {
      response->status = -EINVAL;
      return -EINVAL;
    }

  if (request->command == BKDISPLAY_RPC_SET_EXPRESSION)
    {
      ret = bk7258_display_set_expression(request->expression);
    }
  else if (request->command == BKDISPLAY_RPC_SHOW_MAPPING_TEST)
    {
      ret = bk7258_display_show_mapping_test();
    }
  else
    {
      ret = 0;
    }

  memset(&display_status, 0, sizeof(display_status));
  status_ret = bk7258_display_get_status(&display_status);
  if (status_ret >= 0)
    {
      bkdisplay_rpc_copy_status(response, &display_status);
    }
  else if (ret >= 0)
    {
      ret = status_ret;
    }

  response->status = ret;
  return ret;
}
