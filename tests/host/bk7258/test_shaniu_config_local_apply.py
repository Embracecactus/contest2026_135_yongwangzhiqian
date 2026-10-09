#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CFG-02: publish local loader completion separately from reachability."""

import resource
import subprocess
import sys
import tempfile
from pathlib import Path

from test_nfc_rf_lifecycle import function


ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    case = sys.argv[1]
    assert case in ("success", "failure")
    source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    completion = (
        function(source, "product_application_loaded")
        if "static void product_application_loaded" in source
        else ""
    )
    loader = function(source, "product_load_cloud")
    code = r'''
#include "bk7258_cloud_config.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BKPROV_CONFIG_APPLICATION_READY 2
#define BKPROV_CONFIG_APPLICATION_FAILED 3
static bool g_save_first;
static int load_result;
static uint64_t g_application_revision=7;
static unsigned application_publications;
static uint64_t application_revision;
static unsigned application_state;
static int application_result;
int bkprov_config_application_publish(uint64_t revision,
                                      unsigned state, int result)
{
  application_publications++;
  application_revision=revision;application_state=state;
  application_result=result;return 0;
}
static int product_load_cloud_models(const void *trust, size_t trust_size,
                                     const void *cloud, size_t cloud_size,
                                     const struct bkcloud_models_s *models)
{
  assert(trust && trust_size==1 && cloud && cloud_size==1 && models==NULL);
  return load_result;
}
int bkcloud_config_decode(struct bkcloud_config_s *config,
                          const void *record, size_t size)
{ (void)record;(void)size;memset(config,0,sizeof(*config));return 0; }
void bkcloud_config_clear(struct bkcloud_config_s *config)
{ memset(config,0,sizeof(*config)); }
''' + completion + loader + r'''
int main(int argc,char **argv)
{
  assert(argc==2);
  load_result=!strcmp(argv[1],"success") ? 0 : -EBADMSG;
  unsigned char trust=1,cloud=2;
  assert(product_load_cloud(NULL,&trust,1,&cloud,1)==load_result);
  assert(application_publications==1 && application_revision==7);
  assert(application_state==(load_result ?
    BKPROV_CONFIG_APPLICATION_FAILED : BKPROV_CONFIG_APPLICATION_READY));
  assert(application_result==load_result && g_application_revision==0);
  puts("CONTRACT_PASS");return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="config-local-apply-") as directory:
        root = Path(directory)
        source_path = root / "case.c"
        binary = root / "case"
        source_path.write_text(code)
        build = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "app/bk7258"), str(source_path), "-o", str(binary)],
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
