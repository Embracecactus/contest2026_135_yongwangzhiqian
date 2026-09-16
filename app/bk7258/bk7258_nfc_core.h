/****************************************************************************
 * app/bk7258/bk7258_nfc_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_BK7258_NFC_CORE_H
#define __APP_BK7258_BK7258_NFC_CORE_H
#include <stdbool.h>
#include <stddef.h>

#include "bk7258_nfc_protocol.h"

struct bknfc_source_ops_s
{
  int (*open)(void *context);
  int (*read)(void *context, void *buffer, size_t length);
  int (*close)(void *context);
  int (*hce)(void *context);
};

bool bknfc_rpc_request_valid(const struct bknfc_rpc_request_s *request);
bool bknfc_rpc_response_valid(const struct bknfc_rpc_response_s *response);
void bknfc_rpc_make_response(struct bknfc_rpc_response_s *response,
                             const struct bknfc_rpc_request_s *request,
                             int rpc_status);
int bknfc_rpc_handle_request(const struct bknfc_rpc_request_s *request,
                             struct bknfc_rpc_response_s *response,
                             const struct bknfc_source_ops_s *ops,
                             void *context);
#endif /* __APP_BK7258_BK7258_NFC_CORE_H */
