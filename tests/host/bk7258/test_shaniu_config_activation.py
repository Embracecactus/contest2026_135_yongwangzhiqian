#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CFG-02: execute the production network-completion coordinator block.

The durable settings revision is the desired selection.  A network trial was
started from ``g_config_revision``.  If a newer selection becomes durable
before that trial completes, its late success or failure must not publish
readiness/error for the newer selection or schedule a retry of the old one.

Only the storage revision and network/Wi-Fi peers are replaced.  The readiness,
error, link and retry decisions are extracted verbatim from the product worker.
This is L1 coordinator evidence, not real Wi-Fi or TLS acceptance.
"""

import resource
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CASES = ("stale-success", "stale-failure", "current-success",
         "current-failure", "desired-unknown")


def main() -> int:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    case = sys.argv[1]
    assert case in CASES
    source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    start = source.index("      bkprov_config_step();")
    end_marker = "      network_was_busy = network_busy;"
    end = source.index(end_marker, start) + len(end_marker)
    body = source[start:end]
    code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define syslog(...) ((void)0)
#define LOG_INFO 0
#define LOG_WARNING 1
#define BK7258_WIFI_LINK_CONNECTED 1
struct bk7258_wifi_result_s { int link_state; uint32_t ipaddr; };
static bool network_busy_value, network_was_busy=true, link_expected;
static bool g_configured, pending;
static int network_result_value, g_service_result, g_product_error;
static int storage_revision_result;
static uint64_t desired_revision=1, g_config_revision=1;
static uint64_t now=1000, network_retry_at;
static uint32_t network_backoff=5000;
static void bkprov_config_step(void) {}
static void bkprov_network_step(void) {}
static bool bkprov_network_busy(void) { return network_busy_value; }
static int bkprov_network_result(void) { return network_result_value; }
static int bkprov_storage_revision(uint64_t *revision) {
  if (storage_revision_result) return storage_revision_result;
  *revision=desired_revision; return 0;
}
static int bk7258_wifi_read_link(struct bk7258_wifi_result_s *link) {
  link->link_state=BK7258_WIFI_LINK_CONNECTED; link->ipaddr=0x01020304; return 0;
}
static void step(void) {
''' + body + r'''
}
int main(int argc,char **argv) {
  assert(argc==2 && g_config_revision==1); pending=strncmp(argv[1],"current-",8)!=0;
  if(!strncmp(argv[1],"stale-",6)) desired_revision=2;
  if(!strcmp(argv[1],"desired-unknown")) storage_revision_result=-EINPROGRESS;
  network_result_value=strstr(argv[1],"failure") ? -ETIMEDOUT : 0;
  step();
  if(!strncmp(argv[1],"current-",8)) {
    if(network_result_value==0) {
      assert(g_configured && g_product_error==0 && link_expected);
      assert(network_retry_at==0 && network_backoff==5000);
    } else {
      assert(!g_configured && g_product_error==-ETIMEDOUT);
      assert(network_retry_at>0 && network_backoff==10000);
    }
  } else {
    assert(!g_configured && pending && !link_expected);
    assert(g_product_error==(storage_revision_result ? -EINPROGRESS : -EAGAIN));
    assert(network_retry_at==0 && network_backoff==5000);
  }
  puts("CONTRACT_PASS"); return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="config-activation-") as directory:
        root = Path(directory)
        source_path = root / "case.c"
        binary = root / "case"
        source_path.write_text(code)
        build = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-function", str(source_path), "-o", str(binary)],
            capture_output=True, text=True,
        )
        if build.returncode:
            print("SETUP_ERROR", build.stdout + build.stderr)
            return 2
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        print(result.stdout + result.stderr, end="")
        return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
