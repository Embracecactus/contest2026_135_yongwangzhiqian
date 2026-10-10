#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""BOOT-01/KWS-01: execute the production worker's local-listener routing.

Oracle: validated identity/model/preferences permit local listening without
cloud readiness; a queued local event is consumed, not lost behind cloud state.
Only external owners are peers; the worker conditions/order come from source.
This is L1 routing, not capture, inference, cloud, or physical acceptance.
"""
import resource
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CASES = ("content-busy", "online", "offline", "network-pending", "offline-event",
         "core-unavailable", "identity-unavailable", "threshold-busy", "model-failure", "cloud-retry", "offline-admission", "online-admission")

def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    case = sys.argv[1]
    assert case in CASES
    if case.endswith("-admission"):
        return admission(case)
    source = (ROOT / "app/bk7258/bk7258_agent_product.c").read_text()
    start = source.index("      if (content_busy || !atomic_load(&g_agent_core_ready)")
    end = source.index("\n    }\n}\n#endif", start)
    body = source[start:end]
    prefix = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#define CONFIG_BK7258_PREFERENCES 1
#define syslog(...) ((void)0)
static bool g_configured, g_identity_bound=true, g_trigger_started;
static bool content_busy;
static atomic_bool g_agent_core_ready=true, g_voice_initialized=true;
static atomic_bool g_trigger_prepare_pending=true;
static atomic_int g_active_persona;
static int g_product_error, g_service_result, g_probe_result;
static atomic_bool g_probe_running;
static bool g_cloud_loaded;
static unsigned stops;
#define bkagent_cloud_clear() 0
static int bk7258_agent_trigger_stop(void){stops++;return 0;}
static bool pending, network_busy, preferences_pending, voice_interaction_active;
static uint64_t now=100, preferences_retry_at, trigger_retry_at, voice_action_at;
static unsigned events;
static int voice_action;
#define VOICE_ACTION_REARM 1
static unsigned prepares, starts, processes, applied_threshold;
static int threshold_error, start_error;
static bool threshold_ready;
#define bkagent_ota_busy() false
#define bkprov_network_busy() network_busy
#define bkprov_owner_busy() false
#define voice_channel_is_idle() true
#define bk7258_agent_trigger_model_pending() false
static int bk7258_agent_trigger_model_step(bool arm) { (void)arm; return 0; }
static int bk7258_agent_trigger_prepare(void) { prepares++;return 0; }
static int bk7258_preferences_wake_threshold_get(unsigned *value) {
 if(threshold_error)return threshold_error; *value=85;return 0;
}
static int bk7258_agent_trigger_threshold_set(unsigned value) {
 applied_threshold=value; return 0;
}
#define bk7258_agent_trigger_threshold_get() applied_threshold
#define product_apply_persona(value) 0
#define bkagent_memory_restore(value) 0
static int bk7258_preferences_thinking_get(bool *value){*value=false;return 0;}
#define bkagent_cloud_set_thinking(value) ((void)(value))
static int bkvoice_volume_store_get(unsigned *value){*value=40;return 0;}
static int bkvoice_media_volume(bool write,unsigned value,unsigned *out){(void)write;*out=value;return 0;}
static int bk7258_agent_trigger_start(void){starts++;return start_error;}
static int observe_process(void){processes++;return -ENETDOWN;}
#define bk7258_agent_trigger_process(...) observe_process()
static void step(void) { do {
"""
    from test_nfc_rf_lifecycle import function
    clear = function(source, "product_clear")
    suffix = "\n} while(0); }\n" + clear + r"""
int main(int argc,char **argv) {
 assert(argc==2);
 if(!strcmp(argv[1],"content-busy")) {
  content_busy=true;step();assert(!prepares && !starts && !processes);
  puts("CONTRACT_PASS");return 0;
 }
 g_configured=!strcmp(argv[1],"online") || !strcmp(argv[1],"identity-unavailable") || !strcmp(argv[1],"threshold-busy") || !strcmp(argv[1],"model-failure");
 if(!strcmp(argv[1],"network-pending")){pending=true;network_busy=true;}
 if(!strcmp(argv[1],"offline-event")){g_trigger_started=true;g_trigger_prepare_pending=false;events=4;threshold_ready=true;}
 if(!strcmp(argv[1],"core-unavailable"))g_agent_core_ready=false;
 if(!strcmp(argv[1],"identity-unavailable"))g_identity_bound=false;
 if(!strcmp(argv[1],"threshold-busy"))threshold_error=-EBUSY;
 if(!strcmp(argv[1],"model-failure"))start_error=-EBADMSG;
 step();
 if(!strcmp(argv[1],"cloud-retry")) {
  g_trigger_started=true;
  assert(product_clear(NULL)==0 && stops==0 && g_trigger_started);
 } else if(!strcmp(argv[1],"offline-event")) {
  assert(processes==1 && voice_action==VOICE_ACTION_REARM && !voice_interaction_active);
 } else if(!strcmp(argv[1],"model-failure")) {
  assert(starts==1 && !g_trigger_started && g_configured);
  now=200;step();assert(starts==1);
 } else if(!strcmp(argv[1],"core-unavailable") || !strcmp(argv[1],"identity-unavailable")) {
  assert(starts==0 && prepares==0);
 } else if(!strcmp(argv[1],"threshold-busy")) {
  assert(starts==0);
  threshold_error=0; now=1100;step();
  assert(starts==1 && applied_threshold==85);
 } else {
  assert(prepares==1 && starts==1 && g_trigger_started && applied_threshold==85);
 }
 puts("CONTRACT_PASS");return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="local-ready-") as directory:
        root=Path(directory); code=root/"probe.c"; binary=root/"probe"
        code.write_text(prefix+body+suffix)
        build=subprocess.run(["cc","-std=c11","-Wall","-Wno-unused-variable",
                              str(code),"-o",str(binary)],capture_output=True,text=True)
        if build.returncode:
            print("SETUP_ERROR",build.stderr);return 2
        result=subprocess.run([str(binary),case],capture_output=True,text=True)
        print(result.stdout+result.stderr,end="")
        return 0 if result.returncode==0 else 1


def admission(case):
    from test_nfc_rf_lifecycle import function
    source=(ROOT/"app/bk7258/bk7258_agent_trigger.c").read_text()
    body=function(source,"trigger_process_locked")
    # Transparent binding of the existing no-argument API and new admission
    # input. Expected behavior stays fixed; the old missing guard is a new
    # interface safety Red, not a claim about an old reachable offline turn.
    call="trigger_process_locked(admitted)" if "bool admitted" in body else "trigger_process_locked()"
    code=r"""
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#define syslog(...) ((void)0)
static struct { atomic_bool turn_pending,accepting,reply_abort,reply_pending; atomic_int callback_error; void *capture; } g_agent_trigger;
static unsigned starts, closes, rearms;
static int trigger_join(void){return 0;}
static int trigger_pause(void){closes++;g_agent_trigger.capture=NULL;return 0;}
static int trigger_rearm_locked(void){rearms++;return 0;}
static int voice_channel_start_auto_wake(void){starts++;return 0;}
static int audio_capture_cleanup(unsigned ms){(void)ms;return 0;}
"""+body+"\nint main(void){bool admitted="+("true" if case=="online-admission" else "false")+";\n"+r"""
 g_agent_trigger.turn_pending=true;g_agent_trigger.capture=(void*)1;
 int result=CALL;
 assert(!g_agent_trigger.turn_pending);
 if(admitted)assert(result==0 && starts==1 && closes==0);
 else assert(result==-ENETDOWN && starts==0 && closes==1 && rearms==1 && !g_agent_trigger.reply_pending);
 puts("CONTRACT_PASS");return 0;
}
""".replace("CALL",call)
    with tempfile.TemporaryDirectory(prefix="wake-admission-") as directory:
        root=Path(directory);(root/"probe.c").write_text(code)
        r=subprocess.run(["cc","-std=c11",str(root/"probe.c"),"-o",str(root/"probe")],capture_output=True,text=True)
        if r.returncode:print("SETUP_ERROR",r.stderr);return 2
        r=subprocess.run([str(root/"probe")],capture_output=True,text=True)
        print(r.stdout+r.stderr,end="");return 0 if r.returncode==0 else 1

if __name__=="__main__":
    raise SystemExit(main())
