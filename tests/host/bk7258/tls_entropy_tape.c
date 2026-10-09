/* SPDX-License-Identifier: Apache-2.0 */
/* Host diagnostic ONLY. Records synthetic TLS randomness, never product keys.
 * Link wrappers are selected solely by the host test compiler command.
 * No environment switch can enable this code in a target firmware build.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#define TAPE_LIMIT (8u * 1024u * 1024u)
static FILE *tape;
static int mode;
static int initialized;
static size_t transferred;
int __real_mbedtls_ctr_drbg_random(void *, unsigned char *, size_t);
time_t __real_time(time_t *);

static void invalid(void)
{
  /* No payload/path is printed: the tape contains synthetic secret material. */
  fputs("TLS_TAPE_SETUP_ERROR\n", stderr);
  _Exit(86);
}

static void transfer(void *data, size_t size)
{
  if (size > TAPE_LIMIT - transferred) invalid();
  transferred += size;
  size_t count = mode == 1 ? fwrite(data, 1, size, tape) :
                            fread(data, 1, size, tape);
  if (count != size) invalid();
}

static void finish(void)
{
  if (mode == 2 && (fgetc(tape) != EOF || ferror(tape))) invalid();
  if (fclose(tape) != 0) invalid();
}

static void initialize(void)
{
  if (initialized) return;
  initialized = 1;
  const char *record = getenv("SHANIU_TLS_TAPE_RECORD");
  const char *replay = getenv("SHANIU_TLS_TAPE_REPLAY");
  if (record && replay) invalid();
  if (!record && !replay) return;
  mode = record ? 1 : 2;
  int fd = open(record ? record : replay,
                record ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY, 0600);
  if (fd < 0) invalid();
  tape = fdopen(fd, record ? "wb" : "rb");
  if (!tape || setvbuf(tape, NULL, _IONBF, 0) != 0) invalid();
  unsigned char magic[8] = {'T','L','S','R','N','G','1','\n'};
  unsigned char actual[8];
  memcpy(actual, magic, sizeof(actual));
  transfer(actual, sizeof(actual));
  if (memcmp(actual, magic, sizeof(actual))) invalid();
  if (atexit(finish) != 0) invalid();
}

static int event(unsigned char type, size_t size, int result)
{
  unsigned char header[9];
  if (size > TAPE_LIMIT) invalid();
  header[0] = type;
  for (unsigned int i = 0; i < 4; i++)
    {
      header[i + 1] = (unsigned char)((uint32_t)size >> (8 * i));
      header[i + 5] = (unsigned char)((uint32_t)result >> (8 * i));
    }
  transfer(header, sizeof(header));
  uint32_t length = 0;
  uint32_t code = 0;
  for (unsigned int i = 0; i < 4; i++)
    {
      length |= (uint32_t)header[i + 1] << (8 * i);
      code |= (uint32_t)header[i + 5] << (8 * i);
    }
  if (header[0] != type || length != size) invalid();
  return (int32_t)code;
}

int __wrap_mbedtls_ctr_drbg_random(void *context, unsigned char *output,
                                  size_t size)
{
  initialize();
  if (!mode) return __real_mbedtls_ctr_drbg_random(context, output, size);
  int result = mode == 1 ?
    __real_mbedtls_ctr_drbg_random(context, output, size) : 0;
  result = event('R', size, result);
  if (result == 0) transfer(output, size);
  return result;
}

time_t __wrap_time(time_t *destination)
{
  initialize();
  if (!mode) return __real_time(destination);
  uint64_t value = mode == 1 ? (uint64_t)__real_time(NULL) : 0;
  unsigned char bytes[8];
  for (unsigned int i = 0; i < 8; i++)
    bytes[i] = (unsigned char)(value >> (8 * i));
  if (event('T', sizeof(bytes), 0) != 0) invalid();
  transfer(bytes, sizeof(bytes));
  value = 0;
  for (unsigned int i = 0; i < 8; i++) value |= (uint64_t)bytes[i] << (8 * i);
  time_t result = (time_t)(int64_t)value;
  if (destination) *destination = result;
  return result;
}
