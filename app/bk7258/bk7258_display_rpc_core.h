/****************************************************************************
 * app/bk7258/bk7258_display_rpc_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_RPC_CORE_H
#define __APP_BK7258_BK7258_DISPLAY_RPC_CORE_H

#include <stdbool.h>

#include "bk7258_display_protocol.h"

bool bkdisplay_rpc_request_valid(
  const struct bkdisplay_rpc_request_s *request);
void bkdisplay_rpc_make_response(
  struct bkdisplay_rpc_response_s *response,
  const struct bkdisplay_rpc_request_s *request, int status);
int bkdisplay_rpc_handle_request(
  const struct bkdisplay_rpc_request_s *request,
  struct bkdisplay_rpc_response_s *response);

#endif /* __APP_BK7258_BK7258_DISPLAY_RPC_CORE_H */
