/****************************************************************************
 * app/bk7258/bk7258_pc_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_control.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int current(const struct bkpc_control_s *state)
{
  uint8_t client[16];
  uint64_t revision;
  uint32_t capabilities;
  int ret;

  if (state == NULL || !state->open)
    {
      return -ENOTCONN;
    }

  ret = bkpc_grants_snapshot(state->grants, &revision, client,
                             &capabilities);
  if (ret < 0)
    {
      return ret;
    }

  if (revision != state->revision || capabilities != state->capabilities ||
      capabilities == 0 || memcmp(client, state->client, sizeof(client)))
    {
      return -ESTALE;
    }

  return 0;
}

static int execute(void *context, enum bkcontrol_command_e command,
                   uint32_t argument, struct bkcontrol_status_s *status)
{
  struct bkpc_control_s *state = context;
  int ret = current(state);

  if (ret < 0)
    {
      return ret;
    }

  if (command != BKCONTROL_STATUS && command != BKCONTROL_INFO)
    {
      return -EACCES;
    }

  return state->execute(state->context, command, argument, status);
}

static int config(void *context, enum bkcontrol_command_e command,
                  uint32_t kind, uint32_t offset, const uint8_t *record,
                  size_t size, struct bkcontrol_status_s *status)
{
  struct bkpc_control_s *state = context;
  int ret = current(state);
  bool allowed;

  if (ret < 0)
    {
      return ret;
    }

  allowed = (state->capabilities & BKPC_CAP_SCENES) != 0 &&
            (kind == BKCONTROL_CONFIG_FOCUS ||
             kind == BKCONTROL_CONFIG_EXPRESSION_TRIAL);
  allowed |= (state->capabilities & BKPC_CAP_RESOURCES) != 0 &&
             kind == BKCONTROL_CONFIG_EYE_PACK &&
             command == BKCONTROL_CONFIG_READ;
  if (!allowed)
    {
      return -EACCES;
    }

  if (state->config == NULL)
    {
      return -ENOTSUP;
    }

  return state->config(state->context, command, kind, offset,
                       record, size, status);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bkpc_control_close(struct bkpc_control_s *state)
{
  if (state == NULL)
    {
      return;
    }

  if (state->open)
    {
      bkcontrol_pair_close(state->pair);
    }

  mbedtls_platform_zeroize(state, sizeof(*state));
}

int bkpc_control_start(struct bkpc_control_s *state,
                       struct bkcontrol_pair_s *pair,
                       const struct bkpc_grants_s *grants,
                       uint32_t generation, mbedtls_x509_crt *certificate,
                       mbedtls_pk_context *key, uint64_t (*now_ms)(void *),
                       void *clock_context, bkcontrol_execute_t handler,
                       bkcontrol_config_t config_handler, void *context,
                       const struct bkprov_tls_transport_s *transport)
{
  uint8_t principal[32];
  uint8_t client[16];
  uint64_t revision;
  uint32_t capabilities;
  int ret;

  if (state == NULL || pair == NULL || grants == NULL ||
      handler == NULL || transport == NULL)
    {
      return -EINVAL;
    }

  if (state->open || pair->session.open || pair->tls.initialized)
    {
      return -EBUSY;
    }

  ret = bkpc_grants_snapshot(grants, &revision, client, &capabilities);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkpc_grants_key(grants, revision, principal, &capabilities);
  if (ret < 0)
    {
      return ret;
    }

  memset(state, 0, sizeof(*state));
  state->grants = grants;
  state->pair = pair;
  state->execute = handler;
  state->config = config_handler;
  state->context = context;
  state->revision = revision;
  state->capabilities = capabilities;
  memcpy(state->client, client, sizeof(client));
  state->open = true;
  ret = bkcontrol_pair_start_transport(pair, generation, certificate, key,
                                       principal, now_ms, clock_context,
                                       execute, state, transport);
  mbedtls_platform_zeroize(principal, sizeof(principal));
  if (ret == 0)
    {
      ret = bkcontrol_session_set_config_handler(&pair->session, config);
    }

  if (ret < 0)
    {
      bkpc_control_close(state);
    }

  return ret;
}

int bkpc_control_step(struct bkpc_control_s *state)
{
  int ret = current(state);

  if (ret == 0)
    {
      ret = bkcontrol_pair_step(state->pair);
    }

  if (ret < 0)
    {
      bkpc_control_close(state);
    }

  return ret;
}
