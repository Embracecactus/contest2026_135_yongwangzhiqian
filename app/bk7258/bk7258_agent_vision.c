/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>

#include "cJSON.h"
#include "llm/llm_proxy.h"
#include "tools/tool_registry.h"
#include "bk7258_agent_vision.h"
#include "bk7258_vision_service.h"

static char *vision_tools(void)
{
  return strdup("[{\"name\":\"camera_capture\","
    "\"description\":\"Take a fresh photo with the device camera and answer "
    "a question about visible objects using the selected LLM. Only use when "
    "the user asks to look or take a photo. Resolution is fixed at 640x480.\","
    "\"input_schema\":{\"type\":\"object\",\"properties\":{\"prompt\":{"
    "\"type\":\"string\",\"description\":\"Question about what is visible\"}},"
    "\"required\":[\"prompt\"]}}]");
}

static int vision_execute(const char *name, const char *input,
                          char *output, size_t capacity,
                          int (*check)(void *), void *request_context)
{
  static const char prefix[] = "data:image/jpeg;base64,";
  const size_t image_capacity = CONFIG_BK7258_VISION_JPEG_BUFFER_BYTES;
  cJSON *args = NULL;
  cJSON *messages = NULL;
  cJSON *message = NULL;
  cJSON *content = NULL;
  cJSON *text = NULL;
  cJSON *image = NULL;
  cJSON *url = NULL;
  uint8_t *jpeg = NULL;
  char *uri = NULL;
  size_t jpeg_size = 0;
  size_t uri_capacity = 0;
  size_t encoded = 0;
  llm_response_t response = {0};
  int ret = -EINVAL;

  if (!name || strcmp(name, "camera_capture") != 0) return ERROR;
  if (!output || !capacity) return ERROR;
  output[0] = '\0';
  ret = check ? check(request_context) : 0;
  if (ret) goto out;
  ret = -EINVAL;
  args = input ? cJSON_Parse(input) : NULL;
  cJSON *prompt = cJSON_GetObjectItemCaseSensitive(args, "prompt");
  if (!cJSON_IsObject(args) || !cJSON_IsString(prompt) ||
      !prompt->valuestring || !prompt->valuestring[0] ||
      strlen(prompt->valuestring) > 1024) goto out;

  jpeg = malloc(image_capacity);
  if (!jpeg) { ret = -ENOMEM; goto out; }

  /* 只借用现有唯一摄像头 owner；它在返回前已停止采集并释放 V4L2。
   * 不保存图片、不接入第二条捕获任务，也不从历史文件取图。
   */
  ret = bk7258_vision_capture_jpeg(jpeg, image_capacity, &jpeg_size);
  syslog(LOG_INFO, "BKVOICE camera capture owner=vision bytes=%zu result=%d\n",
         jpeg_size, ret);
  if (ret) goto out;
  ret = check ? check(request_context) : 0;
  if (ret) goto out;
  if (!jpeg_size || jpeg_size > image_capacity) { ret = -EBADMSG; goto out; }

  uri_capacity = sizeof(prefix) + 4 * ((jpeg_size + 2) / 3);
  uri = malloc(uri_capacity);
  if (!uri) { ret = -ENOMEM; goto out; }
  memcpy(uri, prefix, sizeof(prefix) - 1);
  ret = mbedtls_base64_encode((unsigned char *)uri + sizeof(prefix) - 1,
    uri_capacity - sizeof(prefix) + 1, &encoded, jpeg, jpeg_size);
  if (ret) { ret = -EIO; goto out; }
  uri[sizeof(prefix) - 1 + encoded] = '\0';
  mbedtls_platform_zeroize(jpeg, image_capacity);
  free(jpeg);
  jpeg = NULL;

  messages = cJSON_CreateArray();
  message = cJSON_CreateObject();
  content = cJSON_CreateArray();
  text = cJSON_CreateObject();
  image = cJSON_CreateObject();
  url = cJSON_CreateObject();
  if (!messages || !message || !content || !text || !image || !url ||
      !cJSON_AddStringToObject(message, "role", "user") ||
      !cJSON_AddStringToObject(text, "type", "text") ||
      !cJSON_AddStringToObject(text, "text", prompt->valuestring) ||
      !cJSON_AddStringToObject(image, "type", "image_url") ||
      !cJSON_AddStringToObject(url, "url", uri))
    { ret = -ENOMEM; goto out; }

  ret = -ENOMEM;
  if (!cJSON_AddItemToObject(image, "image_url", url)) goto out;
  url = NULL;
  if (!cJSON_AddItemToArray(content, text)) goto out;
  text = NULL;
  if (!cJSON_AddItemToArray(content, image)) goto out;
  image = NULL;
  if (!cJSON_AddItemToObject(message, "content", content)) goto out;
  content = NULL;
  if (!cJSON_AddItemToArray(messages, message)) goto out;
  message = NULL;

  /* 复用同一 LLM 代理的多模态 messages 和受保护 transport。
   * 子请求继续检查原请求状态，传输 prepare 不能抹去取消或已到期状态。
   * 这里只适配图片内容，模型、鉴权、请求与返回解析仍归同一 LLM 代理。
   * 不设置独立视觉服务商、不在此创建 HTTP 客户端或工具循环。
   */
  ret = llm_chat_tools_checked("Answer the user's question using this fresh camera "
    "image. Be concise. Do not invent details you cannot see.",
    messages, NULL, &response, check, request_context);
  if (!ret && (!response.text || !response.text[0] || response.tool_use))
    ret = -ENODATA;
  if (!ret && strlen(response.text) >= capacity) ret = -ENOSPC;
  if (!ret) memcpy(output, response.text, strlen(response.text) + 1);

out:
  llm_response_free(&response);
  cJSON_Delete(url);
  cJSON_Delete(image);
  cJSON_Delete(text);
  cJSON_Delete(content);
  cJSON_Delete(message);
  cJSON_Delete(messages);
  cJSON_Delete(args);
  if (uri) { mbedtls_platform_zeroize(uri, uri_capacity); free(uri); }
  if (jpeg) { mbedtls_platform_zeroize(jpeg, image_capacity); free(jpeg); }
  if (ret)
    snprintf(output, capacity,
      "Error: camera analysis failed (%d); no visual conclusion is available.", ret);
  syslog(LOG_INFO, "BKVOICE camera tool result=%d\n", ret);
  /* 官方 provider 的 OK 表示命中并执行了该工具。真实能力失败作为结果
   * 返回给 Agent；否则 registry 会把已识别工具的错误覆盖成 unknown tool。
   */
  return OK;
}

void bk7258_agent_vision_register(void)
{
  tool_registry_register_provider_checked("bk7258-camera", vision_tools,
                                          vision_execute);
  tool_registry_invalidate();
}
