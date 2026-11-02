/* SPDX-License-Identifier: Apache-2.0 */
/* Host manager regression.  The loader below is deliberately a stub: these
 * checks validate signing, persistence and lifecycle, not ARM execution. */
#include "bk7258_native_apps.h"
#include "bk7258_native_loader.h"
#include "cJSON.h"
#include <assert.h>
#include <errno.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bk7258_provision_store.h"

#ifndef NATIVE_TEST_KEY
#  error "NATIVE_TEST_KEY must name the upstream mbedTLS test private key"
#endif
#ifndef NATIVE_FOREIGN_TEST_KEY
#  error "NATIVE_FOREIGN_TEST_KEY must name a different upstream test key"
#endif
#ifndef NATIVE_TEST_IMAGE
#  error "NATIVE_TEST_IMAGE must name a cross-compiled ARM ET_REL sample"
#endif

static int g_opens;
static int g_closes;
static int g_loader_mode;
static int g_commit_failure;
int __real_bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                               const uint8_t transaction[16], const void *bundle,
                               size_t size);
int __wrap_bkprov_store_commit(struct bkprov_store_s *store, uint64_t expected,
                               const uint8_t transaction[16], const void *bundle,
                               size_t size)
{
  return g_commit_failure ? g_commit_failure :
    __real_bkprov_store_commit(store, expected, transaction, bundle, size);
}
extern const struct bknative_program_s *shaniu_native_entry(void);
static const struct bknative_program_s g_bad_program = { 2, 4, NULL, NULL };
int bknative_loader_open(const char *path, void **handle,
                         const struct bknative_program_s **program)
{
  assert(strstr(path, "/running.elf"));
  if (g_loader_mode == 2) return -ENOEXEC;
  *handle = &g_opens;
  *program = g_loader_mode == 1 ? &g_bad_program : shaniu_native_entry();
  g_opens++; return 0;
}
int bknative_loader_close(void *handle) { assert(handle == &g_opens); g_closes++; return 0; }

static void b64(char *out, size_t capacity, const void *data, size_t size)
{
  size_t used; assert(!mbedtls_base64_encode((unsigned char *)out, capacity, &used,
                         data, size)); out[used] = 0;
}
static void digest(const void *data, size_t size, char out[65])
{
  static const char hex[] = "0123456789abcdef"; unsigned char value[32];
  assert(!mbedtls_sha256(data, size, value, 0));
  for (unsigned int i = 0; i < sizeof(value); i++)
    { out[i * 2] = hex[value[i] >> 4]; out[i * 2 + 1] = hex[value[i] & 15]; }
  out[64] = 0;
}
static uint32_t little(const uint8_t *value)
{
  return (uint32_t)value[0] | (uint32_t)value[1] << 8 |
         (uint32_t)value[2] << 16 | (uint32_t)value[3] << 24;
}
static uint8_t *image_read(size_t *size)
{
  FILE *file = fopen(NATIVE_TEST_IMAGE, "rb"); long length; uint8_t *image;
  assert(file && !fseek(file, 0, SEEK_END)); length = ftell(file);
  assert(length > 52 && length <= BKNATIVE_IMAGE_MAX && !fseek(file, 0, SEEK_SET));
  image = malloc((size_t)length); assert(image && fread(image, 1, (size_t)length, file) == (size_t)length);
  assert(!fclose(file)); *size = (size_t)length; return image;
}
static void invalid_section_link(uint8_t *image, size_t size)
{
  uint32_t offset = little(image + 32); unsigned int count = image[48] | image[49] << 8;
  assert(offset <= size && count && count <= (size - offset) / 40);
  for (unsigned int index = 0; index < count; index++)
    if (little(image + offset + index * 40 + 4) == 2)
      {
        memset(image + offset + index * 40 + 24, 0xff, 4); return;
      }
  assert(!"cross-compiled sample needs a symbol table");
}
static char *revision(char *state)
{
  cJSON *object = cJSON_Parse(state); cJSON *value = cJSON_GetObjectItem(object, "revision");
  char *result = strdup(value->valuestring); cJSON_Delete(object); assert(result); return result;
}
static int apply(const char *op, const char *payload, char *state, size_t capacity)
{
  char *rev = revision(state); char request[32769];
  snprintf(request, sizeof(request), "{\"revision\":\"%s\",\"op\":\"%s\",%s}", rev, op, payload);
  free(rev); int ret = bknative_apply(request);
  if (ret != -EINPROGRESS) assert(bknative_read(state, capacity) == 0);
  return ret;
}
static void sign(mbedtls_pk_context *key, mbedtls_ctr_drbg_context *random,
                 const char *manifest, char *out, size_t capacity)
{
  unsigned char hash[32], signature[80]; size_t size;
  assert(!mbedtls_sha256((const unsigned char *)manifest, strlen(manifest), hash, 0));
  assert(!mbedtls_pk_sign(key, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature,
                          sizeof(signature), &size, mbedtls_ctr_drbg_random, random));
  b64(out, capacity, signature, size);
}
static void package(mbedtls_pk_context *key, mbedtls_ctr_drbg_context *random,
                    const char *id, unsigned int version, int abi, const uint8_t *image, size_t image_size,
                    char *out, size_t capacity)
{
  char hash[65], manifest[512], manifest64[2048], image64[24576], signature[128];
  digest(image, image_size, hash);
  snprintf(manifest, sizeof(manifest), "{\"id\":\"%s\",\"name\":\"%s\",\"description\":\"host test\",\"version\":%u,\"abi\":%d,\"target\":\"bk7258-ap-armv8m\",\"sha256\":\"%s\",\"image_size\":%zu,\"permissions\":[\"display\"]}", id, id, version, abi, hash, image_size);
  b64(manifest64, sizeof(manifest64), manifest, strlen(manifest));
  b64(image64, sizeof(image64), image, image_size); sign(key, random, manifest, signature, sizeof(signature));
  size_t required = strlen(manifest64) + strlen(signature) + strlen(image64) + 69;
  assert(required < capacity);
  strcpy(out, "{\"format\":\"shaniu.native/1\",\"manifest\":\"");
  strcat(out, manifest64); strcat(out, "\",\"signature\":\"");
  strcat(out, signature); strcat(out, "\",\"image\":\"");
  strcat(out, image64); strcat(out, "\"}");
}

int main(void)
{
  mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context random; mbedtls_pk_context key, foreign;
  char public_der[256], public64[384], foreign64[384], publisher[65], state[4096], pack[24576], payload[32768]; int public_size;
  size_t image_size; uint8_t *image;
  assert(system("rm -rf /tmp/bk7258-native-apps") == 0);
  mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&random); mbedtls_pk_init(&key); mbedtls_pk_init(&foreign);
  assert(!mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)"native-host", 11));
  assert(!mbedtls_pk_parse_keyfile(&key, NATIVE_TEST_KEY, NULL,
                                   mbedtls_ctr_drbg_random, &random));
  public_size = mbedtls_pk_write_pubkey_der(&key, (unsigned char *)public_der, sizeof(public_der));
  assert(public_size > 0 && public_size < (int)sizeof(public_der));
  memmove(public_der, public_der + sizeof(public_der) - public_size, public_size);
  b64(public64, sizeof(public64), public_der, public_size);
  digest(public_der, public_size, publisher);
  assert(!strcmp(publisher, CONFIG_BK7258_NATIVE_PUBLISHER_SHA256));
  printf("native_apps_publisher_sha256=%s\n", publisher);
  assert(!mbedtls_pk_parse_keyfile(&foreign, NATIVE_FOREIGN_TEST_KEY, NULL,
                                   mbedtls_ctr_drbg_random, &random));
  public_size = mbedtls_pk_write_pubkey_der(&foreign, (unsigned char *)public_der, sizeof(public_der));
  assert(public_size > 0 && public_size < (int)sizeof(public_der));
  memmove(public_der, public_der + sizeof(public_der) - public_size, public_size);
  b64(foreign64, sizeof(foreign64), public_der, public_size);
  image = image_read(&image_size);
  assert(bknative_init() == 0 && bknative_read(state, sizeof(state)) == 0 &&
         strstr(state, "\"capacity\":4") && strstr(state, "\"storage_limit\":32768"));
  assert(apply("trust", "\"public_key\":\"!\"", state, sizeof(state)) == -EKEYREJECTED);
  snprintf(payload, sizeof(payload), "\"public_key\":\"%s\"", foreign64);
  assert(apply("trust", payload, state, sizeof(state)) == -EKEYREJECTED);
  snprintf(payload, sizeof(payload), "\"public_key\":\"%s\"", public64);
  assert(apply("trust", payload, state, sizeof(state)) == 0);

  package(&foreign, &random, "wrong-signer", 1, BKNATIVE_ABI,
          image, image_size, pack, sizeof(pack));
  snprintf(payload, sizeof(payload), "\"package\":%s", pack);
  assert(apply("install", payload, state, sizeof(state)) == -EKEYREJECTED);

  package(&key, &random, "breath-coach", 1, BKNATIVE_ABI, image, image_size, pack, sizeof(pack));
  snprintf(payload, sizeof(payload), "\"package\":%s", pack);
  char *before_failure = revision(state);
  g_commit_failure = -EIO;
  assert(apply("install", payload, state, sizeof(state)) == -EIO && !strstr(state, "breath-coach"));
  char *after_failure = revision(state);
  assert(!strcmp(after_failure, before_failure));
  free(after_failure);
  free(before_failure); g_commit_failure = 0;
  g_commit_failure = -EINPROGRESS;
  assert(apply("install", payload, state, sizeof(state)) == -EINPROGRESS);
  assert(apply("install", payload, state, sizeof(state)) == -EINPROGRESS);
  g_commit_failure = 0;
  assert(bknative_init() == 0 && bknative_read(state, sizeof(state)) == 0 &&
         !strstr(state, "breath-coach")); /* recovery reloads the durable old record */
  assert(apply("install", payload, state, sizeof(state)) == 0 && strstr(state, "breath-coach"));
  assert(apply("install", payload, state, sizeof(state)) == -ESTALE); /* version must increase */
  char *old = revision(state); snprintf(payload, sizeof(payload), "\"id\":\"breath-coach\"");
  char stale[256]; snprintf(stale, sizeof(stale), "{\"revision\":\"%s\",\"op\":\"start\",\"id\":\"breath-coach\"}", old); free(old);
  assert(bknative_apply(stale) == 0 && g_opens == 1);
  { struct bknative_frame_s frame;
    assert(bknative_step(100, true, &frame) == 1);
    assert(frame.expression == 1 && frame.duration_ms == 4000 && frame.progress == 0);
    assert(bknative_step(4100, true, &frame) == 1);
    assert(frame.expression == 2 && frame.duration_ms == 4000 && frame.progress > 0);
  }
  assert(bknative_apply(stale) == -EAGAIN); /* prior read revision is stale */
  { struct bknative_frame_s frame;
    assert(bknative_step(60100, true, &frame) == 0 && g_closes == 1); /* real module completes */
  }
  assert(bknative_read(state, sizeof(state)) == 0);
  assert(apply("start", "\"id\":\"breath-coach\"", state, sizeof(state)) == 0);
  assert(apply("stop", "\"id\":\"breath-coach\"", state, sizeof(state)) == 0 && g_closes == 2);
  for (unsigned int index = 0; index < 100; index++)
    {
      assert(apply("start", "\"id\":\"breath-coach\"", state, sizeof(state)) == 0);
      assert(apply("stop", "\"id\":\"breath-coach\"", state, sizeof(state)) == 0);
    }
  assert(g_opens == 102 && g_closes == 102 && strstr(state, "\"running\":\"\""));

  package(&key, &random, "breath-coach", 2, BKNATIVE_ABI, image, image_size, pack, sizeof(pack));
  snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) == 0);
  for (unsigned int i = 2; i <= 4; i++) { char id[24]; snprintf(id, sizeof(id), "app-%u", i); package(&key, &random, id, 1, BKNATIVE_ABI, image, image_size, pack, sizeof(pack)); snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) == 0); }
  package(&key, &random, "app-5", 1, BKNATIVE_ABI, image, image_size, pack, sizeof(pack)); snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) == -ENOSPC);
  assert(bknative_init() == 0 && bknative_read(state, sizeof(state)) == 0 && strstr(state, "\"version\":2"));
  assert(apply("uninstall", "\"id\":\"app-4\"", state, sizeof(state)) == 0);
  package(&key, &random, "bad-abi", 1, 2, image, image_size, pack, sizeof(pack)); snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) == -EINVAL);
  uint8_t *bad_link = malloc(image_size); assert(bad_link); memcpy(bad_link, image, image_size); invalid_section_link(bad_link, image_size);
  package(&key, &random, "bad-link", 1, BKNATIVE_ABI, bad_link, image_size, pack, sizeof(pack)); snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) == -ENOEXEC); free(bad_link);
  package(&key, &random, "loader-fail", 1, BKNATIVE_ABI, image, image_size, pack, sizeof(pack)); snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) == 0);
  g_loader_mode = 1; assert(apply("start", "\"id\":\"loader-fail\"", state, sizeof(state)) == -ENOEXEC); g_loader_mode = 0;
  pack[strlen(pack) - 3] ^= 1; snprintf(payload, sizeof(payload), "\"package\":%s", pack); assert(apply("install", payload, state, sizeof(state)) < 0);
  free(image); mbedtls_pk_free(&foreign); mbedtls_pk_free(&key); mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  return 0;
}
