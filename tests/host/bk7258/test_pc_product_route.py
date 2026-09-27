#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exact product dispatcher plus real owner/settings/storage/PC modules."""
import shlex
import subprocess
import tempfile
from pathlib import Path
from test_nfc_rf_lifecycle import function, ROOT

if __name__ == "__main__":
    import sys

    source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    body = function(source, "product_config")
    start = source.index("struct agent_config_workspace_s\n")
    workspace = source[start : source.index("};", start) + 2]
    activation = function(source, "bk7258_agent_activate_cloud")
    prefix = r"""
#include "bk7258_pc_authorization_owner.h"
#include <mbedtls/platform_util.h>
static bool g_identity_bound=true, g_control_bound, g_save_first, g_configured;
static uint64_t g_config_revision;
static atomic_bool g_agent_core_ready, g_voice_initialized;
static struct { mbedtls_x509_crt certificate; mbedtls_pk_context key; uint8_t secret[32]; } g_identity;
#define bkprov_network_busy() false
#define bkprov_identity_load(...) (-ENOTSUP)
#define bkprov_network_bind(...) (-ENOTSUP)
#define bkprov_network_unbind() 0
#define bkprov_network_ops() NULL
#define bkprov_identity_clear(...) ((void)0)
#define storage_unavailable(ret) ((ret)==-ENODEV)
#define product_clear(...) 0
#define bkprov_network_restore(...) (-ENETDOWN)
#define product_control execute
static int ota_busy;
#define bkagent_ota_busy() ota_busy
#define bkfocus_control(...) (-ENOTSUP)
#define bkprov_config_control(...) (-ENOTSUP)
#define product_scan_read(...) (-ENOTSUP)
#define product_reset_control(...) (-ENOTSUP)
#define product_models(...) (-ENOTSUP)
#define product_response_mode(...) (-ENOTSUP)
#define product_wake_threshold(...) (-ENOTSUP)
#define bk7258_agent_trigger_control(...) (-ENOTSUP)
"""
    with tempfile.TemporaryDirectory(prefix="pc-route-") as directory:
        temp = Path(directory)
        code = (ROOT / "tests/host/bk7258/test_pc_owner_binding.c").read_text()
        code = code.replace(
            "int main(int argc,char **argv)",
            prefix
            + body
            + "\n"
            + workspace
            + "\n"
            + activation
            + "\nint main(int argc,char **argv)",
        )
        code = code.replace(
            "assert(prepare(1)==0 && read_current()==0);",
            """bool waiting = true;
 assert(bk7258_agent_activate_cloud(&waiting)==0 && !waiting);
 int ready=-EAGAIN;
 for(int i=0;i<3000 && ready==-EAGAIN;i++)
  {ready=read_current();if(ready==-EAGAIN)tick();}
 assert(ready==0 && g_control_bound && !g_configured);
""",
            1,
        )
        code = code.replace(
            " assert(set(1,0,tx,3)==0);",
            r"""
 struct bkcontrol_status_s status={0};
 assert(product_config(NULL,BKCONTROL_CONFIG_READ,14,0,NULL,0,&status)==0);
 ota_busy=1;
 assert(product_config(NULL,BKCONTROL_CONFIG_READ,14,0,NULL,0,&status)==0);
 assert(product_config(NULL,BKCONTROL_CONFIG_BEGIN,14,0,NULL,88,&status)==-EBUSY);
 ota_busy=0;
 assert(product_config(NULL,BKCONTROL_CONFIG_BEGIN,14,0,NULL,88,&status)==0);
 assert(set(1,0,tx,3)==0);
""",
        )
        (temp / "case.c").write_text(code)
        command = subprocess.check_output(
            ["make", "-n", "-B", "build/test_pc_owner_binding"],
            cwd=ROOT / "tests/host/bk7258",
            text=True,
        )
        args = shlex.split(
            next(
                line
                for line in command.splitlines()
                if line.startswith("cc ") and "test_pc_owner_binding.c" in line
            )
        )
        args = [
            str(temp / "case.c") if x == "test_pc_owner_binding.c" else x for x in args
        ]
        args += [
            "-I",
            str(ROOT / "tests/host/bk7258"),
            "-Wno-unused-parameter",
            "-Wno-unused-value",
        ]
        index = args.index("-o") + 1
        args[index] = str(temp / "test")
        subprocess.run(args, cwd=ROOT / "tests/host/bk7258", check=True)
        (temp / "data").mkdir()
        subprocess.run(
            [temp / "test", sys.argv[1], temp / "data"], check=True, timeout=30
        )
