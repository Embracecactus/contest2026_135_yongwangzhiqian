/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/hello_app/bk7258_wifi_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <arch/chip/bk7258_wifi.h>

#define BKWIFI_IPV4_TEXT_LEN 16

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bkwifi_u32(const char *text, uint32_t *value)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  parsed = strtoul(text, &end, 0);
  if (errno != 0 || text[0] == '\0' || *end != '\0' ||
      parsed > UINT32_MAX)
    {
      return -EINVAL;
    }

  *value = (uint32_t)parsed;
  return OK;
}

static int bkwifi_ipv4(const char *text, uint32_t *address)
{
  uint8_t octets[4];
  const char *cursor = text;
  unsigned int i;

  for (i = 0; i < 4; i++)
    {
      char *end;
      unsigned long value;

      errno = 0;
      value = strtoul(cursor, &end, 10);
      if (errno != 0 || end == cursor || value > UINT8_MAX ||
          (i < 3 && *end != '.') || (i == 3 && *end != '\0'))
        {
          return -EINVAL;
        }

      octets[i] = (uint8_t)value;
      cursor = end + (i < 3 ? 1 : 0);
    }

  memcpy(address, octets, sizeof(octets));
  if (*address == 0 || *address == UINT32_MAX)
    {
      return -EINVAL;
    }

  return OK;
}

static int bkwifi_read_hidden(const char *prompt, char *buffer,
                              size_t capacity, bool allow_empty)
{
  struct termios original;
  struct termios hidden;
  size_t length;
  bool complete;
  int ret;

  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &original) < 0)
    {
      return -ENOTTY;
    }

  hidden = original;
  hidden.c_lflag &= ~(ECHO | ECHONL);
  if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) < 0)
    {
      return -errno;
    }

  /* NSH consumes the command-ending carriage return but can leave the
   * paired line-feed pending on the shared console.  Discard pending input
   * before displaying the prompt so it cannot be mistaken for an empty
   * credential.  The operator enters credentials only after the prompt.
   */

  (void)tcflush(STDIN_FILENO, TCIFLUSH);

  printf("%s", prompt);
  fflush(stdout);
  errno = 0;
  if (fgets(buffer, capacity, stdin) == NULL)
    {
      ret = errno != 0 ? -errno : -EIO;
      (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
      printf("\n");
      return ret;
    }

  (void)tcsetattr(STDIN_FILENO, TCSANOW, &original);
  printf("\n");

  length = strlen(buffer);
  complete = length > 0 &&
             (buffer[length - 1] == '\n' || buffer[length - 1] == '\r');
  while (length > 0 &&
         (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
    {
      buffer[--length] = '\0';
    }

  if (!complete && !feof(stdin))
    {
      int ch;

      do
        {
          ch = getchar();
        }
      while (ch != '\n' && ch != EOF);

      return -E2BIG;
    }

  return length > 0 || allow_empty ? OK : -EINVAL;
}

static void bkwifi_usage(void)
{
  printf("usage:\n"
         "  bkwifi connect [timeout_ms=%u]\n"
         "  bkwifi connect-args <ssid> <password> [timeout_ms=%u]\n"
         "  bkwifi status  [timeout_ms=%u]\n"
         "  bkwifi ping    [timeout_ms=%u]\n"
         "  bkwifi tcp <ipv4> <port> [count=%u] [size=%u] [timeout_ms=%u]\n"
         "  bkwifi udp <ipv4> <port> [count=%u] [size=%u] [timeout_ms=%u]\n"
         "SSID and password are read with terminal echo disabled.\n",
         BK7258_WIFI_CONNECT_DEFAULT_MS,
         BK7258_WIFI_CONNECT_DEFAULT_MS,
         BK7258_WIFI_CONNECT_DEFAULT_MS,
         BK7258_WIFI_CONNECT_DEFAULT_MS,
         BK7258_WIFI_ECHO_COUNT_DEFAULT,
         BK7258_WIFI_ECHO_SIZE_DEFAULT,
         BK7258_WIFI_ECHO_DEFAULT_MS,
         BK7258_WIFI_ECHO_COUNT_DEFAULT,
         BK7258_WIFI_ECHO_SIZE_DEFAULT,
         BK7258_WIFI_ECHO_DEFAULT_MS);
}

static void bkwifi_format_ipv4(uint32_t address, char *buffer,
                               size_t length)
{
  const uint8_t *octet = (const uint8_t *)&address;

  snprintf(buffer, length, "%u.%u.%u.%u",
           octet[0], octet[1], octet[2], octet[3]);
}

static void bkwifi_print_result(enum bk7258_wifi_operation_e operation,
                                int ret,
                                const struct bk7258_wifi_result_s *result,
                                const struct bk7258_wifi_echo_s *echo)
{
  char ip[BKWIFI_IPV4_TEXT_LEN];
  char mask[BKWIFI_IPV4_TEXT_LEN];
  char router[BKWIFI_IPV4_TEXT_LEN];
  const char *name;

  switch (operation)
    {
      case BK7258_WIFI_OPERATION_CONNECT:
        name = "connect";
        break;
      case BK7258_WIFI_OPERATION_PING:
        name = "ping";
        break;
      case BK7258_WIFI_OPERATION_TCP_ECHO:
        name = "tcp";
        break;
      case BK7258_WIFI_OPERATION_UDP_ECHO:
        name = "udp";
        break;
      default:
        name = "status";
        break;
    }

  bkwifi_format_ipv4(result->ipaddr, ip, sizeof(ip));
  bkwifi_format_ipv4(result->netmask, mask, sizeof(mask));
  bkwifi_format_ipv4(result->router, router, sizeof(router));

  printf("BKWIFI RESULT operation=%s status=%d link=%" PRIu32
         " rssi=%" PRId32 " ip=%s mask=%s router=%s\n",
         name, ret, result->link_state, result->rssi,
         ip, mask, router);

  if (echo != NULL)
    {
      char peer[BKWIFI_IPV4_TEXT_LEN];

      bkwifi_format_ipv4(echo->address, peer, sizeof(peer));
      printf("BKWIFI ECHO protocol=%s peer=%s port=%" PRIu32
             " requested_count=%" PRIu32 " completed_count=%" PRIu32
             " size=%" PRIu32 " verified_bytes=%" PRIu32 "\n",
             name, peer, echo->port, echo->count, result->echo_count,
             echo->size, result->echo_bytes);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  struct bk7258_wifi_result_s result;
  struct bk7258_wifi_echo_s echo;
  const struct bk7258_wifi_echo_s *echo_arg = NULL;
  enum bk7258_wifi_operation_e operation;
  char ssid[BK7258_WIFI_SSID_MAX_LEN + 2u];
  char password[BK7258_WIFI_PASSWORD_MAX_LEN + 2u];
  uint32_t timeout_ms = BK7258_WIFI_CONNECT_DEFAULT_MS;
  bool direct_credentials = false;
  int ret;

  memset(ssid, 0, sizeof(ssid));
  memset(password, 0, sizeof(password));
  memset(&echo, 0, sizeof(echo));
  memset(&result, 0, sizeof(result));

  if (argc < 2)
    {
      bkwifi_usage();
      return EXIT_FAILURE;
    }

  if (strcmp(argv[1], "connect") == 0)
    {
      operation = BK7258_WIFI_OPERATION_CONNECT;
    }
  else if (strcmp(argv[1], "connect-args") == 0)
    {
      operation = BK7258_WIFI_OPERATION_CONNECT;
      direct_credentials = true;
    }
  else if (strcmp(argv[1], "status") == 0)
    {
      operation = BK7258_WIFI_OPERATION_STATUS;
    }
  else if (strcmp(argv[1], "ping") == 0)
    {
      operation = BK7258_WIFI_OPERATION_PING;
    }
  else if (strcmp(argv[1], "tcp") == 0)
    {
      operation = BK7258_WIFI_OPERATION_TCP_ECHO;
    }
  else if (strcmp(argv[1], "udp") == 0)
    {
      operation = BK7258_WIFI_OPERATION_UDP_ECHO;
    }
  else
    {
      bkwifi_usage();
      return EXIT_FAILURE;
    }

  if (operation == BK7258_WIFI_OPERATION_TCP_ECHO ||
      operation == BK7258_WIFI_OPERATION_UDP_ECHO)
    {
      uint32_t value;

      if (argc < 4 || argc > 7 ||
          bkwifi_ipv4(argv[2], &echo.address) < 0 ||
          bkwifi_u32(argv[3], &echo.port) < 0 ||
          echo.port == 0 || echo.port > UINT16_MAX)
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }

      echo.count = BK7258_WIFI_ECHO_COUNT_DEFAULT;
      echo.size = BK7258_WIFI_ECHO_SIZE_DEFAULT;
      timeout_ms = BK7258_WIFI_ECHO_DEFAULT_MS;
      if (argc >= 5 && bkwifi_u32(argv[4], &echo.count) < 0)
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }

      if (argc >= 6 && bkwifi_u32(argv[5], &echo.size) < 0)
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }

      if (argc == 7 && bkwifi_u32(argv[6], &value) < 0)
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }
      else if (argc == 7)
        {
          timeout_ms = value;
        }

      if (echo.count == 0 || echo.count > BK7258_WIFI_ECHO_COUNT_MAX ||
          echo.size == 0 || echo.size > BK7258_WIFI_ECHO_SIZE_MAX ||
          timeout_ms < BK7258_WIFI_ECHO_MIN_MS ||
          timeout_ms > BK7258_WIFI_CONNECT_MAX_MS)
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }

      echo_arg = &echo;
    }
  else if (direct_credentials)
    {
      size_t ssid_length;
      size_t password_length;

      if (argc < 4 || argc > 5 ||
          (argc == 5 && bkwifi_u32(argv[4], &timeout_ms) < 0))
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }

      ssid_length = strnlen(argv[2], sizeof(ssid));
      password_length = strnlen(argv[3], sizeof(password));
      if (ssid_length == 0u || ssid_length >= sizeof(ssid) ||
          password_length >= sizeof(password))
        {
          bkwifi_usage();
          return EXIT_FAILURE;
        }

      memcpy(ssid, argv[2], ssid_length + 1u);
      memcpy(password, argv[3], password_length + 1u);
    }
  else if (argc > 3 ||
           (argc == 3 && bkwifi_u32(argv[2], &timeout_ms) < 0))
    {
      bkwifi_usage();
      return EXIT_FAILURE;
    }

  if (timeout_ms <
      (echo_arg != NULL ? BK7258_WIFI_ECHO_MIN_MS :
                          BK7258_WIFI_CONNECT_MIN_MS) ||
      timeout_ms > BK7258_WIFI_CONNECT_MAX_MS)
    {
      bkwifi_usage();
      return EXIT_FAILURE;
    }

  if (operation == BK7258_WIFI_OPERATION_CONNECT && !direct_credentials)
    {
      ret = bkwifi_read_hidden("SSID: ", ssid, sizeof(ssid), false);
      if (ret >= 0)
        {
          ret = bkwifi_read_hidden("Password (empty for open network): ",
                                   password, sizeof(password), true);
        }

      if (ret < 0)
        {
          printf("BKWIFI FAIL input=%d\n", ret);
          explicit_bzero(ssid, sizeof(ssid));
          explicit_bzero(password, sizeof(password));
          return EXIT_FAILURE;
        }
    }

  ret = bk7258_wifi_control_request(
          operation,
          operation == BK7258_WIFI_OPERATION_CONNECT ? ssid : NULL,
          operation == BK7258_WIFI_OPERATION_CONNECT ? password : NULL,
          echo_arg,
          timeout_ms, &result);
  explicit_bzero(ssid, sizeof(ssid));
  explicit_bzero(password, sizeof(password));
  bkwifi_print_result(operation, ret, &result, echo_arg);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
