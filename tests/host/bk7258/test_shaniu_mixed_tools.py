#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise Agent's production ReAct loop and vision-cancel handoff.

The planning LLM, tool registry and trace are controlled boundary peers.
The coordinator, assistant/tool history construction, parallel tool dispatch,
finalize deferral, final-phase transition, and vision adapter come from
agent_loop.c itself. mixed-tts also uses the real final-response proxy/parser,
voice reply stream and TTS queue, with controlled SSE, synthesis and Media.
It invokes the ReAct core directly, not the Agent service worker or ASR.
"""

import resource
import subprocess
import sys
import tempfile
from pathlib import Path

from test_nfc_rf_lifecycle import ROOT


CASES = ("mixed", "mixed-tts", "no-tool-final", "unknown-tool", "missing-id", "duplicate-id", "vision-cancel", "vision-success")
MUTATION = "mutant-executes-finalize"


def source_for(case: str) -> str:
    source = (ROOT.parent / "packages/ai_agent/src/core/agent_loop.c").read_text()
    if case != MUTATION:
        return source

    anchor = """if (defer_finalize &&
            !strcmp(tasks[i].call->name, FINAL_PHASE_TOOL)) {"""
    replacement = """if (false && defer_finalize &&
            !strcmp(tasks[i].call->name, FINAL_PHASE_TOOL)) {"""
    if source.count(anchor) != 1:
        raise RuntimeError("finalize-deferral mutation anchor changed")
    return source.replace(anchor, replacement)


def probe_code(case: str) -> str:
    scenario = "mixed" if case == MUTATION else case
    tts_fixture = ROOT / "tests/host/bk7258/test_bk7258_agent_media_player.c"
    return r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llm/llm_proxy.h"
int llm_chat_vision_checked(const char *prompt, const char *image_b64,
                            const char *mime_type, char *response_buf,
                            size_t buf_size, int (*check)(void *),
                            void *request_context);

#ifdef TEST_MIXED_TTS
/* Reuse the maintained real voice queue/Media fixture.  Renaming its cancel
 * stub leaves Agent's normal final-stream cancellation symbol available. */
#define TEST_AGENT_TTS_QUEUE 1
#define llm_cancel_request tts_fixture_llm_cancel_request
int tts_fixture_llm_cancel_request(void);
#define main tts_fixture_regression_main
#include "''' + str(tts_fixture) + r'''"
#undef main
#undef llm_cancel_request
#endif

#ifdef TEST_MIXED_TTS
#define TAG agent_loop_fixture_tag
/* Planning remains the existing controlled ReAct peer; the final request is
 * the real proxy/stream implementation linked below. */
#define llm_chat_plan_checked fixture_llm_chat_plan_checked
#define llm_chat_tools_checked fixture_llm_chat_tools_checked
int fixture_llm_chat_plan_checked(const char *, cJSON *, const char *,
    llm_response_t *, int (*)(void *), void *);
int fixture_llm_chat_tools_checked(const char *, cJSON *, const char *,
    llm_response_t *, int (*)(void *), void *);
#endif
#include <stdarg.h>
struct stage_event { uint64_t request, mono_ms, value; int result; char stage[32]; };
static struct stage_event stage_events[16];
static unsigned stage_count;
static void test_syslog(int priority, const char *format, ...)
{
    (void)priority;
    char line[192];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (strncmp(line, "[voice-stage] ", 14)) return;
    assert(stage_count < sizeof(stage_events) / sizeof(stage_events[0]));
    struct stage_event *event = &stage_events[stage_count++];
    unsigned long long request, mono_ms, value;
    assert(sscanf(line, "[voice-stage] request=%llu mono_ms=%llu stage=%31s value=%llu result=%d",
        &request, &mono_ms, event->stage, &value, &event->result) == 5);
    event->request = request;
    event->mono_ms = mono_ms;
    event->value = value;
}
#define syslog test_syslog
#include "agent_loop_under_test.c"
#undef syslog
#ifdef TEST_MIXED_TTS
#undef TAG
#endif

static const char *scenario = "''' + scenario + r'''";
static unsigned planning_requests;
static unsigned final_requests;
static unsigned registry_calls;
static unsigned begin_events;
static unsigned delta_events;
static unsigned legacy_vision_calls;
static unsigned checked_vision_calls;
static int request_canceled;
#ifndef TEST_MIXED_TTS
static char spoken[128];
static size_t spoken_size;
#endif

static void assert_stages(const char *const *expected, size_t count)
{
#ifdef TEST_MIXED_TTS
    /* media_player includes voice_channel (and agent_trace.h) first, so the
     * static helper binds to its existing syslog observer. Filter its real
     * voice stages and assert the Agent subset without assuming a separate
     * header instantiation. */
    size_t found = 0;
    uint64_t request = 0, previous_ms = 0;
    pthread_mutex_lock(&test_stage_lock);
    for (size_t n = 0; n < test_stage_count; n++) {
        size_t expected_index = count;
        for (size_t i = 0; i < count; i++)
            if (!strcmp(test_stages[n].name, expected[i])) {
                expected_index = i;
                break;
            }
        if (expected_index == count) continue;
        assert(found < count && expected_index == found);
        assert(test_stages[n].request != 0);
        if (found) assert(test_stages[n].request == request);
        else request = test_stages[n].request;
        if (found) assert(test_stages[n].ms >= previous_ms);
        previous_ms = test_stages[n].ms;
        found++;
    }
    pthread_mutex_unlock(&test_stage_lock);
    assert(found == count);
#else
    assert(stage_count == count);
    for (size_t i = 0; i < count; i++) {
        assert(stage_events[i].request != 0);
        if (i) assert(stage_events[i].request == stage_events[0].request);
        assert(!strcmp(stage_events[i].stage, expected[i]));
        if (i) assert(stage_events[i].mono_ms >= stage_events[i - 1].mono_ms);
    }
#endif
}

int llm_chat_vision(const char *prompt, const char *image_b64,
                    const char *mime_type, char *response_buf,
                    size_t buf_size)
{
    (void)prompt;
    (void)image_b64;
    (void)mime_type;
    (void)response_buf;
    (void)buf_size;
    legacy_vision_calls++;
    request_canceled = 1;
    return -ECANCELED;
}

int llm_chat_vision_checked(const char *prompt, const char *image_b64,
                            const char *mime_type, char *response_buf,
                            size_t buf_size, int (*check)(void *),
                            void *request_context)
{
    (void)mime_type;
    assert(prompt != NULL && !strcmp(prompt, "describe image"));
    assert(image_b64 != NULL && !strcmp(image_b64, "aW1hZ2U="));
    assert(response_buf != NULL && buf_size >= TOOL_OUTPUT_SIZE_MIN);
    assert(check != NULL && check(request_context) == 0);
    checked_vision_calls++;
    if (!strcmp(scenario, "vision-success")) {
        snprintf(response_buf, buf_size, "A complete visual answer.");
        return 0;
    }
    request_canceled = 1;
    assert(check(request_context) == -ECANCELED);
    return -ECANCELED;
}

static void set_call(llm_response_t *response, int index, const char *id,
                     const char *name)
{
    if (id != NULL) {
        strcpy(response->calls[index].id, id);
    }
    strcpy(response->calls[index].name, name);
    response->calls[index].input = strdup("{}");
    assert(response->calls[index].input != NULL);
    response->calls[index].input_len = 2;
}

static void assert_tool_result(cJSON *message, const char *id,
                               const char *content_fragment)
{
    cJSON *role = cJSON_GetObjectItemCaseSensitive(message, "role");
    cJSON *call_id = cJSON_GetObjectItemCaseSensitive(message, "tool_call_id");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    assert(cJSON_IsString(role) && !strcmp(role->valuestring, "tool"));
    assert(cJSON_IsString(call_id) && !strcmp(call_id->valuestring, id));
    assert(cJSON_IsString(content) && strstr(content->valuestring, content_fragment));
}

int llm_chat_plan_checked(const char *system, cJSON *messages,
                          const char *tools, llm_response_t *response,
                          int (*check)(void *), void *request)
{
    assert(system != NULL && strstr(system, "first use any necessary tools"));
    if (!strcmp(scenario, "no-tool-final")) {
        assert(strstr(system, "If no tools are needed and you can answer completely, "
                             "return the complete final answer directly."));
    }
    assert(tools != NULL && strstr(tools, "agent_finalize"));
    assert(check != NULL && check(request) == 0);
    memset(response, 0, sizeof(*response));
    planning_requests++;
#ifdef TEST_MIXED_TTS
    assert(cJSON_GetArraySize(messages) >= 1);
    cJSON *user = cJSON_GetArrayItem(messages, 0);
    cJSON *role = cJSON_GetObjectItemCaseSensitive(user, "role");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(user, "content");
    assert(cJSON_IsString(role) && !strcmp(role->valuestring, "user"));
    assert(cJSON_IsString(content) && !strcmp(content->valuestring, "fixed ASR input"));
#endif

    if (planning_requests == 1) {
        if (!strcmp(scenario, "no-tool-final")) {
            response->tool_phase_complete = true;
            response->text = strdup("Complete no-tool answer.");
            assert(response->text != NULL);
            response->text_len = strlen(response->text);
            return 0;
        }
        if (!strcmp(scenario, "unknown-tool")) {
            response->tool_use = true;
            response->call_count = 1;
            set_call(response, 0, "unknown-1", "not_registered");
            return 0;
        }
        response->tool_use = true;
        response->tool_phase_complete = true;
        response->call_count = 2;
        if (!strcmp(scenario, "missing-id")) {
            set_call(response, 0, NULL, "get_weather");
            set_call(response, 1, "finish-1", FINAL_PHASE_TOOL);
        } else if (!strcmp(scenario, "duplicate-id")) {
            set_call(response, 0, "same-id", "get_weather");
            set_call(response, 1, "same-id", FINAL_PHASE_TOOL);
        } else {
            set_call(response, 0, "real-1", "get_weather");
            set_call(response, 1, "finish-1", FINAL_PHASE_TOOL);
        }
        return 0;
    }

    if (!strcmp(scenario, "unknown-tool")) {
        assert(planning_requests == 2 && cJSON_GetArraySize(messages) == 2);
        assert_tool_result(cJSON_GetArrayItem(messages, 1), "unknown-1",
                           "unknown tool");
        assert(registry_calls == 1 && final_requests == 0);
        response->tool_phase_complete = true;
        response->text = strdup("Unknown-tool result retained.");
        assert(response->text != NULL);
        response->text_len = strlen(response->text);
        return 0;
    }
    assert(!strcmp(scenario, "mixed") || !strcmp(scenario, "mixed-tts"));
    assert(planning_requests == 2);
#ifdef TEST_MIXED_TTS
    assert(cJSON_GetArraySize(messages) == 4);
    int offset = 1;
#else
    assert(cJSON_GetArraySize(messages) == 3);
    int offset = 0;
#endif
    cJSON *assistant = cJSON_GetArrayItem(messages, offset);
    cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls");
    assert(cJSON_IsArray(tool_calls) && cJSON_GetArraySize(tool_calls) == 2);
    assert_tool_result(cJSON_GetArrayItem(messages, offset + 1), "real-1", "sunny");
    assert_tool_result(cJSON_GetArrayItem(messages, offset + 2), "finish-1", "deferred");
    assert(registry_calls == 1 && final_requests == 0);

    response->tool_use = true;
    response->tool_phase_complete = true;
    response->call_count = 1;
    set_call(response, 0, "finish-2", FINAL_PHASE_TOOL);
    return 0;
}

int llm_chat_tools_checked(const char *system, cJSON *messages,
                           const char *tools, llm_response_t *response,
                           int (*check)(void *), void *request)
{
    (void)system;
    (void)messages;
    (void)tools;
    (void)response;
    (void)check;
    (void)request;
    abort();
}

#ifndef TEST_MIXED_TTS
int llm_chat_final_stream_checked(const char *system, cJSON *messages,
                                  llm_response_t *response,
                                  llm_text_delta_t emit, void *emit_context,
                                  int (*check)(void *), void *request)
{
    assert(!strcmp(system, "system"));
    assert(planning_requests == 2 && registry_calls == 1);
    assert(cJSON_GetArraySize(messages) == 5);
    assert_tool_result(cJSON_GetArrayItem(messages, 4), "finish-2",
                       "Tool phase completed");
    assert(check != NULL && check(request) == 0);
    final_requests++;
    memset(response, 0, sizeof(*response));
    response->text = strdup("Mixed final.");
    assert(response->text != NULL);
    response->text_len = strlen(response->text);
    return emit(emit_context, response->text, response->text_len);
}

int llm_final_stream_supported(void)
{
    return 1;
}

void llm_response_free(llm_response_t *response)
{
    if (response == NULL) {
        return;
    }
    free(response->text);
    free(response->reasoning_content);
    for (int i = 0; i < AGENT_MAX_TOOL_CALLS; i++) {
        free(response->calls[i].input);
    }
    memset(response, 0, sizeof(*response));
}
#endif

int tool_registry_execute_checked(const char *name, const char *input,
                                  char *output, size_t output_size,
                                  int (*check)(void *), void *request)
{
    if (!strcmp(name, "not_registered")) {
        assert(!strcmp(input, "{}"));
        assert(check != NULL && check(request) == 0);
        registry_calls++;
        int length = snprintf(output, output_size, "{\"error\":\"unknown tool\"}");
        assert(length > 0 && (size_t)length < output_size);
        return -ENOENT;
    }
    assert(!strcmp(name, "get_weather"));
    assert(!strcmp(input, "{}"));
    assert(check != NULL && check(request) == 0);
    registry_calls++;
    int length = snprintf(output, output_size, "{\"weather\":\"sunny\"}");
    assert(length > 0 && (size_t)length < output_size);
    return 0;
}

llm_complexity_t llm_router_estimate_complexity(const char *prompt, size_t length)
{
    assert(prompt != NULL && length == strlen(prompt));
    return LLM_COMPLEXITY_SIMPLE;
}
int llm_router_select(llm_complexity_t complexity)
{
    assert(complexity == LLM_COMPLEXITY_SIMPLE);
    return -1;
}
int llm_router_select_with_profile(llm_complexity_t complexity,
                                   llm_route_profile_t profile)
{
    (void)complexity;
    (void)profile;
    return -1;
}
int llm_router_apply(int index) { (void)index; return 0; }
llm_route_profile_t llm_router_get_profile(void) { return LLM_ROUTE_AUTO; }
void llm_router_report_failure(int index) { (void)index; }
void llm_router_report_success(int index) { (void)index; }
void llm_router_report_latency(int index, uint32_t latency) { (void)index; (void)latency; }
void llm_router_report_tokens(int index, int prompt, int completion)
{ (void)index; (void)prompt; (void)completion; }

void agent_trace_begin(agent_trace_t *trace, const char *chat_id,
                       const char *channel)
{
    memset(trace, 0, sizeof(*trace));
    assert(!strcmp(chat_id, "chat") && !strcmp(channel, AGENT_CHAN_VOICE));
}
void agent_trace_step(agent_trace_t *trace, int iteration, const char *tool,
                      uint32_t latency, int ok)
{ (void)trace; (void)iteration; (void)tool; (void)latency; (void)ok; }
void agent_trace_end(agent_trace_t *trace, int status)
{ (void)trace; (void)status; }

#ifndef TEST_MIXED_TTS
int message_bus_push_outbound(const agent_msg_t *message)
{ (void)message; abort(); }
#endif

static int request_status(uint64_t request_id)
{
#ifdef TEST_MIXED_TTS
    return voice_request_status(request_id);
#else
    assert(request_id == 7);
    return request_canceled ? -ECANCELED : 0;
#endif
}

static int reply_sink(uint64_t request_id, int event,
                      const char *text, size_t length)
{
#ifdef TEST_MIXED_TTS
    return voice_channel_reply_stream(request_id, event, text, length);
#else
    assert(request_id == 7);
    if (event == AGENT_REPLY_BEGIN) {
        assert(text == NULL && length == 0);
        begin_events++;
        return 0;
    }
    assert(event == AGENT_REPLY_DELTA);
    assert(spoken_size + length < sizeof(spoken));
    memcpy(spoken + spoken_size, text, length);
    spoken_size += length;
    spoken[spoken_size] = 0;
    delta_events++;
    return 0;
#endif
}

#ifdef TEST_MIXED_TTS
static int unused_transport(const char *request, char *response,
    size_t capacity, size_t *length, int *status, void *context,
    int (*check)(void *), void *request_context)
{
    (void)request; (void)response; (void)capacity; (void)length;
    (void)status; (void)context; (void)check; (void)request_context;
    abort();
}

static int controlled_final_transport(const char *request,
    int (*receive)(void *, const char *, size_t), void *receive_context,
    int *status, void *context, int (*check)(void *), void *request_context)
{
    (void)context;
    assert(strstr(request, "\"stream\":true") != NULL);
    assert(strstr(request, "fixed ASR input") != NULL);
    assert(check && check(request_context) == 0);
    assert(++final_requests == 1);
    *status = 200;
    const char first[] = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"你好，这是第一句。\"}}]}\n\n";
    assert(receive(receive_context, first, sizeof(first) - 1) == 0);
    test_wait_first_pcm();
    assert(test_drains == 0 && test_closes == 0);
    const char tail[] = "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"这是尾句\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
    int done = receive(receive_context, tail, sizeof(tail) - 1);
    assert(done == 1);
    return 0;
}
#endif

int main(void)
{
    cJSON *messages = cJSON_CreateArray();
    assert(messages != NULL);
    char tool_output[4096] = {0};
    agent_msg_t message = {
        .request_id = 7,
        .request_status = request_status,
        .reply_stream = reply_sink,
        .content = "weather",
    };
    strcpy(message.channel, AGENT_CHAN_VOICE);
    strcpy(message.chat_id, "chat");
    int failure = 0;

#ifdef TEST_MIXED_TTS
    /* Fixed text is injected after ASR. Planning, tools, history and the
     * Media lower-half are controlled peers; capture/worker are not invoked. */
    message.request_id = test_reply_prepare(5);
    message.request_status = voice_request_status;
    message.request_commit = voice_request_commit;
    message.request_complete = voice_request_complete;
    message.content = "fixed ASR input";
    cJSON *user = cJSON_CreateObject();
    assert(user && cJSON_AddStringToObject(user, "role", "user") &&
           cJSON_AddStringToObject(user, "content", message.content) &&
           cJSON_AddItemToArray(messages, user));
    assert(llm_set_transports("fixture", "fixture.test", unused_transport,
        controlled_final_transport, NULL, NULL) == 0);
#endif

    if (!strcmp(scenario, "vision-cancel")) {
        message.content = "describe image";
        message.image_b64 = strdup("aW1hZ2U=");
        assert(message.image_b64 != NULL);
        char *text = handle_vision_message(&message);
        if (text == NULL) {
            text = run_react_loop("system", messages, "[]", tool_output,
                                  sizeof(tool_output), &message, &failure);
        }
        assert(text == NULL && failure == -ECANCELED);
        assert(message.image_b64 == NULL);
        assert(checked_vision_calls == 1 && legacy_vision_calls == 0);
        assert(planning_requests == 0 && final_requests == 0 && registry_calls == 0);
        cJSON_Delete(messages);
        puts("CONTRACT_PASS");
        return 0;
    }
    if (!strcmp(scenario, "vision-success")) {
        message.content = "describe image";
        message.image_b64 = strdup("aW1hZ2U=");
        assert(message.image_b64 != NULL);
        char *text = handle_vision_message(&message);
        assert(text != NULL && !strcmp(text, "A complete visual answer."));
        assert(message.image_b64 == NULL);
        assert(checked_vision_calls == 1 && legacy_vision_calls == 0);
        assert(planning_requests == 0 && final_requests == 0 && registry_calls == 0);
        free(text);
        cJSON_Delete(messages);
        puts("CONTRACT_PASS");
        return 0;
    }

    char *text = run_react_loop(
        "system", messages,
        "[{\"name\":\"get_weather\",\"description\":\"Weather\","
        "\"input_schema\":{\"type\":\"object\",\"properties\":{}}}]",
        tool_output, sizeof(tool_output), &message, &failure);

    if (!strcmp(scenario, "no-tool-final")) {
#ifdef TEST_MIXED_TTS
        assert(failure == 0 && text != NULL &&
               !strcmp(text, "Complete no-tool answer."));
        assert(planning_requests == 1 && registry_calls == 0 && final_requests == 0);
        test_history_count = test_cancel_at_history = 0;
        assert(message_bus_reply_with_history(&message, text, 0, test_history) == 0);
        text = NULL;
        assert(test_history_count == 1 && s_voice.reply_committed);
        assert(test_synth_calls == 1 &&
               !strcmp(test_spoken[0], "Complete no-tool answer."));
        assert(test_opens == 1 && test_drains == 1 && test_closes == 1);
        assert(voice_channel_is_idle());
        assert(llm_clear_transport() == 0);
        const char *const expected[] = {"plan_request", "plan_response"};
        assert_stages(expected, 2);
#else
        abort();
#endif
    } else if (!strcmp(scenario, "unknown-tool")) {
        assert(failure == 0 && text != NULL &&
               !strcmp(text, "Unknown-tool result retained."));
        assert(planning_requests == 2 && registry_calls == 1 && final_requests == 0);
#ifdef TEST_MIXED_TTS
        abort();
#else
        assert(begin_events == 1 && delta_events == 1 && !strcmp(spoken, text));
#endif
        assert(cJSON_GetArraySize(messages) == 2);
    } else if (!strcmp(scenario, "mixed") || !strcmp(scenario, "mixed-tts")) {
#ifdef TEST_MIXED_TTS
        assert(failure == 0 && text != NULL &&
               !strcmp(text, "你好，这是第一句。这是尾句"));
        assert(planning_requests == 2 && registry_calls == 1 && final_requests == 1);
        test_history_count = test_cancel_at_history = 0;
        assert(message_bus_reply_with_history(&message, text, 0, test_history) == 0);
        text = NULL;
        assert(test_history_count == 1 && s_voice.reply_committed);
        assert(test_synth_calls == 2 && test_written == 16384);
        assert(test_opens == 1 && test_drains == 1 && test_closes == 1);
        assert(!strcmp(test_spoken[0], "你好，这是第一句。"));
        assert(!strcmp(test_spoken[1], "这是尾句"));
        assert(voice_channel_is_idle());
        assert(llm_clear_transport() == 0);
#else
        assert(failure == 0 && text != NULL && !strcmp(text, "Mixed final."));
        assert(planning_requests == 2 && registry_calls == 1 && final_requests == 1);
        assert(begin_events == 1 && delta_events == 1 && !strcmp(spoken, text));
        const char *const expected[] = {
            "plan_request", "plan_response", "tool_start", "tool_result",
            "plan_request", "plan_response", "final_request", "final_response",
        };
        assert_stages(expected, sizeof(expected) / sizeof(expected[0]));
#endif
#ifdef TEST_MIXED_TTS
        assert(cJSON_GetArraySize(messages) == 6);
#else
        assert(cJSON_GetArraySize(messages) == 5);
#endif
    } else {
        assert(failure == -EPROTO && text == NULL);
        assert(planning_requests == 1 && registry_calls == 0 && final_requests == 0);
        assert(begin_events == 0 && delta_events == 0 && cJSON_GetArraySize(messages) == 0);
    }

    free(text);
    cJSON_Delete(messages);
    puts("CONTRACT_PASS");
    return 0;
}
'''


def build_and_run(case: str) -> tuple[bool, subprocess.CompletedProcess[str]]:
    agent = ROOT.parent / "packages/ai_agent"
    cjson = ROOT.parent / "apps/netutils/cjson/cJSON"
    with tempfile.TemporaryDirectory(prefix="agent-mixed-tools-") as directory:
        temp = Path(directory)
        (temp / "agent_loop_under_test.c").write_text(source_for(case))
        (temp / "probe.c").write_text(probe_code(case))
        command = [
            "cc",
            "-std=gnu11",
            "-pthread",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-error=deprecated-declarations",
            "-Wno-error=stringop-truncation",
            "-O2",
            "-D_DEFAULT_SOURCE=1",
            "-D_GNU_SOURCE=1",
            "-DOK=0",
            "-DERROR=-1",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "-I",
            str(temp),
            "-I",
            str(ROOT / "tests/host/bk7258/mocks"),
            "-I",
            str(agent / "include"),
            "-I",
            str(agent / "src"),
            "-I",
            str(cjson),
            str(temp / "probe.c"),
            str(cjson / "cJSON.c"),
            "-o",
            str(temp / "probe"),
        ]
        if case in ("mixed-tts", "no-tool-final"):
            command.extend([
                str(agent / "src/llm/llm_proxy.c"),
                str(agent / "src/llm/llm_parse.c"),
                str(agent / "src/llm/llm_stream.c"),
                str(agent / "src/core/message_bus.c"),
            ])
            command.remove("-o")
            command.remove(str(temp / "probe"))
            command.extend(["-o", str(temp / "probe")])
            command.insert(1, "-DTEST_MIXED_TTS=1")
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode:
            print("SETUP_ERROR", built.stderr, end="")
            return False, built
        return True, subprocess.run(
            [str(temp / "probe")], capture_output=True, text=True, timeout=15
        )


def main() -> int:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    if len(sys.argv) != 2 or sys.argv[1] not in (*CASES, MUTATION):
        print("usage: test_shaniu_mixed_tools.py " + "|".join((*CASES, MUTATION)))
        return 2
    case = sys.argv[1]
    built, result = build_and_run(case)
    print(result.stdout + result.stderr, end="")
    if not built:
        return 2
    if case == MUTATION:
        if result.returncode == 0:
            print("MUTATION_SURVIVED")
            return 1
        print("MUTATION_DETECTED")
        return 0
    return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
