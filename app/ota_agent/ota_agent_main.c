/****************************************************************************
 * contest2026_135_yongwangzhiqian/app/ota_agent/ota_agent_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/chip/bk7258_ota_manager.h>
#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
#  include <arch/chip/bk7258_ota_source_file.h>
#endif
#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
#  include <arch/chip/bk7258_ota_source_http.h>
#endif

static int ota_status(void)
{
  struct bk7258_ota_manager_status_s status;
  int ret = bk7258_ota_manager_get_status(&status);

  if (ret < 0)
    {
      fprintf(stderr, "ota: status failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("state=%u phase=%u image=%u progress=%lu/%lu error=%ld\n",
         (unsigned int)status.state, (unsigned int)status.phase,
         (unsigned int)status.image, (unsigned long)status.completed,
         (unsigned long)status.total, (long)status.last_error);
  return EXIT_SUCCESS;
}

#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
static int ota_apply_file(const char *root)
{
  struct bk7258_ota_file_source_s source;
  int ret;

  ret = bk7258_ota_file_source_initialize(&source, root);
  if (ret == 0)
    {
      ret = bk7258_ota_manager_apply(
              bk7258_ota_file_source_ops(), &source,
              CONFIG_BK7258_APP_OTA_AGENT_TIMEOUT_MS);
    }
  if (ret < 0)
    {
      fprintf(stderr, "ota: apply-file failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("ota: inactive CP/AP pair verified and ready to reboot\n");
  return EXIT_SUCCESS;
}
#endif

#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
static int ota_apply_http(const char *catalog_url, const char *ca_path)
{
  struct bk7258_ota_http_source_s source = {0};
  int ret;

  ret = bk7258_ota_http_source_initialize(&source, catalog_url, ca_path);
  if (ret == 0)
    {
      ret = bk7258_ota_manager_apply(
              bk7258_ota_http_source_ops(), &source,
              CONFIG_BK7258_APP_OTA_AGENT_TIMEOUT_MS);
    }
  if (ret < 0)
    {
      fprintf(stderr, "ota: apply-http failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  printf("ota: HTTPS pair verified and ready to reboot\n");
  return EXIT_SUCCESS;
}
#endif

int main(int argc, char *argv[])
{
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      return ota_status();
    }
  if (argc == 2 && strcmp(argv[1], "cancel") == 0)
    {
      ret = bk7258_ota_manager_cancel();
      if (ret < 0)
        {
          fprintf(stderr, "ota: cancel failed: %d\n", ret);
          return EXIT_FAILURE;
        }
      return EXIT_SUCCESS;
    }
#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
  if (argc == 3 && strcmp(argv[1], "apply-file") == 0)
    {
      return ota_apply_file(argv[2]);
    }
#endif
#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
  if (argc == 4 && strcmp(argv[1], "apply-http") == 0)
    {
      return ota_apply_http(argv[2], argv[3]);
    }
#endif

  fprintf(stderr, "usage: ota status | ota cancel"
#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
                  " | ota apply-file <verified-package-directory>"
#endif
#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP
                  " | ota apply-http <catalog-url> <ca-pem>"
#endif
                  "\n");
  return EXIT_FAILURE;
}
