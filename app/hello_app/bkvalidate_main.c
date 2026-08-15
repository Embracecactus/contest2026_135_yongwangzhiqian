/****************************************************************************
 * app/hello_app/bkvalidate_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small public-API validation dispatcher.  Descriptor policy is checked by
 * board/bk7258/scripts/bk7258_validation.py; this target-side skeleton only
 * probes public device paths, serializes resource claims, and emits stable
 * JSON outcomes.  It has no vendor SDK calls.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct bkvalidate_descriptor_s g_descriptors[] =
{
  {
    "rptun_public_api_smoke", "cp_ap", "devpath:/dev/rptun", "auto",
    30000, "none", "public_api:open-close", "standard-timeout",
    "public_api:close", "ready", "rptun_transport"
  },
  {
    "wifi_public_api_smoke", "cp_ap", "devpath:/dev/wlan0", "auto",
    30000, "none", "public_api:open-close", "standard-timeout",
    "public_api:close", "planned", "wifi_control"
  },
  {
    "gpio_interactive", "board", "operator:pin-observation", "interactive",
    60000, "operator-confirm", "public_api:gpio-observe", "operator-cancel",
    "public_api:close", "ready", "board_gpio"
  },
  {
    "jpeg_fixture", "ap", "fixture:jpeg-baseline", "fixture",
    30000, "fixture-mount", "public_api:video-io", "standard-timeout",
    "fixture-unmount", "ready", "jpeg_engine"
  },
  {
    "power_fault_recovery", "cp_ap", "fault:power-cycle", "destructive-fault",
    120000, "fault-authorization", "public_api:recovery-observe",
    "standard-timeout", "public_api:close", "ready", "pm_cross_core"
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
      return false;
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

  if (all_compatible && strcmp(descriptor->category, "auto") != 0)
    {
      bkvalidate_print_outcome(descriptor, "SKIP",
                               "category_not_all_compatible", newline);
      return;
    }

  if (strcmp(descriptor->category, "auto") != 0)
    {
      bkvalidate_print_outcome(descriptor, "SKIP",
                               "category_requires_explicit_authorization", newline);
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

  reason = strcmp(descriptor->run, "public_api:open-close") == 0 ?
           "public_device_api_probe" : "runner_skeleton";
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
