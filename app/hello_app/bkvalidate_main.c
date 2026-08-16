/****************************************************************************
 * app/hello_app/bkvalidate_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small public-API validation dispatcher.  Descriptor policy is checked by
 * board/bk7258/scripts/bk7258_validation.py; this target-side dispatcher only
 * uses public device paths or versioned diagnostic records, serializes
 * resource claims, and emits stable JSON outcomes.  It has no vendor SDK
 * calls and never starts validation from peripheral composition.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_JPEG_M2M_VALIDATION)
#  include <arch/chip/bk7258_jpeg_m2m_validation.h>
#  define BKVALIDATE_JPEG_START bk7258_jpeg_m2m_validation_start
#  define BKVALIDATE_JPEG_WAIT bkvalidate_wait_jpeg
#else
#  define BKVALIDATE_JPEG_START NULL
#  define BKVALIDATE_JPEG_WAIT NULL
#endif

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_TEMPERATURE_VALIDATION)
#  include <arch/chip/bk7258_temperature.h>
#  define BKVALIDATE_TEMPERATURE_START bk7258_temperature_validation_start
#  define BKVALIDATE_TEMPERATURE_WAIT bkvalidate_wait_temperature
#else
#  define BKVALIDATE_TEMPERATURE_START NULL
#  define BKVALIDATE_TEMPERATURE_WAIT NULL
#endif

#define BKVALIDATE_POLL_US 10000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bkvalidate_descriptor_s
{
  const char *id;
  const char *role;
  const char *requirement;
  const char *category;
  unsigned int timeout;
  const char *prepare;
  const char *run;
  const char *cancel;
  const char *cleanup;
  const char *status;
  const char *resource_claim;
  bool command_driven;
  int (*start)(void);
  int (*wait)(unsigned int timeout_ms);
};

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_JPEG_M2M_VALIDATION)
static int bkvalidate_wait_jpeg(unsigned int timeout_ms)
{
  unsigned int attempts = (timeout_ms + 9u) / 10u;

  while (attempts-- > 0)
    {
      uint32_t state;

      if (g_bk7258_jpeg_m2m_validation_diag.magic !=
            BK7258_JPEG_M2M_VALIDATION_MAGIC ||
          g_bk7258_jpeg_m2m_validation_diag.version !=
            BK7258_JPEG_M2M_VALIDATION_VERSION ||
          g_bk7258_jpeg_m2m_validation_diag.size !=
            sizeof(g_bk7258_jpeg_m2m_validation_diag))
        {
          return -EPROTO;
        }

      state = __atomic_load_n(
        &g_bk7258_jpeg_m2m_validation_diag.state, __ATOMIC_ACQUIRE);
      if (state == BK7258_JPEG_M2M_VALIDATION_PASSED)
        {
          return 0;
        }

      if (state == BK7258_JPEG_M2M_VALIDATION_FAILED)
        {
          return g_bk7258_jpeg_m2m_validation_diag.result < 0 ?
                 g_bk7258_jpeg_m2m_validation_diag.result : -EIO;
        }

      usleep(BKVALIDATE_POLL_US);
    }

  return -ETIMEDOUT;
}
#endif

#if defined(CONFIG_BK7258_AP_CORE) && \
    defined(CONFIG_BK7258_TEMPERATURE_VALIDATION)
static int bkvalidate_wait_temperature(unsigned int timeout_ms)
{
  unsigned int attempts = (timeout_ms + 9u) / 10u;

  while (attempts-- > 0)
    {
      uint16_t state;

      if (g_bk7258_temperature_validation_diag.magic !=
            BK7258_TEMPERATURE_VALIDATION_MAGIC ||
          g_bk7258_temperature_validation_diag.version !=
            BK7258_TEMPERATURE_VALIDATION_VERSION)
        {
          return -EPROTO;
        }

      state = __atomic_load_n(
        &g_bk7258_temperature_validation_diag.state, __ATOMIC_ACQUIRE);
      if (state == BK7258_TEMPERATURE_VALIDATION_PASSED)
        {
          return 0;
        }

      if (state == BK7258_TEMPERATURE_VALIDATION_FAILED)
        {
          return g_bk7258_temperature_validation_diag.status < 0 ?
                 g_bk7258_temperature_validation_diag.status : -EIO;
        }

      usleep(BKVALIDATE_POLL_US);
    }

  return -ETIMEDOUT;
}
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bkvalidate_descriptor_s g_descriptors[] =
{
  {
    "rptun_public_api_smoke", "cp_ap", "devpath:/dev/rptun", "auto",
    30000, "none", "public_api:open-close", "standard-timeout",
    "public_api:close", "ready", "rptun_transport", false, NULL, NULL
  },
  {
    "wifi_public_api_smoke", "cp_ap", "devpath:/dev/wlan0", "auto",
    30000, "none", "public_api:open-close", "standard-timeout",
    "public_api:close", "planned", "wifi_control", false, NULL, NULL
  },
  {
    "gpio_interactive", "board", "operator:pin-observation", "interactive",
    60000, "operator-confirm", "public_api:gpio-observe", "operator-cancel",
    "public_api:close", "ready", "board_gpio", false, NULL, NULL
  },
  {
    "jpeg_fixture", "ap", "fixture:jpeg-baseline", "fixture",
    30000, "none", "public_api:jpeg-m2m-validation", "none",
    "none", "ready", "jpeg_engine", true,
    BKVALIDATE_JPEG_START, BKVALIDATE_JPEG_WAIT
  },
  {
    "temperature_validation", "ap", "fixture:temperature-rpmsg", "fixture",
    30000, "none", "public_api:temperature-validation", "none",
    "none", "ready", "temperature_sensor", true,
    BKVALIDATE_TEMPERATURE_START, BKVALIDATE_TEMPERATURE_WAIT
  },
  {
    "power_fault_recovery", "cp_ap", "fault:power-cycle", "destructive-fault",
    120000, "fault-authorization", "public_api:recovery-observe",
    "standard-timeout", "public_api:close", "ready", "pm_cross_core", false,
    NULL, NULL
  },
};

static const char *g_active_claim;

#define BKVALIDATE_DESCRIPTOR_COUNT \
  (sizeof(g_descriptors) / sizeof(g_descriptors[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const struct bkvalidate_descriptor_s *bkvalidate_find(const char *id)
{
  unsigned int index;

  for (index = 0; index < BKVALIDATE_DESCRIPTOR_COUNT; index++)
    {
      if (strcmp(g_descriptors[index].id, id) == 0)
        {
          return &g_descriptors[index];
        }
    }

  return NULL;
}

static bool bkvalidate_requirement_available(const char *requirement)
{
  const char *path;
  int fd;

  if (strncmp(requirement, "devpath:", 8) != 0)
    {
      /* Fixture requirements are validated by the bounded command itself.
       * They deliberately do not probe or initialize production devices from
       * the dispatcher.
       */

      return strncmp(requirement, "fixture:", 8) == 0;
    }

  path = requirement + 8;
  fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      return false;
    }

  close(fd);
  return true;
}

static bool bkvalidate_claim_acquire(const char *claim)
{
  if (g_active_claim != NULL)
    {
      return false;
    }

  g_active_claim = claim;
  return true;
}

static void bkvalidate_claim_release(void)
{
  g_active_claim = NULL;
}

static void bkvalidate_print_outcome(
  const struct bkvalidate_descriptor_s *descriptor,
  const char *status, const char *reason, bool newline)
{
  printf("{\"schema\":\"bk7258.validation-outcome/1\","
         "\"kind\":\"validation-outcome\",\"version\":1,"
         "\"id\":\"%s\",\"status\":\"%s\",\"reason\":\"%s\","
         "\"role\":\"%s\",\"category\":\"%s\",\"timeout\":%u,"
         "\"requirements\":[\"%s\"],\"resource_claims\":[\"%s\"],"
         "\"prepare\":\"%s\",\"run\":\"%s\","
         "\"cancel\":\"%s\",\"cleanup\":\"%s\"}",
         descriptor->id, status, reason, descriptor->role,
         descriptor->category, descriptor->timeout, descriptor->requirement,
         descriptor->resource_claim, descriptor->prepare, descriptor->run,
         descriptor->cancel, descriptor->cleanup);
  if (newline)
    {
      putchar('\n');
    }
}

static void bkvalidate_list_stable(void)
{
  unsigned int index;

  printf("{\"schema\":\"bk7258.validation-outcome/1\","
         "\"kind\":\"validation-list\",\"version\":1,\"tests\":[");
  for (index = 0; index < BKVALIDATE_DESCRIPTOR_COUNT; index++)
    {
      const struct bkvalidate_descriptor_s *descriptor = &g_descriptors[index];

      if (index != 0)
        {
          putchar(',');
        }

      printf("{\"id\":\"%s\",\"version\":1,\"role\":\"%s\","
             "\"requirements\":[\"%s\"],\"category\":\"%s\","
             "\"timeout\":%u,\"prepare\":\"%s\",\"run\":\"%s\","
             "\"cancel\":\"%s\",\"cleanup\":\"%s\",\"status\":\"%s\","
             "\"resource_claims\":[\"%s\"],"
             "\"entrypoint\":\"app/hello_app/bkvalidate_main.c\"}",
             descriptor->id, descriptor->role, descriptor->requirement,
             descriptor->category, descriptor->timeout, descriptor->prepare,
             descriptor->run, descriptor->cancel, descriptor->cleanup,
             descriptor->status, descriptor->resource_claim);
    }

  printf("]}\n");
}

static void bkvalidate_run_one(const struct bkvalidate_descriptor_s *descriptor,
                               bool all_compatible, bool newline)
{
  const char *reason;
  int ret;

  if (all_compatible && strcmp(descriptor->category, "auto") != 0)
    {
      bkvalidate_print_outcome(descriptor, "SKIP",
                               "category_not_all_compatible", newline);
      return;
    }

  if (strcmp(descriptor->category, "auto") != 0 &&
      !descriptor->command_driven)
    {
      bkvalidate_print_outcome(descriptor, "SKIP",
                               "category_requires_explicit_authorization", newline);
      return;
    }

  if (descriptor->command_driven && descriptor->start == NULL)
    {
      bkvalidate_print_outcome(descriptor, "SKIP", "validation_not_built",
                               newline);
      return;
    }

  if (strcmp(descriptor->status, "ready") != 0)
    {
      bkvalidate_print_outcome(descriptor, "SKIP", "descriptor_not_ready", newline);
      return;
    }

  if (!bkvalidate_requirement_available(descriptor->requirement))
    {
      bkvalidate_print_outcome(descriptor, "SKIP", "incompatible_requirement", newline);
      return;
    }

  if (!bkvalidate_claim_acquire(descriptor->resource_claim))
    {
      bkvalidate_print_outcome(descriptor, "SKIP", "resource_claim_busy", newline);
      return;
    }

  if (descriptor->start != NULL)
    {
      ret = descriptor->start();
      if (ret < 0)
        {
          bkvalidate_print_outcome(descriptor, "FAIL",
                                   "validation_start_failed", newline);
          bkvalidate_claim_release();
          return;
        }

      ret = descriptor->wait == NULL ? -EPROTO :
            descriptor->wait(descriptor->timeout);
      if (ret < 0)
        {
          reason = ret == -ETIMEDOUT ? "validation_timeout" :
                   ret == -EPROTO ? "validation_diag_identity_mismatch" :
                   "validation_failed";
          bkvalidate_print_outcome(descriptor, "FAIL", reason, newline);
          bkvalidate_claim_release();
          return;
        }

      reason = "validation_passed";
    }
  else
    {
      reason = strcmp(descriptor->run, "public_api:open-close") == 0 ?
               "public_device_api_probe" : "runner_skeleton";
    }

  bkvalidate_print_outcome(descriptor, "PASS", reason, newline);
  bkvalidate_claim_release();
}

static int bkvalidate_run(const char *id)
{
  const struct bkvalidate_descriptor_s *descriptor = bkvalidate_find(id);

  if (descriptor == NULL)
    {
      fprintf(stderr, "bkvalidate: unknown descriptor: %s\n", id);
      return 2;
    }

  bkvalidate_run_one(descriptor, false, true);
  return 0;
}

static int bkvalidate_all_compatible(void)
{
  unsigned int index;

  printf("{\"schema\":\"bk7258.validation-outcome/1\","
         "\"kind\":\"validation-run\",\"version\":1,"
         "\"mode\":\"all-compatible\",\"outcomes\":[");
  for (index = 0; index < BKVALIDATE_DESCRIPTOR_COUNT; index++)
    {
      if (index != 0)
        {
          putchar(',');
        }

      bkvalidate_run_one(&g_descriptors[index], true, false);
    }

  printf("]}\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  if (argc == 2 && strcmp(argv[1], "list") == 0)
    {
      bkvalidate_list_stable();
      return 0;
    }

  if (argc == 3 && strcmp(argv[1], "run") == 0)
    {
      return bkvalidate_run(argv[2]);
    }

  if (argc == 2 && strcmp(argv[1], "all-compatible") == 0)
    {
      return bkvalidate_all_compatible();
    }

  fprintf(stderr, "usage: bkvalidate list | run <id> | all-compatible\n");
  return 2;
}
