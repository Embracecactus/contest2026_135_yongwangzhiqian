/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_client.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "llm/llm_proxy.h"
#include "core/agent_turn.h"
#include <string.h>
#include <mbedtls/platform_util.h>
#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include <cJSON.h>
#endif
#define BKCLOUD_REQUEST_CAPACITY 65536u

void bkcloud_history_clear(struct bkcloud_history_s *history)
{
  if (history != NULL) mbedtls_platform_zeroize(history, sizeof(*history));
}

int bkcloud_history_commit(struct bkcloud_history_s *history,
                            const char *user, const char *assistant)
{
  if (history == NULL || history->count > BKCLOUD_HISTORY_TURNS ||
      !bkcloud_audio_valid_text(user) || !bkcloud_audio_valid_text(assistant)) return -EINVAL;
  /* Inputs must not alias history: the owner passes pending-turn storage. */
  if (history->count == BKCLOUD_HISTORY_TURNS)
    {
      memmove(history->turns, history->turns + 1,
              sizeof(history->turns[0]) * (BKCLOUD_HISTORY_TURNS - 1));
      history->count--;
    }
  size_t index = history->count++;
  mbedtls_platform_zeroize(&history->turns[index], sizeof(history->turns[index]));
  memcpy(history->turns[index].user, user, strlen(user));
  memcpy(history->turns[index].assistant, assistant, strlen(assistant));
  return 0;
}

static bool add_message(cJSON *messages, const char *role, const char *content)
{
  cJSON *message = cJSON_CreateObject();
  if (message == NULL) return false;
  if (!cJSON_AddStringToObject(message, "role", role) ||
      !cJSON_AddStringToObject(message, "content", content) ||
      !cJSON_AddItemToArray(messages, message))
    { cJSON_Delete(message); return false; }
  return true;
}

static bool add_image_message(cJSON *messages, const char *prompt,
                              const char *marker)
{
  cJSON *message = cJSON_CreateObject();
  cJSON *content = cJSON_CreateArray();
  cJSON *part = cJSON_CreateObject();
  cJSON *image_part = cJSON_CreateObject();
  cJSON *image = cJSON_CreateObject();
  char url[64];
  int length = snprintf(url, sizeof(url), "data:image/jpeg;base64,%s", marker);
  if (length < 0 || (size_t)length >= sizeof(url) || message == NULL ||
      content == NULL || part == NULL || image_part == NULL || image == NULL ||
      !cJSON_AddStringToObject(message, "role", "user") ||
      !cJSON_AddStringToObject(part, "type", "text") ||
      !cJSON_AddStringToObject(part, "text", prompt) ||
      !cJSON_AddStringToObject(image_part, "type", "image_url") ||
      !cJSON_AddStringToObject(image, "url", url) ||
      !cJSON_AddItemToObject(image_part, "image_url", image)) goto fail;
  image = NULL;
  if (!cJSON_AddItemToArray(content, part)) goto fail;
  part = NULL;
  if (!cJSON_AddItemToArray(content, image_part)) goto fail;
  image_part = NULL;
  if (!cJSON_AddItemToObject(message, "content", content)) goto fail;
  content = NULL;
  if (!cJSON_AddItemToArray(messages, message)) goto fail;
  return true;
fail:
  cJSON_Delete(message);
  cJSON_Delete(content);
  cJSON_Delete(part);
  cJSON_Delete(image_part);
  cJSON_Delete(image);
  return false;
}

/* The official Agent owns chat serialization and response/tool parsing.
 * Product code supplies the existing single TLS request owner, deadline and
 * bounded JPEG body source. It creates no second session or retry loop.
 */
struct bkcloud_agent_request_s
{
  struct bkcloud_client_s *client;
  const struct bkcloud_config_s *config;
  const struct bkvoice_wss_tls_ops_s *tls;
  void *tls_context;
  uint64_t deadline_ms;
  const uint8_t *jpeg;
  size_t jpeg_size;
  const struct bkcloud_camera_s *camera;
  cJSON *parameters;
  bool camera_requested;
};

static const char g_bkcloud_camera_tool[] =
  /* Agent owns the provider wire format. Its tool registry consumes
   * name/description/input_schema, then wraps these for OpenAI providers.
   */
  "[{"
  "\"name\":\"capture_current_view\","
  "\"description\":\"Capture a fresh camera frame for the current user's visual request.\","
  "\"input_schema\":{\"type\":\"object\",\"properties\":{},"
  "\"required\":[],\"additionalProperties\":false}}]";

static int bkcloud_agent_dispatch(void *context, const llm_tool_call_t *call,
                                  agent_turn_tool_result_t *result)
{
  struct bkcloud_agent_request_s *request = context;
  if (request->camera == NULL || request->camera->capture == NULL ||
      strcmp(call->name, "capture_current_view") != 0) return -EACCES;

  const char *end = NULL;
  const char *input = call->input != NULL ? call->input : "{}";
  cJSON *arguments = cJSON_ParseWithOpts(input, &end, true);
  bool valid = cJSON_IsObject(arguments) && arguments->child == NULL;
  cJSON_Delete(arguments);
  if (!valid) return -EINVAL;
  if (request->camera_requested)
    {
      result->text = "{\"error\":\"only_one_frame_per_turn\"}";
      return 0;
    }

  request->camera_requested = true;
  int ret = request->camera->capture(request->camera->context,
                                     &request->jpeg, &request->jpeg_size);
  if (ret < 0) return ret;
  if (request->jpeg == NULL || request->jpeg_size == 0) return -EBADMSG;
  result->extra_messages = cJSON_CreateArray();
  if (result->extra_messages == NULL ||
      !add_image_message(result->extra_messages,
        "This is the fresh frame captured for my current request. Describe what you see.", ""))
    return -ENOMEM;
  result->text = "{\"captured\":true,\"fresh_frame_follows\":true}";
  /* The consented first request must call the camera. Once it has done so,
   * let the official loop obtain the multimodal answer without forcing a
   * second capture. All iterations keep the same product deadline and TLS. */
  cJSON_DeleteItemFromObjectCaseSensitive(request->parameters, "tool_choice");
  return 0;
}

static int bkcloud_agent_post(void *opaque, const char *body_data,
                              size_t body_size, char **response,
                              size_t *response_size)
{
  static const char image_prefix[] = "\"url\":\"data:image/jpeg;base64,";
  struct bkcloud_agent_request_s *request = opaque;
  struct bkcloud_client_s *client = request->client;
  struct bkcloud_image_source_s image = {0};
  struct webclient_context body;
  int ret;

  *response = NULL;
  *response_size = 0;
  if (body_size > BKCLOUD_REQUEST_CAPACITY) return -E2BIG;
  webclient_set_defaults(&body);
  webclient_set_static_body(&body, body_data, body_size);
  if (request->jpeg != NULL)
    {
      /* Stream the one current frame without another full base64 allocation.
       * The Agent has serialized all messages and provider parameters already.
       */
      const char *location = strstr(body_data, image_prefix);
      if (location == NULL ||
          strstr(location + sizeof(image_prefix) - 1, image_prefix) != NULL)
        return -EBADMSG;
      size_t prefix = (size_t)(location - body_data) + sizeof(image_prefix) - 1;
      ret = bkcloud_image_source_init(&image, body_data, prefix,
                                      body_data + prefix, body_size - prefix,
                                      request->jpeg, request->jpeg_size);
      if (ret != 0) return ret;
      body.body_callback = bkcloud_image_body;
      body.body_callback_arg = &image;
      body.bodylen = image.length;
    }
  ret = bkcloud_http_post(&client->http, request->config, "chat/completions",
                          request->tls, request->tls_context,
                          request->deadline_ms, body.body_callback,
                          body.body_callback_arg, body.bodylen,
                          client->response, sizeof(client->response));
  bkcloud_image_source_clear(&image);
  if (ret == 0)
    {
      *response = malloc(client->http.received + 1);
      if (*response == NULL) ret = -ENOMEM;
      else
        {
          memcpy(*response, client->response, client->http.received);
          (*response)[client->http.received] = '\0';
          *response_size = client->http.received;
        }
    }
  mbedtls_platform_zeroize(client->response, sizeof(client->response));
  return ret;
}

static int bkcloud_agent_chat(struct bkcloud_agent_request_s *transport,
                              const char *persona,
                              const struct bkcloud_history_s *history,
                              const char *input, char *text, size_t capacity)
{
  cJSON *messages = NULL;
  cJSON *parameters = NULL;
  llm_response_t response = {0};
  int ret = -ENOMEM;
  if (text == NULL || capacity == 0) return -EINVAL;
  memset(text, 0, capacity);
  if (transport->client == NULL || transport->config == NULL ||
      !bkcloud_audio_valid_text(persona) || !bkcloud_audio_valid_text(input) || history == NULL ||
      history->count > BKCLOUD_HISTORY_TURNS) return -EINVAL;
  const struct bkcloud_config_s *config = transport->config;
  if (config->dialect != 1 && config->dialect != 2) return -ENOTSUP;
  for (size_t i = 0; i < history->count; i++)
    if (!bkcloud_audio_valid_text(history->turns[i].user) ||
        !bkcloud_audio_valid_text(history->turns[i].assistant)) return -EINVAL;
  memset(transport->client, 0, sizeof(*transport->client));
  messages = cJSON_CreateArray();
  parameters = cJSON_CreateObject();
  if (messages == NULL || parameters == NULL ||
      !cJSON_AddBoolToObject(parameters, "stream", false) ||
      !cJSON_AddNumberToObject(parameters, config->dialect == 2 ?
        "max_completion_tokens" : "max_tokens", 1024)) goto out;
  if (config->dialect == 2)
    {
      cJSON *thinking = cJSON_AddObjectToObject(parameters, "thinking");
      if (thinking == NULL ||
          !cJSON_AddStringToObject(thinking, "type", "disabled")) goto out;
    }
  for (size_t i = 0; i < history->count; i++)
    if (!add_message(messages, "user", history->turns[i].user) ||
        !add_message(messages, "assistant", history->turns[i].assistant)) goto out;
  if (transport->camera != NULL)
    {
      cJSON *choice = cJSON_AddObjectToObject(parameters, "tool_choice");
      if (choice == NULL ||
          !cJSON_AddStringToObject(choice, "type", "function")) goto out;
      cJSON *function = cJSON_AddObjectToObject(choice, "function");
      if (function == NULL || !cJSON_AddStringToObject(function, "name",
                                                     "capture_current_view")) goto out;
    }
  if (!add_message(messages, "user", input)) goto out;
  transport->parameters = parameters;
  const llm_request_t request = {
    .model = config->chat_model, .host = config->host,
    .parameters = parameters, .context = transport, .post = bkcloud_agent_post
  };
  ret = agent_turn_run(persona, messages,
                        transport->camera != NULL ? g_bkcloud_camera_tool : NULL,
                        &request, 3, bkcloud_agent_dispatch, transport, &response);
  if (ret == 0)
    {
      if (response.tool_use || response.call_count != 0) ret = -ENOTSUP;
      else if (response.text == NULL || response.text_len == 0) ret = -EBADMSG;
      else if (response.text_len >= capacity) ret = -E2BIG;
      else memcpy(text, response.text, response.text_len + 1);
    }
 out:
  if (response.text != NULL)
    mbedtls_platform_zeroize(response.text, response.text_len);
  llm_response_free(&response);
  cJSON_Delete(messages);
  cJSON_Delete(parameters);
  return ret;
}

int bkcloud_chat(struct bkcloud_client_s *client,
                 const struct bkcloud_config_s *config,
                 const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                 uint64_t deadline_ms, const char *persona,
                 const struct bkcloud_history_s *history, const char *input,
                 const struct bkcloud_camera_s *camera,
                 char *text, size_t capacity)
{
  struct bkcloud_agent_request_s request = {
    .client = client, .config = config, .tls = tls,
    .tls_context = tls_context, .deadline_ms = deadline_ms, .camera = camera
  };
  return bkcloud_agent_chat(&request, persona, history, input, text, capacity);
}

struct playback_sink_s
{
  struct bkcloud_playback_s *play;
  struct bkvoice_turn_s *turn;
  uint64_t (*now_ms)(void *);
  void *clock_context;
  bool started;
};
static int playback_sink(void *context, const void *pcm, size_t size)
{
  struct playback_sink_s *sink = context;
  if (!sink->started)
    {
      int ret = bkcloud_playback_begin(sink->play, sink->turn, sink->now_ms,
                                       sink->clock_context);
      if (ret) return ret;
      sink->started = true;
    }
  return bkcloud_playback_feed(sink->play, pcm, size);
}
int bkcloud_synthesize_turn(struct bkcloud_client_s *client,
                            struct bkcloud_tts_s *decoder,
                            struct bkcloud_playback_s *play,
                            struct bkvoice_turn_s *turn,
                            const struct bkcloud_config_s *config,
                            const struct bkvoice_wss_tls_ops_s *tls,
                            void *tls_context, uint64_t deadline_ms,
                            uint64_t (*now_ms)(void *), void *clock_context,
                            const char *text)
{
  if (!play || !turn || !now_ms || turn->state != BKVOICE_TURN_WAITING_TTS)
    return -EINVAL;
  struct playback_sink_s sink = {play, turn, now_ms, clock_context, false};
  int ret = bkcloud_synthesize(client, decoder, config, tls, tls_context,
                                deadline_ms, text, playback_sink, &sink);
  if (ret == 0 && sink.started) ret = bkcloud_playback_end(play);
  else if (ret == 0) ret = -ENODATA;
  if (ret && sink.started) bkcloud_playback_abort(play, ret);
  else if (ret)
    {
      struct bkvoice_turn_token_s token = turn->active;
      token.sequence = turn->last_control_sequence + 1;
      (void)bkvoice_turn_cancel(turn, &token, ret);
    }
  return ret;
}
