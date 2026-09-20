/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_settings.h"
#include "bk7258_voice_config.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned int clock_writes;
int __wrap_clock_settime(clockid_t id, const struct timespec *value);
int __wrap_clock_settime(clockid_t id, const struct timespec *value)
{
  (void)id;
  (void)value;
  clock_writes++;
  errno = EPERM;
  return -1;
}

static size_t read_der(const char *path, uint8_t *output)
{
  FILE *file = fopen(path, "rb");
  assert(file != NULL);
  size_t size = fread(output, 1, 4096, file);
  assert(size > 0 && size < 4096 && !ferror(file));
  assert(fclose(file) == 0);
  return size;
}

int main(int argc, char **argv)
{
  uint8_t cert[4096], key[4096], bundle[8192] = {0}, voice[8192];
  struct bkprov_settings_s settings;
  struct bkvoice_config_s config = {0};
  size_t size, voice_size;
  uint64_t utc = UINT64_C(1800000000);
  assert(argc == 3);
  size_t cert_size = read_der(argv[1], cert);
  size_t key_size = read_der(argv[2], key);
  memcpy(bundle, "SCB1", 4);
  bundle[4] = 4;
  bundle[5] = 8;
  bundle[6] = 9;
  bundle[8] = 0x20;
  bundle[9] = 0xfb;
  bundle[12] = 192;
  bundle[13] = 168;
  bundle[14] = 1;
  bundle[15] = 2;
  for (int i = 23; i >= 16; i--) { bundle[i] = utc; utc >>= 8; }
  bundle[26] = cert_size >> 8;
  bundle[27] = cert_size;
  memcpy(bundle + 32, "testpasswordlocalhost", 21);
  memcpy(bundle + 53, cert, cert_size);
  size = 53 + cert_size;
  assert(bkprov_settings_decode(&settings, bundle, size) == 0);
  assert(strcmp(settings.ssid, "test") == 0);
  assert(strcmp(settings.password, "password") == 0);
  assert(settings.port == 8443);
  assert(bkprov_settings_voice(&settings, cert, cert_size, key, key_size,
                               voice, sizeof(voice), &voice_size) == 0);
  assert(bkvoice_config_validate(voice, voice_size) == 0);
  assert(clock_writes == 0);
  assert(bkvoice_config_load(&config, voice, voice_size) == -EPERM);
  assert(clock_writes == 1 && !config.initialized);
  voice[voice_size - 1] ^= 1;
  assert(bkvoice_config_validate(voice, voice_size) < 0);
  assert(clock_writes == 1);
  assert(bkprov_settings_decode(&settings, bundle, size - 1) < 0);
  assert(settings.ca == NULL && settings.password[0] == 0);
  bundle[44] = '-';
  assert(bkprov_settings_decode(&settings, bundle, size) < 0);
  bundle[44] = 'l';
  uint8_t *cloud = bundle+size;
  const char *fields[]={"localhost", "/v1", "test-key", "asr", "chat", "tts"};
  memcpy(cloud, "CCF1", 4);cloud[4] = 2;cloud[6] = 0x20;cloud[7] = 0xfb;
  size_t cloud_size = 24;
  for (size_t i=0;i<6;i++)
  {
    size_t n = strlen(fields[i]);cloud[9+2*i] = n;
    memcpy(cloud+cloud_size, fields[i], n);cloud_size += n;
  }
  bundle[3]='2';bundle[31] = cloud_size;
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size) == 0);
  assert(settings.cloud == cloud && settings.cloud_size == cloud_size);
  cloud[7] ^= 1;
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size)<0);
  assert(settings.cloud == NULL && settings.password[0] == 0);
  cloud[7] ^= 1;
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size-1)<0);
  bundle[3]='3';
  uint8_t *control = cloud+cloud_size;
  memset(control, 0, 32);control[31] = 0x7b;
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size+32) == 0);
  assert(settings.control_key == control && settings.control_key[31] == 0x7b);
  assert(settings.cloud == cloud && settings.cloud_size == cloud_size);
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size+31)<0);
  assert(settings.control_key == NULL && settings.cloud == NULL);
  control[31] = 0;
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size+32)<0);
  assert(settings.control_key == NULL && settings.password[0] == 0);
  bundle[3]='2';
  assert(bkprov_settings_decode(&settings, bundle, size+cloud_size+32)<0);
  bundle[3]='1';bundle[31] = 0;
  bundle[12] = 127;
  assert(bkprov_settings_decode(&settings, bundle, size) < 0);
  puts("settings DER validation and clock isolation: PASS");
  return 0;
}
