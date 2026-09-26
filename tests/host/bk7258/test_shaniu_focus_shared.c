/* SPDX-License-Identifier: Apache-2.0 */
/* Real focus owner through typed and wire entries, virtual monotonic time. */
#include "bk7258_focus.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct bkcontrol_status_s status;
/* Independent FOC1: start, expected revision 0, operation 11, 60 seconds. */
static const uint8_t start[32] =
{
  0x46, 0x4f, 0x43, 0x31, 0, 0, 0, 1,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 11,
  0, 0, 0, 0, 0, 0, 0xea, 0x60
};

static int wire(const uint8_t *record, uint64_t now)
{
  return bkfocus_control(BKCONTROL_CONFIG_APPLY, 0, record, 32,
                         &status, now);
}

static void snapshot(unsigned int state, uint64_t revision,
                     uint64_t remaining, uint64_t now)
{
  struct bkfocus_snapshot_s value;
  assert(bkfocus_snapshot(&value, now) == 0);
  assert(value.state == state && value.revision == revision);
  assert(value.remaining_ms == remaining && value.duration_ms == 60000);
}

int main(int argc, char **argv)
{
  struct bkfocus_request_s request;
  uint8_t record[32];

  assert(argc == 2);
  memset(&request, 0xa5, sizeof(request));
  request.action = 1;
  request.revision = 0;
  request.operation = 11;
  request.duration_ms = 60000;

  if (strcmp(argv[1], "start") == 0)
    {
      assert(bkfocus_execute(&request, 1000) == 0);
      snapshot(1, 1, 50000, 11000);
      assert(bkfocus_control(BKCONTROL_CONFIG_READ, 0, NULL, 0,
                             &status, 11000) == 0);
      const uint8_t header[16] =
        {'F', 'O', 'S', '1', 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1};
      assert(memcmp(status.config_chunk, header, 16) == 0);
      /* Wire pause changes the timer created through the typed entry. */
      memcpy(record, start, 32);
      record[7] = 2; record[15] = 1; record[23] = 12;
      memset(record + 24, 0, 8);
      assert(wire(record, 11000) == 0);
      snapshot(2, 2, 50000, 16000);
      request.action = 3; request.revision = 2;
      request.operation = 13; request.duration_ms = 0;
      assert(bkfocus_execute(&request, 16000) == 0);
      assert(bkfocus_step(65999) == 0);
      assert(bkfocus_step(66000) == 1);
      assert(bkfocus_step(66000) == 0);
      snapshot(3, 4, 0, 66000);
    }
  else if (strcmp(argv[1], "retry") == 0)
    {
      assert(wire(start, 1000) == 0);
      /* Padding and entry point must not affect exact operation identity. */
      assert(bkfocus_execute(&request, 5000) == 0);
      snapshot(1, 1, 56000, 5000);
      assert(wire(start, 6000) == 0);
      snapshot(1, 1, 55000, 6000);
      request.operation = 12;
      assert(bkfocus_execute(&request, 6000) == -ESTALE);
      snapshot(1, 1, 55000, 6000);
    }
  else if (strcmp(argv[1], "cancel") == 0)
    {
      assert(wire(start, 1000) == 0);
      request.action = 4; request.revision = 1;
      request.operation = 12; request.duration_ms = 0;
      assert(bkfocus_execute(&request, 2000) == 0);
      snapshot(4, 2, 0, 2000);
      assert(wire(start, 3000) == -ESTALE);
      assert(bkfocus_step(61000) == 0);
      request.action = 1; request.revision = 2;
      request.operation = 13; request.duration_ms = 60000;
      assert(bkfocus_execute(&request, 62000) == 0);
      bkfocus_cancel();
      snapshot(4, 4, 0, 62000);
      assert(bkfocus_execute(&request, 63000) == -ESTALE);
      assert(bkfocus_step(200000) == 0);
    }
  else
    {
      assert(strcmp(argv[1], "invalid") == 0);
      assert(bkfocus_execute(NULL, 0) == -EINVAL);
      assert(bkfocus_snapshot(NULL, 0) == -EINVAL);
      request.action = 5;
      assert(bkfocus_execute(&request, 0) == -EINVAL);
      request.action = 1; request.operation = 0;
      assert(bkfocus_execute(&request, 0) == -EINVAL);
      request.operation = 11;
      assert(bkfocus_execute(&request, 1000) == 0);
      request.action = 2; request.revision = 1;
      request.operation = 12;
      assert(bkfocus_execute(&request, 2000) == -EINVAL);
      request.duration_ms = 0;
      assert(bkfocus_execute(&request, 999) == -EAGAIN);
      assert(bkfocus_execute(&request, 61000) == -EAGAIN);
      snapshot(1, 1, 0, 61000);
      assert(bkfocus_step(61000) == 1);
    }

  puts("CONTRACT_PASS");
  return 0;
}
