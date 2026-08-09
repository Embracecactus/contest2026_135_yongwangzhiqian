/* Host harness for the format-2 symmetric boot selector. */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "boot_ota_rotation_select_core.h"
#include "bk7258_partition_layout.h"

#define SLOT_SIZE   BK7258_ROLE_SLOT_B_PAIR_SIZE
#define SLOT_A      BK7258_ROLE_SLOT_A_CP_OFFSET
#define SLOT_B      BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define BANK0       BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET
#define BANK1       BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET

struct host_hash_s
{
  EVP_MD_CTX *context;
};

struct host_context_s
{
  uint8_t *slot[2];
  uint8_t *bank[2];
  const char *mode;
  uint32_t slot_reads[2];
  uint32_t bank_reads[2];
  bool outside_access;
  bool injected;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 format-2 selector harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_file(const char *path, size_t expected_size)
{
  FILE *stream = fopen(path, "rb");
  uint8_t *data;
  long size;

  if (stream == NULL || fseek(stream, 0, SEEK_END) != 0)
    {
      die("cannot open input");
    }

  size = ftell(stream);
  if (size < 0 || (size_t)size != expected_size ||
      fseek(stream, 0, SEEK_SET) != 0)
    {
      die("input size mismatch");
    }

  data = malloc(expected_size);
  if (data == NULL || fread(data, 1, expected_size, stream) != expected_size)
    {
      die("cannot read input");
    }

  fclose(stream);
  return data;
}

static void hash_init(void *context)
{
  struct host_hash_s *hash = context;

  hash->context = EVP_MD_CTX_new();
  if (hash->context == NULL ||
      EVP_DigestInit_ex(hash->context, EVP_sha256(), NULL) != 1)
    {
      die("OpenSSL SHA-256 init failed");
    }
}

static void hash_update(void *context, const uint8_t *data, size_t len)
{
  struct host_hash_s *hash = context;

  if (EVP_DigestUpdate(hash->context, data, len) != 1)
    {
      die("OpenSSL SHA-256 update failed");
    }
}

static void hash_final(void *context, uint8_t digest[32])
{
  struct host_hash_s *hash = context;
  unsigned int length = 0;

  if (EVP_DigestFinal_ex(hash->context, digest, &length) != 1 || length != 32)
    {
      die("OpenSSL SHA-256 final failed");
    }

  EVP_MD_CTX_free(hash->context);
  hash->context = NULL;
}

static bool read_range(uint32_t address, size_t len, uint32_t start,
                       uint32_t size)
{
  return address >= start && address - start <= size &&
         len <= size - (address - start);
}

static int raw_read(void *arg, uint32_t address, uint8_t *buffer, size_t len)
{
  struct host_context_s *context = arg;
  const uint8_t *source;
  const char *failure;
  uint32_t start;
  unsigned int index;

  if (read_range(address, len, SLOT_A, SLOT_SIZE))
    {
      source = context->slot[0];
      start = SLOT_A;
      index = 0;
      failure = "read-error-slot-a";
      context->slot_reads[index]++;
    }
  else if (read_range(address, len, SLOT_B, SLOT_SIZE))
    {
      source = context->slot[1];
      start = SLOT_B;
      index = 1;
      failure = "read-error-slot-b";
      context->slot_reads[index]++;
    }
  else if (read_range(address, len, BANK0,
                      BK7258_BOOT_OTA_ROTATION_BANK_SIZE))
    {
      source = context->bank[0];
      start = BANK0;
      index = 0;
      failure = "read-error-bank0";
      context->bank_reads[index]++;
    }
  else if (read_range(address, len, BANK1,
                      BK7258_BOOT_OTA_ROTATION_BANK_SIZE))
    {
      source = context->bank[1];
      start = BANK1;
      index = 1;
      failure = "read-error-bank1";
      context->bank_reads[index]++;
    }
  else
    {
      context->outside_access = true;
      return -EINVAL;
    }

  if (!context->injected && strcmp(context->mode, failure) == 0)
    {
      context->injected = true;
      return -EIO;
    }

  memcpy(buffer, source + address - start, len);
  if (!context->injected && strcmp(context->mode, "short-read") == 0)
    {
      context->injected = true;
      return 1;
    }

  return 0;
}

static bool parse_bool(const char *value)
{
  if (strcmp(value, "0") == 0)
    {
      return false;
    }

  if (strcmp(value, "1") != 0)
    {
      die("boolean argument must be 0 or 1");
    }

  return true;
}

int main(int argc, char **argv)
{
  struct bk7258_ota_hash_ops_s hash_ops =
  {
    .context_size = sizeof(struct host_hash_s),
    .init = hash_init,
    .update = hash_update,
    .final = hash_final
  };
  struct bk7258_boot_ota_raw_ops_s raw_ops;
  struct bk7258_boot_ota_rotation_result_s result;
  struct host_context_s context;
  uint8_t *bank_workspace;
  uint8_t *scratch;
  bool expect_error;
  int ret;

  if (argc != 15)
    {
      die("usage: harness bank0 bank1 slot_a slot_b ok|error decision reason "
          "boot_slot metadata_valid degraded base_verified target_verified "
          "trial_required mode");
    }

  memset(&context, 0, sizeof(context));
  context.mode = argv[14];
  context.bank[0] = read_file(argv[1],
                              BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  context.bank[1] = read_file(argv[2],
                              BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  context.slot[0] = read_file(argv[3], SLOT_SIZE);
  context.slot[1] = read_file(argv[4], SLOT_SIZE);
  bank_workspace = malloc(BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  scratch = malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (bank_workspace == NULL || scratch == NULL)
    {
      die("workspace allocation failed");
    }

  if (strcmp(argv[5], "ok") == 0)
    {
      expect_error = false;
    }
  else if (strcmp(argv[5], "error") == 0)
    {
      expect_error = true;
    }
  else
    {
      die("return expectation must be ok or error");
    }

  raw_ops.arg = &context;
  raw_ops.read = raw_read;
  ret = bk7258_boot_ota_rotation_select_core(
    &raw_ops, &hash_ops, bank_workspace, scratch,
    BK7258_OTA_STAGE_SCRATCH_SIZE, &result);
  if ((expect_error && ret >= 0) || (!expect_error && ret != 0) ||
      result.status != ret || context.outside_access ||
      result.decision !=
        (enum bk7258_boot_ota_rotation_decision_e)strtol(argv[6], NULL, 0) ||
      result.reason !=
        (enum bk7258_boot_ota_rotation_reason_e)strtol(argv[7], NULL, 0) ||
      result.boot_slot !=
        (enum bk7258_boot_ota_slot_e)strtol(argv[8], NULL, 0) ||
      result.metadata_valid != parse_bool(argv[9]) ||
      result.metadata_degraded != parse_bool(argv[10]) ||
      result.base_verified != parse_bool(argv[11]) ||
      result.target_verified != parse_bool(argv[12]))
    {
      fprintf(stderr,
              "observed ret=%d status=%d decision=%d reason=%d slot=%d "
              "metadata=%u degraded=%u base=%u target=%u trial=%u "
              "outside=%u\n",
              ret, result.status, (int)result.decision, (int)result.reason,
              (int)result.boot_slot, result.metadata_valid ? 1u : 0u,
              result.metadata_degraded ? 1u : 0u,
              result.base_verified ? 1u : 0u,
              result.target_verified ? 1u : 0u,
              result.trial_required ? 1u : 0u,
              context.outside_access ? 1u : 0u);
      die("selector result invariant failed");
    }

  if (result.trial_required != parse_bool(argv[13]))
    {
      die("trial permission invariant failed");
    }

  printf("BK7258 format-2 selector harness PASS status=%d decision=%d "
         "reason=%d slot=%d generation=%llu bank=%u reads=%u/%u/%u/%u\n",
         ret, (int)result.decision, (int)result.reason,
         (int)result.boot_slot, (unsigned long long)result.generation,
         result.selected_bank, context.bank_reads[0], context.bank_reads[1],
         context.slot_reads[0], context.slot_reads[1]);

  free(scratch);
  free(bank_workspace);
  free(context.slot[1]);
  free(context.slot[0]);
  free(context.bank[1]);
  free(context.bank[0]);
  return 0;
}
