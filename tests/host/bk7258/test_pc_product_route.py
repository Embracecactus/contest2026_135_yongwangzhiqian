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
    response_length = function(source, "product_response_length")
    task_step = function(source, "product_pc_task_step")
    focus_source = (ROOT / "app/bk7258/bk7258_agent_focus.inc").read_text()
    focus_start = focus_source.index("static unsigned int product_focus_visual(")
    focus_open = focus_source.index("{", focus_start)
    focus_depth = 0
    for focus_end in range(focus_open, len(focus_source)):
        if focus_source[focus_end] == "{":
            focus_depth += 1
        elif focus_source[focus_end] == "}":
            focus_depth -= 1
            if focus_depth == 0:
                focus_end += 1
                break
    else:
        raise RuntimeError("product_focus_visual body changed")
    focus_visual = focus_source[focus_start:focus_end]
    visual_start = source.index("      bk7258_display_focus(product_focus_visual(")
    visual_statement = source[visual_start : source.index(";", visual_start) + 1]
    visual_step = (
        "static void product_visual_test(uint64_t now) {"
        "bool focus_idle = !atomic_load(&g_voice_initialized) || "
        "voice_channel_is_idle();"
        + visual_statement + "}"
    )

    start = source.index("struct agent_config_workspace_s\n")
    workspace = source[start : source.index("};", start) + 2]
    application_completion = function(source, "product_application_loaded")
    activation = function(source, "bk7258_agent_activate_cloud")
    prefix = r"""
#include "bk7258_pc_authorization_owner.h"
#include "bk7258_pc_tasks.h"
#include "bk7258_pc_grants.h"
#include "bk7258_preferences.h"
#include "bk7258_agent_cloud.h"
#include "bk7258_focus_intent.h"
#include "bk7258_focus_pixels.h"
#include <pthread.h>
static struct bkpc_tasks_s g_pc_tasks;
static unsigned painted, focus_visual;
static bool voice_idle=true;
#define voice_channel_is_idle() voice_idle
#define bkfocus_visual(now) focus_visual
static pthread_mutex_t g_focus_feedback_lock = PTHREAD_MUTEX_INITIALIZER;
static struct bkfocus_intent_status_s g_focus_feedback;
static uint64_t g_focus_feedback_until;
static unsigned int g_focus_feedback_state;
#define bk7258_display_focus(value) (painted=(value))
#include <mbedtls/platform_util.h>
static bool g_identity_bound=true, g_control_bound, g_save_first, g_configured;
static uint64_t g_config_revision;
static uint64_t g_application_revision;
#define BKPROV_CONFIG_APPLICATION_APPLYING 1
#define BKPROV_CONFIG_APPLICATION_READY 2
#define BKPROV_CONFIG_APPLICATION_FAILED 3
static unsigned application_publications;
static uint64_t application_revision;
static unsigned application_state;
static int application_result;
static __attribute__((unused)) int
bkprov_config_application_publish(uint64_t revision,
                                  unsigned state, int result)
{
 application_publications++;application_revision=revision;
 application_state=state;application_result=result;return 0;
}
static atomic_bool g_agent_core_ready, g_voice_initialized;
int bk7258_preferences_response_length_get(
  struct bk7258_response_length_s *value)
{ value->mode=BK7258_RESPONSE_LENGTH_STANDARD; value->revision=0; return 0; }
int bk7258_preferences_response_length_set(
  enum bk7258_response_length_e mode, uint64_t revision,
  const uint8_t transaction[16])
{ (void)mode; (void)revision; (void)transaction; return 0; }
int bkagent_cloud_get_response_length(unsigned int *mode)
{ *mode=BK7258_RESPONSE_LENGTH_STANDARD; return 0; }
void bkagent_cloud_set_response_length(unsigned int mode) { (void)mode; }
static struct { mbedtls_x509_crt certificate; mbedtls_pk_context key; uint8_t secret[32]; } g_identity;
#define bkprov_network_busy() false
#define bkprov_identity_load(...) (-ENOTSUP)
#define bkprov_network_bind(...) (-ENOTSUP)
#define bkprov_network_unbind() 0
#define bkprov_network_ops() NULL
#define bkprov_identity_clear(...) ((void)0)
#define storage_unavailable(ret) ((ret)==-ENODEV)
#define product_clear(...) 0
static int network_restore_result=-ENETDOWN;
#define bkprov_network_restore(...) network_restore_result
#define product_control execute
static int ota_busy;
#define bkagent_ota_busy() ota_busy
#define bkfocus_control(...) (-ENOTSUP)
#define bkvoice_config_now_ms(...) task_now
static uint64_t task_now=100;
#define bkprov_config_control(...) (-ENOTSUP)
#define product_scan_read(...) (-ENOTSUP)
#define product_reset_control(...) (-ENOTSUP)
#define product_models(...) (-ENOTSUP)
#define product_response_mode(...) (-ENOTSUP)
#define product_wake_threshold(...) (-ENOTSUP)
#define bk7258_agent_trigger_control(...) (-ENOTSUP)
static bool task_snapshot_eagain;
static int product_test_pc_snapshot(void *context, uint64_t *binding,
                                    struct bkprov_pc_snapshot_s *view)
{
 if(task_snapshot_eagain)
  {
   task_snapshot_eagain=false;*binding=0;memset(view,0,sizeof(*view));
   return -EAGAIN;
  }
 return bkpc_authorization_snapshot(context,binding,view);
}
#define bkpc_authorization_snapshot product_test_pc_snapshot
"""
    with tempfile.TemporaryDirectory(prefix="pc-route-") as directory:
        temp = Path(directory)
        code = (ROOT / "tests/host/bk7258/test_pc_owner_binding.c").read_text()
        code = code.replace(
            "int main(int argc,char **argv)",
            prefix
            + focus_visual
            + visual_step
            + task_step
            + "\n"
            + response_length
            + "\n"
            + body
            + "\n"
            + workspace
            + "\n"
            + application_completion
            + "\n"
            + activation
            + "\nint main(int argc,char **argv)",
        )
        application_expectation = (
            " assert(application_publications>=1 && application_revision==1 "
            "&& application_state==2 && application_result==0);"
            if sys.argv[1] == "application-status"
            else ""
        )
        code = code.replace(
            "assert(prepare(1)==0 && read_current()==0);",
            """bool waiting = true;
 %s
 assert(bk7258_agent_activate_cloud(&waiting)==0 && !waiting);
%s
 product_visual_test(task_now); assert(painted==0);
 int ready=-EAGAIN;
 for(int i=0;i<3000 && ready==-EAGAIN;i++)
  {ready=read_current();if(ready==-EAGAIN)tick();}
 assert(ready==0 && g_control_bound && !g_configured);
""" % (
                "atomic_store(&g_agent_core_ready,true); "
                "atomic_store(&g_voice_initialized,true); network_restore_result=0;"
                if sys.argv[1] == "application-status" else "",
                application_expectation,
            ),
            1,
        )
        if sys.argv[1] == "application-status":
            code = code.replace(
                "struct bkprov_settings_s settings={.control_key=key,.deferred=true,.utc=1750000000};",
                "struct bkprov_settings_s settings={.control_key=key,.ssid=\"wifi\",.deferred=true,.utc=1750000000};",
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
        if sys.argv[1] == "tasks":
            code = code.replace(
                "assert(set(1,0,tx,3)==0);",
                r"""
 assert(set(1,0,tx,7)==0);
 product_pc_task_step(task_now,true);
 unsigned char task[40]={'P','T','E','1',0,0,0,1,7};
 task[31]=1;task[34]=0x27;task[35]=0x10;
 assert(product_config(NULL,BKCONTROL_CONFIG_APPLY,15,0,task,40,&status)==0);
 assert(product_config(NULL,BKCONTROL_CONFIG_READ,15,0,NULL,0,&status)==0);
 assert(!memcmp(status.config_chunk,"PTS1",4) && status.config_chunk[7]==1);
 task[7]=3;task[31]=2;task[39]=100;
 assert(product_config(NULL,BKCONTROL_CONFIG_APPLY,15,0,task,40,&status)==0);
 product_visual_test(task_now); assert(painted==(4u<<8));
 atomic_store(&g_voice_initialized,true);voice_idle=false;
 product_visual_test(task_now); assert(painted==0);
 voice_idle=true;focus_visual=0x110;
 product_visual_test(task_now); assert(painted==0x110);
 focus_visual=0;
 product_visual_test(task_now+10000); assert(painted==0);
 task[7]=2;task[31]=3;
 assert(product_config(NULL,BKCONTROL_CONFIG_APPLY,15,0,task,40,&status)==-EALREADY);
 bkpc_authorization_unbind();
 assert(product_config(NULL,BKCONTROL_CONFIG_READ,15,0,NULL,0,&status)==-ENOKEY);
 product_pc_task_step(task_now,true);
 assert(g_pc_tasks.binding==0);
 assert(prepare(1)==0);
 product_pc_task_step(task_now,true);
 assert(product_config(NULL,BKCONTROL_CONFIG_READ,15,0,NULL,0,&status)==0);
 assert(status.config_chunk[7]==0);
""",
            )
        elif sys.argv[1] == "task-transient-authorization":
            code = code.replace(
                "assert(set(1,0,tx,3)==0);",
                r"""
 assert(set(1,0,tx,7)==0);
 product_pc_task_step(task_now,true);
 unsigned char task[40]={'P','T','E','1',0,0,0,1,7};
 task[31]=1;task[34]=0x27;task[35]=0x10;
 assert(product_config(NULL,BKCONTROL_CONFIG_APPLY,15,0,task,40,&status)==0);
 task[7]=3;task[31]=2;task[39]=100;
 assert(product_config(NULL,BKCONTROL_CONFIG_APPLY,15,0,task,40,&status)==0);
 product_visual_test(task_now);assert(painted==(4u<<8));
 task_now++;
 unsigned char next[40]={'P','T','E','1',0,0,0,1,8};
 next[31]=3;next[34]=0x27;next[35]=0x10;
 task_snapshot_eagain=true;
 assert(product_config(NULL,BKCONTROL_CONFIG_APPLY,15,0,next,40,&status)==-EAGAIN);
 task_now++;
 product_pc_task_step(task_now,true);
 product_visual_test(task_now);assert(painted==(4u<<8));
 assert(product_config(NULL,BKCONTROL_CONFIG_READ,15,32,NULL,0,&status)==0);
 assert(status.config_total==48 && status.config_chunk[11]==5);
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
            str(ROOT / "app/bk7258/bk7258_pc_tasks.c"),
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
            [
                temp / "test",
                "offline"
                if sys.argv[1] in ("tasks", "task-transient-authorization",
                                   "application-status")
                else sys.argv[1],
                temp / "data",
            ],
            check=True,
            timeout=30,
        )
