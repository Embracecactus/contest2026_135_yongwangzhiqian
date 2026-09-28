#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual Agent final-phase function; cloud and reply consumer are boundary peers.

The existing real proxy/parser tests independently validate the no-tool stop.
This probe verifies reuse, finalization, cancellation and message construction;
it is not a whole Agent/tool/voice round or a physical latency measurement.
"""
import resource
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path
from test_nfc_rf_lifecycle import function, ROOT

CASES = ('reuse', 'empty', 'finalize', 'cancel', 'sink-cancel')

def main():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    case=sys.argv[1]
    assert case in CASES
    source=(ROOT.parent/'packages/ai_agent/src/core/agent_loop.c').read_text()
    start=source.index('static int final_body_delta(')
    end=source.index('\nstatic char* run_react_loop(',start)
    code='''#define main existing_regression_main
#include "'''+str(ROOT/'tests/host/bk7258/test_bk7258_cloud_request.c')+'''"
#undef main
#include "core/message_bus.h"
#define FINAL_PHASE_TOOL "agent_finalize"
static unsigned final_requests, begins, deltas;
static int canceled, reject_sink;
static int status(uint64_t id){assert(id==7);return canceled ? -ECANCELED : 0;}
static int sink(uint64_t id,int event,const char *text,size_t length){
 assert(id==7);
 if(event==AGENT_REPLY_BEGIN){begins++;return 0;}
 assert(event==AGENT_REPLY_DELTA);deltas++;
 if(reject_sink)return -ECANCELED;
 assert(spoken_size+length<sizeof(spoken));
 memcpy(spoken+spoken_size,text,length);spoken_size+=length;return 0;
}
static int phase_cloud(const char *system,cJSON *messages,llm_response_t *resp,
 llm_text_delta_t emit,void *context,int (*check)(void *),void *request){
 (void)system;(void)messages;final_requests++;memset(resp,0,sizeof(*resp));
 int ret=check(request);if(ret)return ret;
 resp->text=strdup("Streamed final.");resp->text_len=strlen(resp->text);
 return emit(context,resp->text,resp->text_len);
}
#define llm_chat_final_stream_checked phase_cloud
'''
    code+=function(source,'agent_request_check')+function(source,'add_assistant_message')+source[start:end]
    code+=r'''
int main(int argc,char **argv){
 assert(argc==2);
 bool reuse=!strcmp(argv[1],"reuse"), empty=!strcmp(argv[1],"empty");
 bool finalize=!strcmp(argv[1],"finalize");
 canceled=!strcmp(argv[1],"cancel");reject_sink=!strcmp(argv[1],"sink-cancel");
 llm_response_t plan={0};plan.tool_phase_complete=true;
 if(!empty){plan.text=strdup("Complete answer.");plan.text_len=strlen(plan.text);}
 if(finalize){plan.tool_use=true;plan.call_count=1;
  strcpy(plan.calls[0].id,"final");strcpy(plan.calls[0].name,FINAL_PHASE_TOOL);
  plan.calls[0].input=strdup("{}");}
 cJSON *messages=cJSON_CreateArray();
 agent_msg_t msg={.request_id=7,.request_status=status,.reply_stream=sink};
 int failure=0;char *text=voice_final_phase("system",messages,&plan,&msg,&failure);
 if(canceled){assert(failure==-ECANCELED && !text && !begins && !final_requests);}
 else if(reject_sink){assert(failure==-ECANCELED && !text && begins==1 && !final_requests);}
 else if(reuse){
  assert(!failure && text && !strcmp(text,"Complete answer."));
  assert(final_requests==0 && begins==1 && deltas==1 && !strcmp(spoken,text));
  assert(cJSON_GetArraySize(messages)==0);
 }else{
  assert(!failure && text && !strcmp(text,"Streamed final."));
  assert(final_requests==1 && begins==1 && deltas==1);
  assert(cJSON_GetArraySize(messages)==(finalize?2:0));
 }
 free(text);llm_response_free(&plan);cJSON_Delete(messages);
 puts("CONTRACT_PASS");return 0;
}
'''
    here=ROOT/'tests/host/bk7258'
    with tempfile.TemporaryDirectory(prefix='agent-final-body-') as directory:
        temp=Path(directory);(temp/'probe.c').write_text(code)
        dry=subprocess.run(['make','-n','-B','build/test_agent_final_stream'],cwd=here,capture_output=True,text=True,check=True).stdout
        lines=dry.replace('\\\n',' ').splitlines()
        command=next(shlex.split(line) for line in lines if line.startswith('cc ') and '-DTEST_AGENT_FINAL_STREAM' in line)
        command[command.index('test_bk7258_cloud_request.c')]=str(temp/'probe.c')
        command[command.index('-o')+1]=str(temp/'probe')
        build=subprocess.run(command,cwd=here,capture_output=True,text=True)
        if build.returncode:print('SETUP_ERROR',build.stderr);return 2
        result=subprocess.run([str(temp/'probe'),case],capture_output=True,text=True)
        print(result.stdout+result.stderr,end='');return 0 if result.returncode==0 else 1

if __name__=='__main__':raise SystemExit(main())
