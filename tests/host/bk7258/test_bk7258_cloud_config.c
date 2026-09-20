/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_config.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
  struct bkcloud_config_s config;
  unsigned char data[BKCLOUD_CONFIG_MAX] = {'C', 'C', 'F', '1', 1, 0, 1, 187};
  const char *fields[] = {"cloud.example", "/v1", "fixture-only",
                          "vendor/asr", "vendor/chat", "vendor/tts"};
  size_t size = 24;
  for (size_t i = 0; i < 6; i++)
    {
      size_t n = strlen(fields[i]);
      data[8 + i * 2] = n >> 8; data[9 + i * 2] = n;
      memcpy(data + size, fields[i], n); size += n;
    }
  assert(bkcloud_config_decode(&config, data, size) == 0);
  assert(config.port == 443 && config.dialect == 1);
  assert(strcmp(config.host, fields[0]) == 0);
  assert(strcmp(config.api_key, fields[2]) == 0);
  assert(strcmp(config.asr_model, fields[3]) == 0);
  assert(strcmp(config.chat_model, fields[4]) == 0);
  assert(strcmp(config.tts_model, fields[5]) == 0);
  for (size_t n = 0; n < size; n++)
    {
      memset(&config, 0xa5, sizeof(config));
      assert(bkcloud_config_decode(&config, data, n) < 0);
      const unsigned char *p = (const unsigned char *)&config;
      for (size_t i = 0; i < sizeof(config); i++) assert(p[i] == 0);
    }
  assert(bkcloud_config_decode(&config, data, size + 1) < 0);
  for (size_t i = 0; i < size; i++)
    {
      if (i == 6 || i == 7) continue; /* Any nonzero TLS port is allowed. */
      unsigned char saved = data[i]; data[i] = 255;
      assert(bkcloud_config_decode(&config, data, size) < 0);
      data[i] = saved;
    }
  data[4] = 2;
  assert(bkcloud_config_decode(&config, data, size) == 0);
  data[24] = '-';
  assert(bkcloud_config_decode(&config, data, size) < 0);
  data[24] = 'c';
  data[24 + strlen(fields[0])] = '?';
  assert(bkcloud_config_decode(&config, data, size) < 0);
  bkcloud_config_clear(&config);
  assert(config.api_key[0] == 0 && config.dialect == 0);
  puts("Cloud configuration validation and clearing: PASS");
  return 0;
}
