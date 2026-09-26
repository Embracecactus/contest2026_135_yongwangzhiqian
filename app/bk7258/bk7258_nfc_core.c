/****************************************************************************
 * app/bk7258/bk7258_nfc_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable NFC policy. V1 presence; V2 device-internal card sample.
 ****************************************************************************/
#include "bk7258_nfc_core.h"

#include <errno.h>
#include <string.h>

bool bknfc_card_valid(const struct bknfc_card_s *card)
{
  unsigned int i;

  if (card == NULL || (card->sak & 4) != 0 ||
      (card->size != 4 && card->size != 7 && card->size != 10))
    {
      return false;
    }

  for (i = card->size; i < sizeof(card->uid); i++)
    {
      if (card->uid[i] != 0)
        {
          return false;
        }
    }

  return true;
}

bool bknfc_rpc_request_valid(const struct bknfc_rpc_request_s *request)
{
  return request != NULL && request->magic == BKNFC_RPC_MAGIC &&
         ((request->version == BKNFC_RPC_VERSION &&
           (request->command == BKNFC_RPC_SCAN ||
            request->command == BKNFC_RPC_HCE)) ||
          (request->version == BKNFC_CARD_VERSION &&
           request->command == BKNFC_RPC_CARD)) && request->session != 0 &&
         request->sequence != 0 && request->reserved[0] == 0 &&
         request->reserved[1] == 0;
}

bool bknfc_rpc_response_valid(const struct bknfc_rpc_response_s *response)
{
  if (response == NULL || response->magic != BKNFC_RPC_MAGIC ||
      (response->version != BKNFC_RPC_VERSION &&
       response->version != BKNFC_CARD_VERSION) ||
      response->command != BKNFC_RPC_RESPONSE || response->session == 0 ||
      response->sequence == 0 ||
      response->rpc_status > 0 || response->operation_status > 0 ||
      response->present > 1)
    {
      return false;
    }

  if (response->version == BKNFC_CARD_VERSION && response->present == 1)
    {
      return response->rpc_status == 0 && response->operation_status == 0 &&
             bknfc_card_valid(&response->card);
    }

  if (response->reserved[0] != 0 || response->reserved[1] != 0 ||
      response->reserved[2] != 0)
    {
      return false;
    }

  if (response->rpc_status < 0)
    {
      return response->operation_status < 0 && response->present == 0;
    }

  return response->operation_status == 0 ||
         (response->operation_status < 0 && response->present == 0);
}

void bknfc_rpc_make_response(struct bknfc_rpc_response_s *response,
                             const struct bknfc_rpc_request_s *request,
                             int rpc_status)
{
  if (response == NULL)
    {
      return;
    }

  memset(response, 0, sizeof(*response));
  response->magic = BKNFC_RPC_MAGIC;
  response->version = request != NULL &&
                      request->version == BKNFC_CARD_VERSION ?
                      BKNFC_CARD_VERSION : BKNFC_RPC_VERSION;
  response->command = BKNFC_RPC_RESPONSE;
  response->rpc_status = rpc_status;
  response->operation_status = rpc_status < 0 ? rpc_status : -ENODATA;
  if (request != NULL)
    {
      response->session = request->session;
      response->sequence = request->sequence;
    }
}

int bknfc_rpc_handle_request(const struct bknfc_rpc_request_s *request,
                             struct bknfc_rpc_response_s *response,
                             const struct bknfc_source_ops_s *ops,
                             void *context)
{
  unsigned char scratch = 0;
  struct bknfc_card_s card = {0};
  int result;
  int close_result;

  if (response == NULL)
    {
      return -EINVAL;
    }

  if (!bknfc_rpc_request_valid(request) || ops == NULL)
    {
      bknfc_rpc_make_response(response, request, -EINVAL);
      return -EINVAL;
    }

  bknfc_rpc_make_response(response, request, 0);
  if (ops->open == NULL || ops->close == NULL)
    {
      response->operation_status = -EINVAL;
      return -EINVAL;
    }

  result = ops->open(context);
  if (result < 0)
    {
      response->operation_status = result;
      return result;
    }

  if (request->command == BKNFC_RPC_CARD)
    {
      result = ops->card == NULL ? -ENOSYS : ops->card(context, &card);
    }
  else if (request->command == BKNFC_RPC_HCE)
    {
      result = ops->hce == NULL ? -ENOSYS : ops->hce(context);
    }
  else
    {
      result = ops->read == NULL ? -ENOSYS :
               ops->read(context, &scratch, sizeof(scratch));
    }
  close_result = ops->close(context);
  scratch = 0; /* UID-derived data is never retained beyond this scope. */
  if (close_result < 0)
    {
      response->operation_status = close_result;
      response->present = 0;
      return close_result;
    }

  if (request->command == BKNFC_RPC_CARD)
    {
      /* 关闭成功后才发布卡片；读取错误不能作为移开后的重入证据。 */

      if (result == 0 && bknfc_card_valid(&card))
        {
          response->card = card;
          response->present = 1;
        }
      else if (result >= 0)
        {
          result = -EPROTO;
        }

      response->operation_status = result;
      return result;
    }

  if (result == -EAGAIN)
    {
      response->operation_status = 0;
      response->present = 0;
      return 0;
    }

  /* Scan reads one byte; zero means no completed sample, not presence.
   * HCE is a transaction status and succeeds only with zero. Keep these
   * contracts separate so an incomplete selection cannot trigger a scene.
   */

  if ((request->command == BKNFC_RPC_SCAN &&
       result == (int)sizeof(scratch)) ||
      (request->command == BKNFC_RPC_HCE && result == 0))
    {
      response->operation_status = 0;
      response->present = 1;
      return 0;
    }

  if (result >= 0)
    {
      result = result == 0 ? -ENODATA : -EPROTO;
    }

  response->operation_status = result;
  return result;
}
