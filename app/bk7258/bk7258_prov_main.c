/****************************************************************************
 * app/bk7258/bk7258_prov_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible operator command for the per-device identity supply channel.
 * It only receives one bounded BPI1 record over the console and forwards it
 * to the AP-owned store; it never prints certificate or key material.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_PROV

#include "bk7258_prov_rpc.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define BKPROV_SUPPLY_CHUNK_BYTES 64u

static uint8_t g_bkprov_record[BKPROV_RPC_RECORD_MAX];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bkprov_wipe(void *buffer, size_t size)
{
  volatile uint8_t *cursor = buffer;

  while (size-- > 0)
    {
      *cursor++ = 0;
    }
}

static int bkprov_read_line(char *line, size_t size)
{
  size_t length = 0;

  for (; ; )
    {
      uint8_t byte;
      ssize_t got = read(STDIN_FILENO, &byte, 1);

      if (got < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      if (got == 0)
        {
          return -ENODATA;
        }

      if (byte == '\n' || byte == '\r')
        {
          line[length] = '\0';
          return (int)length;
        }

      if (byte < 0x20u || byte == 0x7fu ||
          (byte >= 0x80u && byte <= 0x9fu) || length + 1u >= size)
        {
          return -EINVAL;
        }

      line[length++] = (char)byte;
    }
}

static bool bkprov_hex(const char *text, size_t bytes)
{
  size_t index;

  for (index = 0; index < bytes * 2u; index++)
    {
      char value = text[index];

      if (!((value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F')))
        {
          return false;
        }
    }

  return text[bytes * 2u] == '\0';
}

static int bkprov_parse_range(const char *text, unsigned long minimum,
                              unsigned long maximum, unsigned long *value)
{
  char *end = NULL;
  unsigned long parsed;

  if (text == NULL || value == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno != 0 || end == NULL || *end != '\0' ||
      parsed < minimum || parsed > maximum)
    {
      return -EINVAL;
    }

  *value = parsed;
  return 0;
}

static int bkprov_unhex(const char *text, uint8_t *target, size_t bytes)
{
  size_t index;

  if (!bkprov_hex(text, bytes))
    {
      return -EINVAL;
    }

  for (index = 0; index < bytes; index++)
    {
      char pair[3];
      unsigned long value;

      pair[0] = text[index * 2u];
      pair[1] = text[index * 2u + 1u];
      pair[2] = '\0';
      value = strtoul(pair, NULL, 16);
      target[index] = (uint8_t)value;
    }

  return 0;
}

static int bkprov_supply(void)
{
  struct termios original;
  struct termios hidden;
  char line[BKPROV_SUPPLY_CHUNK_BYTES * 2u + 1u];
  unsigned long total = 0;
  size_t offset = 0;
  bool termios_changed = false;
  int ret;

  memset(line, 0, sizeof(line));
  memset(g_bkprov_record, 0, sizeof(g_bkprov_record));

  if (tcgetattr(STDIN_FILENO, &original) < 0)
    {
      return -ENOTTY;
    }

  hidden = original;
  hidden.c_lflag &= ~(ECHO | ECHONL);
  if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) < 0)
    {
      return errno > 0 ? -errno : -EIO;
    }

  termios_changed = true;
  (void)tcflush(STDIN_FILENO, TCIFLUSH);
  printf("BKPROV SUPPLY READY\n");
  fflush(stdout);

  ret = bkprov_read_line(line, sizeof(line));
  if (ret < 0 || bkprov_parse_range(line, BKPROV_RPC_RECORD_MIN,
                                    BKPROV_RPC_RECORD_MAX, &total) < 0)
    {
      ret = ret < 0 ? ret : -EINVAL;
      goto out;
    }

  total = (unsigned long)(uint16_t)total;
  printf("BKPROV SUPPLY NEXT offset=0\n");
  fflush(stdout);

  while (offset < total)
    {
      size_t bytes = (size_t)total - offset;

      if (bytes > BKPROV_SUPPLY_CHUNK_BYTES)
        {
          bytes = BKPROV_SUPPLY_CHUNK_BYTES;
        }

      memset(line, 0, sizeof(line));
      ret = bkprov_read_line(line, sizeof(line));
      if (ret < 0 ||
          bkprov_unhex(line, g_bkprov_record + offset, bytes) < 0)
        {
          ret = ret < 0 ? ret : -EINVAL;
          goto out;
        }

      offset += bytes;
      printf("BKPROV SUPPLY NEXT offset=%lu\n", (unsigned long)offset);
      fflush(stdout);
    }

  ret = bkprov_rpc_supply(g_bkprov_record, (size_t)total);

out:
  if (termios_changed)
    {
      if (ret < 0)
        {
          (void)tcflush(STDIN_FILENO, TCIFLUSH);
        }

      if (tcsetattr(STDIN_FILENO, TCSANOW, &original) < 0 && ret >= 0)
        {
          ret = errno > 0 ? -errno : -EIO;
        }

      if (ret < 0)
        {
          (void)tcflush(STDIN_FILENO, TCIFLUSH);
        }
    }

  bkprov_wipe(line, sizeof(line));
  bkprov_wipe(g_bkprov_record, sizeof(g_bkprov_record));
  if (ret < 0)
    {
      fprintf(stderr, "BKPROV SUPPLY FAIL ret=%d\n", ret);
    }
  else
    {
      printf("BKPROV SUPPLY PASS bytes=%lu\n", total);
    }

  return ret;
}

static int bkprov_status(void)
{
  size_t size = 0;
  int ret = bkprov_rpc_status(&size);

  if (ret == 0)
    {
      printf("BKPROV STATUS identity=present bytes=%lu\n",
             (unsigned long)size);
    }
  else if (ret == -ENOENT)
    {
      printf("BKPROV STATUS identity=absent ret=%d\n", ret);
    }
  else
    {
      fprintf(stderr, "BKPROV STATUS FAIL ret=%d\n", ret);
    }

  return ret;
}

#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS
static int bkprov_diagnostics_enable(void)
{
  struct termios original;
  struct termios hidden;
  uint8_t record[BKPROV_RPC_DIAGNOSTICS_RECORD_SIZE];
  uint8_t certificate[BKPROV_RPC_DIAGNOSTICS_CERT_SIZE];
  char line[BKPROV_RPC_DIAGNOSTICS_RECORD_SIZE * 2u + 1u];
  uint32_t flags = 0;
  uint32_t remaining_ms = 0;
  bool termios_changed = false;
  int ret;

  memset(record, 0, sizeof(record));
  memset(certificate, 0, sizeof(certificate));
  memset(line, 0, sizeof(line));
  if (tcgetattr(STDIN_FILENO, &original) < 0)
    {
      ret = -ENOTTY;
      goto out;
    }

  hidden = original;
  hidden.c_lflag &= ~(ECHO | ECHONL);
  if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) < 0)
    {
      ret = errno > 0 ? -errno : -EIO;
      goto out;
    }

  termios_changed = true;
  (void)tcflush(STDIN_FILENO, TCIFLUSH);
  printf("BKPROV DIAGNOSTICS READY bytes=%u\n",
         BKPROV_RPC_DIAGNOSTICS_RECORD_SIZE);
  fflush(stdout);
  ret = bkprov_read_line(line, sizeof(line));
  if (ret < 0 || bkprov_unhex(line, record, sizeof(record)) < 0)
    {
      ret = ret < 0 ? ret : -EINVAL;
      goto restore;
    }

  ret = bkprov_rpc_diagnostics_enable(record, &flags, &remaining_ms);

restore:
  if (termios_changed)
    {
      (void)tcflush(STDIN_FILENO, TCIFLUSH);
      if (tcsetattr(STDIN_FILENO, TCSANOW, &original) < 0 && ret >= 0)
        {
          ret = errno > 0 ? -errno : -EIO;
        }
    }

  if (ret == 0)
    {
      for (uint32_t offset = 0; offset < sizeof(certificate); offset += 4)
        {
          uint32_t word = 0;

          ret = bkprov_rpc_diagnostics_certificate(offset, &word);
          if (ret < 0)
            {
              break;
            }

          certificate[offset] = word >> 24;
          certificate[offset + 1] = word >> 16;
          certificate[offset + 2] = word >> 8;
          certificate[offset + 3] = word;
        }
    }

  if (ret < 0)
    {
      (void)bkprov_rpc_diagnostics_revoke();
    }

out:
  bkprov_wipe(line, sizeof(line));
  bkprov_wipe(record, sizeof(record));
  if (ret < 0)
    {
      fprintf(stderr, "BKPROV DIAGNOSTICS FAIL ret=%d\n", ret);
    }
  else
    {
      printf("BKPROV DIAGNOSTICS PASS flags=%lu ttl_ms=%lu certificate_sha256=",
             (unsigned long)flags, (unsigned long)remaining_ms);
      for (size_t i = 0; i < sizeof(certificate); i++)
        {
          printf("%02x", certificate[i]);
        }

      printf("\n");
    }

  bkprov_wipe(certificate, sizeof(certificate));
  return ret;
}

static int bkprov_diagnostics_status(void)
{
  uint32_t flags = 0;
  uint32_t remaining_ms = 0;
  int ret = bkprov_rpc_diagnostics_status(&flags, &remaining_ms);

  printf("BKPROV DIAGNOSTICS STATUS flags=%lu ttl_ms=%lu ret=%d\n",
         (unsigned long)flags, (unsigned long)remaining_ms, ret);
  return ret;
}

static int bkprov_diagnostics_revoke(void)
{
  int ret = bkprov_rpc_diagnostics_revoke();

  if (ret < 0)
    {
      fprintf(stderr, "BKPROV DIAGNOSTICS REVOKE FAIL ret=%d\n", ret);
    }
  else
    {
      printf("BKPROV DIAGNOSTICS REVOKE PASS\n");
    }

  return ret;
}

#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "supply") == 0)
    {
      return bkprov_supply() < 0 ? 1 : 0;
    }

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return bkprov_status() < 0 ? 1 : 0;
    }

#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS
  if (argc == 2 && strcmp(argv[1], "diagnostics-enable") == 0)
    {
      return bkprov_diagnostics_enable() < 0 ? 1 : 0;
    }

  if (argc == 2 && strcmp(argv[1], "diagnostics-status") == 0)
    {
      return bkprov_diagnostics_status() < 0 ? 1 : 0;
    }

  if (argc == 2 && strcmp(argv[1], "diagnostics-revoke") == 0)
    {
      return bkprov_diagnostics_revoke() < 0 ? 1 : 0;
    }

#endif

  fprintf(stderr,
          "usage: %s supply\n"
          "       %s status\n"
#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS
          "       %s diagnostics-enable\n"
          "       %s diagnostics-status\n"
          "       %s diagnostics-revoke\n"
#endif
          "supply accepts one bounded BPI1 device identity record from the\n"
          "operator console; status is read-only.\n",
          argv[0], argv[0]
#ifdef CONFIG_BK7258_FACTORY_DIAGNOSTICS
          , argv[0], argv[0], argv[0]
#endif
          );
  return 1;
}

#endif /* CONFIG_BK7258_APP_PROV */
