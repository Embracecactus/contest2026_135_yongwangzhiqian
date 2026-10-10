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
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 0;
  int ret;

  if (state == NULL || !state->open)
    {
      return -ENOTCONN;
    }

  memset(&view, 0, sizeof(view));
  ret = state->source.snapshot(state->source.context, &binding, &view);
  if (ret > 0)
    {
      ret = -EIO;
    }
  else if (ret == 0 &&
           (binding != state->binding || view.revision != state->revision ||
            view.capabilities != state->capabilities ||
            memcmp(view.client, state->client, sizeof(view.client))))
    {
      ret = -ESTALE;
    }

  mbedtls_platform_zeroize(&view, sizeof(view));
  return ret;
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
  allowed |= (state->capabilities & BKPC_CAP_RESOURCES) != 0 &&
             (kind == BKCONTROL_CONFIG_RESOURCE_JOB ||
              kind == BKCONTROL_CONFIG_DEFAULT_SELECTION ||
              kind == BKCONTROL_CONFIG_RESOURCE_CATALOG);
  allowed |= (state->capabilities & BKPC_CAP_TASKS) != 0 &&
             kind == BKCONTROL_CONFIG_PC_TASK;
  allowed |= (state->capabilities & BKPC_CAP_CAMERA) != 0 &&
             (kind == BKCONTROL_CONFIG_CAMERA ||
              (kind == BKCONTROL_CONFIG_CAMERA_FRAME &&
               command == BKCONTROL_CONFIG_READ));
#ifdef CONFIG_BK7258_ENGINEERING_TEST
  allowed |= (state->capabilities & BKPC_CAP_DIAGNOSTICS) != 0 &&
             (kind == BKCONTROL_CONFIG_ENGINEERING_TEST ||
              kind == BKCONTROL_CONFIG_ENGINEERING_AUDIO);
#endif
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
  void (*closed)(void *context);
  void *closed_context;

  if (state == NULL)
    {
      return;
    }

  closed = state->open ? state->closed : NULL;
  closed_context = state->closed_context;
  if (state->open)
    {
      bkcontrol_pair_close(state->pair);
    }

  mbedtls_platform_zeroize(state, sizeof(*state));
  if (closed != NULL)
    {
      closed(closed_context);
    }
}

int bkpc_control_set_close_handler(struct bkpc_control_s *state,
                                   void (*closed)(void *context),
                                   void *context)
{
  if (state == NULL || closed == NULL)
    {
      return -EINVAL;
    }

  if (!state->open)
    {
      return -ENOTCONN;
    }

  if (state->closed != NULL)
    {
      return state->closed == closed && state->closed_context == context ?
             0 : -EBUSY;
    }

  state->closed = closed;
  state->closed_context = context;
  return 0;
}

int bkpc_control_start(struct bkpc_control_s *state,
                       struct bkcontrol_pair_s *pair,
                       const struct bkpc_source_s *source,
                       uint32_t generation, mbedtls_x509_crt *certificate,
                       mbedtls_pk_context *key, uint64_t (*now_ms)(void *),
                       void *clock_context, bkcontrol_execute_t handler,
                       bkcontrol_config_t config_handler, void *context,
                       const struct bkprov_tls_transport_s *transport)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 0;
  uint8_t key_bits = 0;
  uint8_t client_bits = 0;
  int ret;

  if (state == NULL || pair == NULL || source == NULL ||
      source->snapshot == NULL || handler == NULL || transport == NULL)
    {
      return -EINVAL;
    }

  if (state->open || pair->session.open || pair->tls.initialized)
    {
      return -EBUSY;
    }

  memset(&view, 0, sizeof(view));
  ret = source->snapshot(source->context, &binding, &view);
  if (ret == 0)
    {
      for (size_t i = 0; i < sizeof(view.key); i++)
        {
          key_bits |= view.key[i];
        }

      for (size_t i = 0; i < sizeof(view.client); i++)
        {
          client_bits |= view.client[i];
        }

      if (binding == 0 || view.revision == 0 || key_bits == 0 ||
          client_bits == 0 || view.capabilities == 0 ||
          (view.capabilities & ~BKPC_CAP_ALL))
        {
          ret = view.capabilities == 0 ? -EACCES : -ENOKEY;
        }
    }

  if (ret != 0)
    {
      mbedtls_platform_zeroize(&view, sizeof(view));
      return ret < 0 ? ret : -EIO;
    }

  memset(state, 0, sizeof(*state));
  state->source = *source;
  state->binding = binding;
  state->pair = pair;
  state->execute = handler;
  state->config = config_handler;
  state->context = context;
  state->revision = view.revision;
  state->capabilities = view.capabilities;
  memcpy(state->client, view.client, sizeof(view.client));
  state->open = true;
  ret = bkcontrol_pair_start_transport(pair, generation, certificate, key,
                                       view.key, now_ms, clock_context,
                                       execute, state, transport);
  mbedtls_platform_zeroize(&view, sizeof(view));
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
