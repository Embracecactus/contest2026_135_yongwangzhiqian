#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CFG-02: link loss changes connectivity, not local application state."""

import resource
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    start = source.index(
        "      if (link_expected && !network_busy && now >= link_check_at)"
    )
    end = source.index(
        "      if (network_retry_at && now >= network_retry_at)", start
    )
    body = source[start:end]
    code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#define LOG_WARNING 1
#define syslog(...) ((void)0)
#define BK7258_WIFI_LINK_CONNECTED 1
#define BKPROV_CONFIG_APPLICATION_FAILED 3
struct bk7258_wifi_result_s { int link_state; uint32_t ipaddr; };
static bool link_expected=true, network_busy, g_configured=true;
static uint64_t now=1000, link_check_at, network_retry_at;
static __attribute__((unused)) uint64_t g_config_revision=7;
static int g_service_result;
static atomic_bool g_voice_initialized=true;
static unsigned cancel_calls, application_publications;
static uint64_t application_revision;
static unsigned application_state;
static int application_result;
static int bk7258_wifi_read_link(struct bk7258_wifi_result_s *link) {
  link->link_state=0;link->ipaddr=0;return 0;
}
static void voice_channel_cancel(void) { cancel_calls++; }
static __attribute__((unused)) int
bkprov_config_application_publish(uint64_t revision,
                                  unsigned state, int result) {
  application_publications++;application_revision=revision;
  application_state=state;application_result=result;return 0;
}
static void step(void) {
''' + body + r'''
}
int main(void) {
  step();
  assert(!link_expected && !g_configured);
  assert(g_service_result==-ENETDOWN && network_retry_at==2000);
  assert(cancel_calls==1);
  assert(application_publications==0);
  puts("CONTRACT_PASS");return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="config-link-loss-") as directory:
        root = Path(directory)
        source_path = root / "case.c"
        binary = root / "case"
        source_path.write_text(code)
        build = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             str(source_path), "-o", str(binary)],
            capture_output=True, text=True,
        )
        if build.returncode:
            print("SETUP_ERROR", build.stdout + build.stderr)
            return 2
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        print(result.stdout + result.stderr, end="")
        return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
