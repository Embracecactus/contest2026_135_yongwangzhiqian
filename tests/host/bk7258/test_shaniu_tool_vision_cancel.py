#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise cancellation through production checked tool routes.

The registry dispatcher and registration paths come from the Agent checkout.
Only the external provider/vision boundary, guard ledger, and tiny image file
are controlled.  Cancellation is raised while that boundary is active.
"""

import resource
import subprocess
import sys
import tempfile
from pathlib import Path

from test_nfc_rf_lifecycle import ROOT


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise RuntimeError(f"unterminated function: {signature}")


def registry_slice(agent: Path) -> str:
    source = (agent / "src/tools/tool_registry.c").read_text()
    start = source.index("    /* Vision tool */")
    end = source.index("    /* Camera capture tool */", start)
    registration = source[start:end]
    register_provider = extract_function(source, "static void register_provider(")
    register_checked = extract_function(
        source, "void tool_registry_register_provider_checked("
    )
    execute = extract_function(source, "int tool_registry_execute_checked(")
    return r'''
#include "tools/tool_registry.h"
#include "tools/tool_guard.h"
#include "tools/tool_vision.h"
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#define MAX_TOOLS 4
#define MAX_PROVIDERS 1
static const char *TAG = "tools-test";

typedef struct {
    const char *name;
    tool_provider_fn get_tools;
    tool_executor_fn execute;
    tool_executor_checked_fn execute_checked;
} tool_provider_t;

static tool_provider_t s_providers[MAX_PROVIDERS];
static int s_provider_count;
static agent_tool_t s_tools[MAX_TOOLS];
static int s_tool_count;

static void register_tool(const agent_tool_t *tool)
{
    s_tools[s_tool_count++] = *tool;
}

''' + register_provider + "\n\n" + register_checked + r'''

void test_register_vision_tool(void)
{
''' + registration + r'''
}

''' + execute + "\n"


def probe_code() -> str:
    return r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llm/llm_proxy.h"
#include "tools/tool_guard.h"
#include "tools/tool_registry.h"

int llm_chat_vision_raw_checked(const char *prompt,
    const unsigned char *raw_image, size_t raw_len,
    const char *mime_type, char *response_buf, size_t buf_size,
    int (*check)(void *), void *request_context);
void test_register_vision_tool(void);

static int canceled;
static int legacy_calls;
static int checked_calls;
static int guard_records;

static int request_check(void *context)
{
    assert(context == &canceled);
    return canceled ? -ECANCELED : 0;
}

tool_guard_result_t tool_guard_check(const char *name,
    const char *input, size_t input_len)
{
    assert(!strcmp(name, "analyze_image"));
    assert(input != NULL && input_len == strlen(input));
    return GUARD_ALLOW;
}

void tool_guard_record_call(const char *name)
{
    assert(!strcmp(name, "analyze_image"));
    guard_records++;
}

int mbedtls_base64_encode(unsigned char *output, size_t capacity,
    size_t *written, const unsigned char *input, size_t input_len)
{
    (void)input;
    size_t needed = ((input_len + 2) / 3) * 4;
    assert(output != NULL && capacity >= needed && written != NULL);
    memset(output, 'A', needed);
    *written = needed;
    return 0;
}

int llm_chat_vision_raw(const char *prompt,
    const unsigned char *raw_image, size_t raw_len,
    const char *mime_type, char *response_buf, size_t buf_size)
{
    (void)prompt;
    assert(raw_image != NULL && raw_len > 0);
    assert(!strcmp(mime_type, "image/png"));
    legacy_calls++;
    canceled = 1;
    snprintf(response_buf, buf_size, "legacy response after cancellation");
    return 0;
}

int llm_chat_vision_raw_checked(const char *prompt,
    const unsigned char *raw_image, size_t raw_len,
    const char *mime_type, char *response_buf, size_t buf_size,
    int (*check)(void *), void *request_context)
{
    (void)prompt;
    assert(raw_image != NULL && raw_len > 0);
    assert(!strcmp(mime_type, "image/png"));
    assert(check != NULL && check(request_context) == 0);
    checked_calls++;
    canceled = 1;
    /* A peer may complete at the same boundary at which cancellation wins.
     * The production checked route must re-check before committing success. */
    snprintf(response_buf, buf_size, "late peer success");
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_register_vision_tool();
    char input[512];
    int length = snprintf(input, sizeof(input),
        "{\"image_path\":\"%s\",\"prompt\":\"describe\"}", argv[1]);
    assert(length > 0 && (size_t)length < sizeof(input));

    char output[512] = {0};
    int ret = tool_registry_execute_checked("analyze_image", input,
        output, sizeof(output), request_check, &canceled);

    printf("ret=%d checked_calls=%d legacy_calls=%d guard_records=%d "
           "output_size=%zu\n", ret, checked_calls, legacy_calls,
           guard_records, strlen(output));
    fflush(stdout);
    assert(ret == -ECANCELED);
    assert(checked_calls == 1);
    assert(legacy_calls == 0);
    assert(guard_records == 0);
    assert(output[0] == '\0');
    puts("CONTRACT_PASS");
    return 0;
}
'''


def provider_probe_code() -> str:
    return r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "tools/tool_guard.h"
#include "tools/tool_registry.h"

static int canceled;
static int checked_calls;
static int guard_records;

static int request_check(void *context)
{
    assert(context == &canceled);
    return canceled ? -ECANCELED : 0;
}

tool_guard_result_t tool_guard_check(const char *name,
    const char *input, size_t input_len)
{
    assert(!strcmp(name, "provider_tool"));
    assert(input != NULL && input_len == strlen(input));
    return GUARD_ALLOW;
}

void tool_guard_record_call(const char *name)
{
    assert(!strcmp(name, "provider_tool"));
    guard_records++;
}

static int provider_execute_checked(const char *name, const char *input,
    char *output, size_t output_size, int (*check)(void *),
    void *request_context)
{
    assert(!strcmp(name, "provider_tool"));
    assert(!strcmp(input, "{}"));
    assert(check != NULL && check(request_context) == 0);
    checked_calls++;
    canceled = 1;
    /* The peer completed at the same boundary at which cancellation won. */
    snprintf(output, output_size, "late provider success");
    return 0;
}

int main(void)
{
    tool_registry_register_provider_checked(
        "product", NULL, provider_execute_checked);

    char output[128] = {0};
    int ret = tool_registry_execute_checked("provider_tool", "{}",
        output, sizeof(output), request_check, &canceled);

    printf("ret=%d checked_calls=%d guard_records=%d output_size=%zu\n",
           ret, checked_calls, guard_records, strlen(output));
    fflush(stdout);
    assert(ret == -ECANCELED);
    assert(checked_calls == 1);
    assert(guard_records == 0);
    assert(output[0] == '\0');
    puts("CONTRACT_PASS");
    return 0;
}
'''


def main() -> int:
    case = "builtin"
    if len(sys.argv) == 2:
        case = sys.argv[1]
    elif len(sys.argv) > 2:
        print("usage: test_shaniu_tool_vision_cancel.py [builtin|provider]")
        return 2
    if case not in ("builtin", "provider"):
        print("usage: test_shaniu_tool_vision_cancel.py [builtin|provider]")
        return 2

    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    agent = ROOT.parent / "packages/ai_agent"
    cjson = ROOT.parent / "apps/netutils/cjson/cJSON"
    mbedtls = ROOT.parent / "apps/crypto/mbedtls/mbedtls/include"
    with tempfile.TemporaryDirectory(prefix="agent-tool-vision-cancel-") as directory:
        temp = Path(directory)
        image = temp / "fixture.png"
        image.write_bytes(b"\x89PNG\r\n\x1a\nfixture")
        (temp / "registry_slice.c").write_text(registry_slice(agent))
        (temp / "probe.c").write_text(
            provider_probe_code() if case == "provider" else probe_code()
        )
        command = [
            "cc",
            "-std=gnu11",
            "-pthread",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            "-D_DEFAULT_SOURCE=1",
            "-D_GNU_SOURCE=1",
            "-DOK=0",
            "-DERROR=-1",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "-I",
            str(ROOT / "tests/host/bk7258/mocks"),
            "-I",
            str(agent / "include"),
            "-I",
            str(agent / "src"),
            "-I",
            str(cjson),
            "-I",
            str(mbedtls),
            str(temp / "registry_slice.c"),
            str(temp / "probe.c"),
            str(agent / "src/tools/tool_vision.c"),
            str(cjson / "cJSON.c"),
            "-o",
            str(temp / "probe"),
        ]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode:
            print("SETUP_ERROR", built.stderr, end="")
            return 2
        args = [str(temp / "probe")]
        if case == "builtin":
            args.append(str(image))
        result = subprocess.run(args, capture_output=True, text=True)
        print(result.stdout + result.stderr, end="")
        return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
