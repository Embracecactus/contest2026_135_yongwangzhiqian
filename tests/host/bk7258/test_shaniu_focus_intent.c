/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_focus_intent.h"
#include "cJSON.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static char output[512];
static int checks;
static int cancel_during_submit(void *context)
{
  checks++;
  if (checks == 2 && context != NULL) bkfocus_intent_step(2000, true);
  return checks == 2 ? -ECANCELED : 0;
}
static int tool(const char *input, int (*check)(void *), void *context)
{
  cJSON *args = cJSON_Parse(input);
  int ret = bkfocus_tool_execute(args, output, sizeof(output), check, context);
  cJSON_Delete(args);
  return ret;
}
static void timer(unsigned int state, uint64_t remaining, uint64_t now)
{
  struct bkfocus_snapshot_s value;
  assert(bkfocus_snapshot(&value, now) == 0);
  assert(value.state == state && value.remaining_ms == remaining);
}
int main(int argc, char **argv)
{
  struct bkfocus_intent_status_s status;
  uint32_t id = 123;
  assert(argc == 2);
  assert(bkfocus_intent_submit(1, 60000, &id) == -ESHUTDOWN && id == 0);
  bkfocus_intent_step(1000, true);
  if (strcmp(argv[1], "voice") == 0)
    {
      assert(tool("{\"action\":\"start\",\"seconds\":60}", NULL, NULL) == 0);
      assert(strstr(output, "accepted") != NULL);
      timer(0, 0, 1000);
      bkfocus_intent_status(&status);
      assert(status.phase == 1 && status.id != 0);
      id = status.id;
      assert(bkfocus_intent_submit(1, 1000, &id) == -EBUSY);
      assert(tool("{\"action\":\"status\"}", NULL, NULL) == 0);
      timer(0, 0, 1000);
      bkfocus_intent_step(2000, true);
      timer(1, 60000, 2000);
      assert(tool("{\"action\":\"pause\"}", NULL, NULL) == 0);
      bkfocus_intent_step(12000, true);
      timer(2, 50000, 12000);
      assert(tool("{\"action\":\"resume\"}", NULL, NULL) == 0);
      bkfocus_intent_step(17000, true);
      assert(bkfocus_step(67000) == 1);
      assert(bkfocus_step(67000) == 0);
      bkfocus_intent_step(67000, true);
      assert(tool("{\"action\":\"status\"}", NULL, NULL) == 0);
      assert(strstr(output, "\"timer_state\":3") != NULL);
    }
  else if (strcmp(argv[1], "gate") == 0)
    {
      assert(bkfocus_intent_submit(1, 60000, &id) == 0);
      bkfocus_intent_step(2000, false);
      bkfocus_intent_status(&status);
      assert(status.phase == 4 && status.error == -ESHUTDOWN);
      timer(0, 0, 2000);
      assert(bkfocus_intent_submit(1, 60000, &id) == -ESHUTDOWN);
      bkfocus_intent_step(3000, true);
      timer(0, 0, 3000);
    }
  else if (strcmp(argv[1], "revision") == 0)
    {
      struct bkfocus_request_s newer = {1, 0, 900, 30000};
      assert(bkfocus_intent_submit(1, 60000, &id) == 0);
      assert(bkfocus_execute(&newer, 1500) == 0);
      bkfocus_intent_step(2000, true);
      bkfocus_intent_status(&status);
      assert(status.phase == 3 && status.error == -ESTALE);
      timer(1, 29500, 2000);
    }
  else if (strcmp(argv[1], "cancel") == 0)
    {
      assert(tool("{\"action\":\"start\",\"seconds\":60}",
                  cancel_during_submit, NULL) == -ECANCELED);
      bkfocus_intent_step(2000, true);
      timer(0, 0, 2000);
      checks = 0;
      assert(tool("{\"action\":\"start\",\"seconds\":60}",
                  cancel_during_submit, &checks) == 0);
      assert(strstr(output, "too_late") != NULL);
      timer(1, 60000, 2000);
      bkfocus_intent_status(&status);
      assert(bkfocus_intent_cancel(status.id) == -EALREADY);
      assert(bkfocus_intent_cancel(status.id - 1) == -ESTALE);
    }
  else
    {
      assert(strcmp(argv[1], "invalid") == 0);
      const char *invalid[] = {
        "{}", "{\"action\":\"reset\"}", "{\"action\":\"start\"}",
        "{\"action\":\"start\",\"seconds\":0}",
        "{\"action\":\"start\",\"seconds\":1.5}",
        "{\"action\":\"start\",\"seconds\":4294968}",
        "{\"action\":\"pause\",\"seconds\":60}"
      };
      for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        assert(tool(invalid[i], NULL, NULL) == -EINVAL);
      cJSON *args = cJSON_Parse("{\"action\":\"start\",\"seconds\":60}");
      assert(bkfocus_tool_execute(args, output, 4, NULL, NULL) == -ENOSPC);
      cJSON_Delete(args);
      bkfocus_intent_status(&status);
      assert(status.id == 0 && status.phase == 0);
      timer(0, 0, 2000);
    }
  puts("CONTRACT_PASS");
  return 0;
}
