/*
 * Host harness for the portable BK7258 N15-V deterministic failpoint core.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bk7258_ota_fault_core.h"

static void require(bool condition, const char *message)
{
  if (!condition)
    {
      fprintf(stderr, "BK7258 N15-V fault harness FAIL: %s\n", message);
      exit(2);
    }
}

int main(void)
{
  struct bk7258_ota_fault_plan_s plan;
  struct bk7258_ota_fault_status_s status;
  unsigned int positive = 0;
  unsigned int negative = 0;

  bk7258_ota_fault_core_initialize(&plan);
  require(bk7258_ota_fault_core_snapshot(&plan, &status) == 0,
          "initial snapshot");
  require(!status.configured && !status.active && !status.triggered,
          "initial plan is not empty");
  positive++;

  require(bk7258_ota_fault_core_arm(NULL,
          BK7258_OTA_FAULT_STAGE_WRITE, 1, 42) == -EINVAL,
          "null arm accepted");
  negative++;
  require(bk7258_ota_fault_core_arm(&plan, BK7258_OTA_FAULT_NONE,
          1, 42) == -EINVAL, "invalid point accepted");
  negative++;
  require(bk7258_ota_fault_core_arm(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE, 0, 42) == -EINVAL,
          "zero ordinal accepted");
  negative++;
  require(bk7258_ota_fault_core_arm(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE, 1, 0) == -EINVAL,
          "zero generation accepted");
  negative++;
  require(bk7258_ota_fault_core_begin(NULL, 42,
          BK7258_OTA_FAULT_STAGE_MASK) == -EINVAL,
          "null begin accepted");
  negative++;
  require(bk7258_ota_fault_core_begin(&plan, 0,
          BK7258_OTA_FAULT_STAGE_MASK) == -EINVAL,
          "zero begin generation accepted");
  negative++;
  require(bk7258_ota_fault_core_begin(&plan, 42, 0) == -EINVAL,
          "zero begin mask accepted");
  negative++;
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_NONE) == -EINVAL,
          "invalid callback point accepted");
  negative++;
  require(bk7258_ota_fault_core_snapshot(&plan, NULL) == -EINVAL,
          "null snapshot accepted");
  negative++;

  require(bk7258_ota_fault_core_arm(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE, 3, 42) == 0,
          "cannot arm stage-write plan");
  positive++;
  require(bk7258_ota_fault_core_arm(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE, 3, 42) == -EBUSY,
          "duplicate plan accepted");
  negative++;
  require(bk7258_ota_fault_core_begin(&plan, 43,
          BK7258_OTA_FAULT_STAGE_MASK) == -ESTALE,
          "generation mismatch accepted");
  negative++;
  require(bk7258_ota_fault_core_begin(&plan, 42,
          BK7258_OTA_FAULT_PUBLISH_MASK) == -EPERM,
          "operation-family mismatch accepted");
  negative++;
  require(bk7258_ota_fault_core_begin(&plan, 42,
          BK7258_OTA_FAULT_STAGE_MASK) == 0,
          "cannot begin matching stage plan");
  positive++;
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_STAGE_READ) == 0,
          "unrelated callback failed");
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE) == 0,
          "first callback failed early");
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE) == 0,
          "second callback failed early");
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE) == -ECANCELED,
          "third callback did not trigger");
  require(bk7258_ota_fault_core_snapshot(&plan, &status) == 0 &&
          status.configured && !status.active && status.triggered &&
          status.seen == 3 && status.ordinal == 3 &&
          status.generation == 42,
          "trigger status drift");
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_STAGE_WRITE) == 0,
          "one-shot failpoint triggered twice");
  positive++;

  require(bk7258_ota_fault_core_finish(&plan, &status) == 0 &&
          status.triggered && status.seen == 3,
          "triggered finish snapshot drift");
  require(bk7258_ota_fault_core_snapshot(&plan, &status) == 0 &&
          !status.configured && status.point == BK7258_OTA_FAULT_NONE,
          "finish did not clear plan");
  positive++;

  require(bk7258_ota_fault_core_arm(&plan,
          BK7258_OTA_FAULT_PUBLISH_WRITE, 4, 44) == 0,
          "cannot arm publish miss plan");
  require(bk7258_ota_fault_core_begin(&plan, 44,
          BK7258_OTA_FAULT_PUBLISH_MASK) == 0,
          "cannot begin publish miss plan");
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_PUBLISH_WRITE) == 0,
          "publish miss callback one failed");
  require(bk7258_ota_fault_core_before(&plan,
          BK7258_OTA_FAULT_PUBLISH_WRITE) == 0,
          "publish miss callback two failed");
  require(bk7258_ota_fault_core_finish(&plan, &status) == 0 &&
          status.configured && status.active && !status.triggered &&
          status.seen == 2,
          "non-triggered finish status drift");
  positive++;

  require(bk7258_ota_fault_core_begin(&plan, 45,
          BK7258_OTA_FAULT_STAGE_MASK) == 0,
          "empty plan must not block mutation");
  require(bk7258_ota_fault_core_finish(&plan, NULL) == 0,
          "empty finish failed");
  positive++;

  printf("BK7258 N15-V fault harness PASS: positive=%u negative=%u\n",
         positive, negative);
  return 0;
}
