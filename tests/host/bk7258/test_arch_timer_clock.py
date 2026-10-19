#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the actual NuttX timer conversion across a second boundary.

Pass the arch_timer.c used by the build. The hardware counter is the only
mock; the production conversion function is compiled without modification.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.read_text()
    match = re.search(
        r"^int weak_function up_timer_gettime\([^;{}]*\)\s*\{", source, re.M
    )
    if match is None:
        raise RuntimeError("production up_timer_gettime not found")
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    function = source[match.start():end]
    fixture = r'''
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#define weak_function
#define OK 0
#define USEC_PER_SEC 1000000ull
#define NSEC_PER_USEC 1000ull
static struct { void *lower; } g_timer;
static uint64_t counter;
static unsigned reads;
static uint64_t current_usec(void)
{
  reads++;
  return counter++;
}
'''
    cases = r'''
static int check(uint64_t before)
{
  struct timespec ts;
  uint64_t observed;
  counter = before;
  reads = 0;
  if (up_timer_gettime(&ts) != OK)
    return 1;
  if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
    {
      printf("FAIL nanoseconds out of range\n");
      return 1;
    }
  observed = ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC;
  if (observed < before || observed >= counter)
    {
      printf("FAIL counter=%llu observed=%llu reads=%u\n",
             (unsigned long long)before, (unsigned long long)observed, reads);
      return 1;
    }
  return 0;
}
int main(void)
{
  struct timespec ts = {7, 9};
  unsigned failures = 0;
  if (up_timer_gettime(&ts) != -EAGAIN || reads != 0 ||
      ts.tv_sec != 7 || ts.tv_nsec != 9)
    failures++;
  g_timer.lower = &g_timer;
  failures += check(1234567);
  failures += check(161999999);
  failures += check(162000000);
  failures += check(4294999999ull);
  printf("timer conversion cases=5 failures=%u\n", failures);
  return failures ? 1 : 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="bk7258-clock-") as directory:
        c_file = Path(directory) / "clock.c"
        executable = Path(directory) / "clock"
        c_file.write_text(fixture + function + cases)
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             str(c_file), "-o", str(executable)], check=True
        )
        return subprocess.run([str(executable)], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
